#include <futurehead/node/bootstrap/bootstrap.hpp>
#include <futurehead/node/bootstrap/bootstrap_bulk_pull.hpp>
#include <futurehead/node/bootstrap/bootstrap_connections.hpp>
#include <futurehead/node/bootstrap/bootstrap_lazy.hpp>
#include <futurehead/node/node.hpp>
#include <futurehead/node/transport/tcp.hpp>

#include <boost/format.hpp>

futurehead::pull_info::pull_info (futurehead::hash_or_account const & account_or_head_a, futurehead::block_hash const & head_a, futurehead::block_hash const & end_a, uint64_t bootstrap_id_a, count_t count_a, unsigned retry_limit_a) :
account_or_head (account_or_head_a),
head (head_a),
head_original (head_a),
end (end_a),
count (count_a),
retry_limit (retry_limit_a),
bootstrap_id (bootstrap_id_a)
{
}

futurehead::bulk_pull_client::bulk_pull_client (std::shared_ptr<futurehead::bootstrap_client> connection_a, std::shared_ptr<futurehead::bootstrap_attempt> attempt_a, futurehead::pull_info const & pull_a) :
connection (connection_a),
attempt (attempt_a),
known_account (0),
pull (pull_a),
pull_blocks (0),
unexpected_count (0)
{
	attempt->condition.notify_all ();
}

futurehead::bulk_pull_client::~bulk_pull_client ()
{
	// If received end block is not expected end block
	if (expected != pull.end)
	{
		pull.head = expected;
		if (attempt->mode != futurehead::bootstrap_mode::legacy)
		{
			pull.account_or_head = expected;
		}
		pull.processed += pull_blocks - unexpected_count;
		connection->node->bootstrap_initiator.connections->requeue_pull (pull, network_error);
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			connection->node->logger.try_log (boost::str (boost::format ("Bulk pull end block is not expected %1% for account %2%") % pull.end.to_string () % pull.account_or_head.to_account ()));
		}
	}
	else
	{
		connection->node->bootstrap_initiator.cache.remove (pull);
	}
	attempt->pull_finished ();
}

void futurehead::bulk_pull_client::request ()
{
	debug_assert (!pull.head.is_zero () || pull.retry_limit != std::numeric_limits<unsigned>::max ());
	expected = pull.head;
	futurehead::bulk_pull req;
	if (pull.head == pull.head_original && pull.attempts % 4 < 3)
	{
		// Account for new pulls
		req.start = pull.account_or_head;
	}
	else
	{
		// Head for cached pulls or accounts with public key equal to existing block hash (25% of attempts)
		req.start = pull.head;
	}
	req.end = pull.end;
	req.count = pull.count;
	req.set_count_present (pull.count != 0);

	if (connection->node->config.logging.bulk_pull_logging ())
	{
		connection->node->logger.try_log (boost::str (boost::format ("Requesting account %1% from %2%. %3% accounts in queue") % pull.account_or_head.to_account () % connection->channel->to_string () % attempt->pulling));
	}
	else if (connection->node->config.logging.network_logging () && attempt->should_log ())
	{
		connection->node->logger.always_log (boost::str (boost::format ("%1% accounts in pull queue") % attempt->pulling));
	}
	auto this_l (shared_from_this ());
	connection->channel->send (
	req, [this_l](boost::system::error_code const & ec, size_t size_a) {
		if (!ec)
		{
			this_l->throttled_receive_block ();
		}
		else
		{
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				this_l->connection->node->logger.try_log (boost::str (boost::format ("Error sending bulk pull request to %1%: to %2%") % ec.message () % this_l->connection->channel->to_string ()));
			}
			this_l->connection->node->stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::bulk_pull_request_failure, futurehead::stat::dir::in);
		}
	},
	futurehead::buffer_drop_policy::no_limiter_drop);
}

void futurehead::bulk_pull_client::throttled_receive_block ()
{
	debug_assert (!network_error);
	if (!connection->node->block_processor.half_full () && !connection->node->block_processor.flushing)
	{
		receive_block ();
	}
	else
	{
		auto this_l (shared_from_this ());
		connection->node->alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (1), [this_l]() {
			if (!this_l->connection->pending_stop && !this_l->attempt->stopped)
			{
				this_l->throttled_receive_block ();
			}
		});
	}
}

void futurehead::bulk_pull_client::receive_block ()
{
	auto this_l (shared_from_this ());
	if (auto socket_l = connection->channel->socket.lock ())
	{
		socket_l->async_read (connection->receive_buffer, 1, [this_l](boost::system::error_code const & ec, size_t size_a) {
			if (!ec)
			{
				this_l->received_type ();
			}
			else
			{
				if (this_l->connection->node->config.logging.bulk_pull_logging ())
				{
					this_l->connection->node->logger.try_log (boost::str (boost::format ("Error receiving block type: %1%") % ec.message ()));
				}
				this_l->connection->node->stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::bulk_pull_receive_block_failure, futurehead::stat::dir::in);
				this_l->network_error = true;
			}
		});
	}
}

void futurehead::bulk_pull_client::received_type ()
{
	auto this_l (shared_from_this ());
	futurehead::block_type type (static_cast<futurehead::block_type> (connection->receive_buffer->data ()[0]));

	if (auto socket_l = connection->channel->socket.lock ())
	{
		switch (type)
		{
			case futurehead::block_type::send:
			{
				socket_l->async_read (connection->receive_buffer, futurehead::send_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
					this_l->received_block (ec, size_a, type);
				});
				break;
			}
			case futurehead::block_type::receive:
			{
				socket_l->async_read (connection->receive_buffer, futurehead::receive_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
					this_l->received_block (ec, size_a, type);
				});
				break;
			}
			case futurehead::block_type::open:
			{
				socket_l->async_read (connection->receive_buffer, futurehead::open_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
					this_l->received_block (ec, size_a, type);
				});
				break;
			}
			case futurehead::block_type::change:
			{
				socket_l->async_read (connection->receive_buffer, futurehead::change_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
					this_l->received_block (ec, size_a, type);
				});
				break;
			}
			case futurehead::block_type::state:
			{
				socket_l->async_read (connection->receive_buffer, futurehead::state_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
					this_l->received_block (ec, size_a, type);
				});
				break;
			}
			case futurehead::block_type::not_a_block:
			{
				// Avoid re-using slow peers, or peers that sent the wrong blocks.
				if (!connection->pending_stop && (expected == pull.end || (pull.count != 0 && pull.count == pull_blocks)))
				{
					connection->connections->pool_connection (connection);
				}
				break;
			}
			default:
			{
				if (connection->node->config.logging.network_packet_logging ())
				{
					connection->node->logger.try_log (boost::str (boost::format ("Unknown type received as block type: %1%") % static_cast<int> (type)));
				}
				break;
			}
		}
	}
}

void futurehead::bulk_pull_client::received_block (boost::system::error_code const & ec, size_t size_a, futurehead::block_type type_a)
{
	if (!ec)
	{
		futurehead::bufferstream stream (connection->receive_buffer->data (), size_a);
		std::shared_ptr<futurehead::block> block (futurehead::deserialize_block (stream, type_a));
		if (block != nullptr && !futurehead::work_validate_entry (*block))
		{
			auto hash (block->hash ());
			if (connection->node->config.logging.bulk_pull_logging ())
			{
				std::string block_l;
				block->serialize_json (block_l, connection->node->config.logging.single_line_record ());
				connection->node->logger.try_log (boost::str (boost::format ("Pulled block %1% %2%") % hash.to_string () % block_l));
			}
			// Is block expected?
			bool block_expected (false);
			// Unconfirmed head is used only for lazy destinations if legacy bootstrap is not available, see futurehead::bootstrap_attempt::lazy_destinations_increment (...)
			bool unconfirmed_account_head (connection->node->flags.disable_legacy_bootstrap && pull_blocks == 0 && pull.retry_limit != std::numeric_limits<unsigned>::max () && expected == pull.account_or_head && block->account () == pull.account_or_head);
			if (hash == expected || unconfirmed_account_head)
			{
				expected = block->previous ();
				block_expected = true;
			}
			else
			{
				unexpected_count++;
			}
			if (pull_blocks == 0 && block_expected)
			{
				known_account = block->account ();
			}
			if (connection->block_count++ == 0)
			{
				connection->set_start_time (std::chrono::steady_clock::now ());
			}
			attempt->total_blocks++;
			bool stop_pull (attempt->process_block (block, known_account, pull_blocks, pull.count, block_expected, pull.retry_limit));
			pull_blocks++;
			if (!stop_pull && !connection->hard_stop.load ())
			{
				/* Process block in lazy pull if not stopped
				Stop usual pull request with unexpected block & more than 16k blocks processed
				to prevent spam */
				if (attempt->mode != futurehead::bootstrap_mode::legacy || unexpected_count < 16384)
				{
					throttled_receive_block ();
				}
			}
			else if (stop_pull && block_expected)
			{
				connection->connections->pool_connection (connection);
			}
		}
		else
		{
			if (connection->node->config.logging.bulk_pull_logging ())
			{
				connection->node->logger.try_log ("Error deserializing block received from pull request");
			}
			connection->node->stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::bulk_pull_deserialize_receive_block, futurehead::stat::dir::in);
		}
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			connection->node->logger.try_log (boost::str (boost::format ("Error bulk receiving block: %1%") % ec.message ()));
		}
		connection->node->stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::bulk_pull_receive_block_failure, futurehead::stat::dir::in);
		network_error = true;
	}
}

futurehead::bulk_pull_account_client::bulk_pull_account_client (std::shared_ptr<futurehead::bootstrap_client> connection_a, std::shared_ptr<futurehead::bootstrap_attempt> attempt_a, futurehead::account const & account_a) :
connection (connection_a),
attempt (attempt_a),
account (account_a),
pull_blocks (0)
{
	attempt->condition.notify_all ();
}

futurehead::bulk_pull_account_client::~bulk_pull_account_client ()
{
	attempt->pull_finished ();
}

void futurehead::bulk_pull_account_client::request ()
{
	futurehead::bulk_pull_account req;
	req.account = account;
	req.minimum_amount = connection->node->config.receive_minimum;
	req.flags = futurehead::bulk_pull_account_flags::pending_hash_and_amount;
	if (connection->node->config.logging.bulk_pull_logging ())
	{
		connection->node->logger.try_log (boost::str (boost::format ("Requesting pending for account %1% from %2%. %3% accounts in queue") % req.account.to_account () % connection->channel->to_string () % attempt->wallet_size ()));
	}
	else if (connection->node->config.logging.network_logging () && attempt->should_log ())
	{
		connection->node->logger.always_log (boost::str (boost::format ("%1% accounts in pull queue") % attempt->wallet_size ()));
	}
	auto this_l (shared_from_this ());
	connection->channel->send (
	req, [this_l](boost::system::error_code const & ec, size_t size_a) {
		if (!ec)
		{
			this_l->receive_pending ();
		}
		else
		{
			this_l->attempt->requeue_pending (this_l->account);
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				this_l->connection->node->logger.try_log (boost::str (boost::format ("Error starting bulk pull request to %1%: to %2%") % ec.message () % this_l->connection->channel->to_string ()));
			}
			this_l->connection->node->stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::bulk_pull_error_starting_request, futurehead::stat::dir::in);
		}
	},
	futurehead::buffer_drop_policy::no_limiter_drop);
}

void futurehead::bulk_pull_account_client::receive_pending ()
{
	auto this_l (shared_from_this ());
	size_t size_l (sizeof (futurehead::uint256_union) + sizeof (futurehead::uint128_union));
	if (auto socket_l = connection->channel->socket.lock ())
	{
		socket_l->async_read (connection->receive_buffer, size_l, [this_l, size_l](boost::system::error_code const & ec, size_t size_a) {
			// An issue with asio is that sometimes, instead of reporting a bad file descriptor during disconnect,
			// we simply get a size of 0.
			if (size_a == size_l)
			{
				if (!ec)
				{
					futurehead::block_hash pending;
					futurehead::bufferstream frontier_stream (this_l->connection->receive_buffer->data (), sizeof (futurehead::uint256_union));
					auto error1 (futurehead::try_read (frontier_stream, pending));
					(void)error1;
					debug_assert (!error1);
					futurehead::amount balance;
					futurehead::bufferstream balance_stream (this_l->connection->receive_buffer->data () + sizeof (futurehead::uint256_union), sizeof (futurehead::uint128_union));
					auto error2 (futurehead::try_read (balance_stream, balance));
					(void)error2;
					debug_assert (!error2);
					if (this_l->pull_blocks == 0 || !pending.is_zero ())
					{
						if (this_l->pull_blocks == 0 || balance.number () >= this_l->connection->node->config.receive_minimum.number ())
						{
							this_l->pull_blocks++;
							{
								if (!pending.is_zero ())
								{
									if (!this_l->connection->node->ledger.block_exists (pending))
									{
										this_l->connection->node->bootstrap_initiator.bootstrap_lazy (pending, false, false);
									}
								}
							}
							this_l->receive_pending ();
						}
						else
						{
							this_l->attempt->requeue_pending (this_l->account);
						}
					}
					else
					{
						this_l->connection->connections->pool_connection (this_l->connection);
					}
				}
				else
				{
					this_l->attempt->requeue_pending (this_l->account);
					if (this_l->connection->node->config.logging.network_logging ())
					{
						this_l->connection->node->logger.try_log (boost::str (boost::format ("Error while receiving bulk pull account frontier %1%") % ec.message ()));
					}
				}
			}
			else
			{
				this_l->attempt->requeue_pending (this_l->account);
				if (this_l->connection->node->config.logging.network_message_logging ())
				{
					this_l->connection->node->logger.try_log (boost::str (boost::format ("Invalid size: expected %1%, got %2%") % size_l % size_a));
				}
			}
		});
	}
}

/**
 * Handle a request for the pull of all blocks associated with an account
 * The account is supplied as the "start" member, and the final block to
 * send is the "end" member.  The "start" member may also be a block
 * hash, in which case the that hash is used as the start of a chain
 * to send.  To determine if "start" is interpretted as an account or
 * hash, the ledger is checked to see if the block specified exists,
 * if not then it is interpretted as an account.
 *
 * Additionally, if "start" is specified as a block hash the range
 * is inclusive of that block hash, that is the range will be:
 * [start, end); In the case that a block hash is not specified the
 * range will be exclusive of the frontier for that account with
 * a range of (frontier, end)
 */
void futurehead::bulk_pull_server::set_current_end ()
{
	include_start = false;
	debug_assert (request != nullptr);
	auto transaction (connection->node->store.tx_begin_read ());
	if (!connection->node->store.block_exists (transaction, request->end))
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			connection->node->logger.try_log (boost::str (boost::format ("Bulk pull end block doesn't exist: %1%, sending everything") % request->end.to_string ()));
		}
		request->end.clear ();
	}

	if (connection->node->store.block_exists (transaction, request->start))
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			connection->node->logger.try_log (boost::str (boost::format ("Bulk pull request for block hash: %1%") % request->start.to_string ()));
		}

		current = request->start;
		include_start = true;
	}
	else
	{
		futurehead::account_info info;
		auto no_address (connection->node->store.account_get (transaction, request->start, info));
		if (no_address)
		{
			if (connection->node->config.logging.bulk_pull_logging ())
			{
				connection->node->logger.try_log (boost::str (boost::format ("Request for unknown account: %1%") % request->start.to_account ()));
			}
			current = request->end;
		}
		else
		{
			current = info.head;
			if (!request->end.is_zero ())
			{
				auto account (connection->node->ledger.account (transaction, request->end));
				if (account != request->start)
				{
					if (connection->node->config.logging.bulk_pull_logging ())
					{
						connection->node->logger.try_log (boost::str (boost::format ("Request for block that is not on account chain: %1% not on %2%") % request->end.to_string () % request->start.to_account ()));
					}
					current = request->end;
				}
			}
		}
	}

	sent_count = 0;
	if (request->is_count_present ())
	{
		max_count = request->count;
	}
	else
	{
		max_count = 0;
	}
}

void futurehead::bulk_pull_server::send_next ()
{
	auto block (get_next ());
	if (block != nullptr)
	{
		std::vector<uint8_t> send_buffer;
		{
			futurehead::vectorstream stream (send_buffer);
			futurehead::serialize_block (stream, *block);
		}
		auto this_l (shared_from_this ());
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			connection->node->logger.try_log (boost::str (boost::format ("Sending block: %1%") % block->hash ().to_string ()));
		}
		connection->socket->async_write (futurehead::shared_const_buffer (std::move (send_buffer)), [this_l](boost::system::error_code const & ec, size_t size_a) {
			this_l->sent_action (ec, size_a);
		});
	}
	else
	{
		send_finished ();
	}
}

std::shared_ptr<futurehead::block> futurehead::bulk_pull_server::get_next ()
{
	std::shared_ptr<futurehead::block> result;
	bool send_current = false, set_current_to_end = false;

	/*
	 * Determine if we should reply with a block
	 *
	 * If our cursor is on the final block, we should signal that we
	 * are done by returning a null result.
	 *
	 * Unless we are including the "start" member and this is the
	 * start member, then include it anyway.
	 */
	if (current != request->end)
	{
		send_current = true;
	}
	else if (current == request->end && include_start == true)
	{
		send_current = true;

		/*
		 * We also need to ensure that the next time
		 * are invoked that we return a null result
		 */
		set_current_to_end = true;
	}

	/*
	 * Account for how many blocks we have provided.  If this
	 * exceeds the requested maximum, return an empty object
	 * to signal the end of results
	 */
	if (max_count != 0 && sent_count >= max_count)
	{
		send_current = false;
	}

	if (send_current)
	{
		result = connection->node->block (current);
		if (result != nullptr && set_current_to_end == false)
		{
			auto previous (result->previous ());
			if (!previous.is_zero ())
			{
				current = previous;
			}
			else
			{
				current = request->end;
			}
		}
		else
		{
			current = request->end;
		}

		sent_count++;
	}

	/*
	 * Once we have processed "get_next()" once our cursor is no longer on
	 * the "start" member, so this flag is not relevant is always false.
	 */
	include_start = false;

	return result;
}

void futurehead::bulk_pull_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		send_next ();
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			connection->node->logger.try_log (boost::str (boost::format ("Unable to bulk send block: %1%") % ec.message ()));
		}
	}
}

void futurehead::bulk_pull_server::send_finished ()
{
	futurehead::shared_const_buffer send_buffer (static_cast<uint8_t> (futurehead::block_type::not_a_block));
	auto this_l (shared_from_this ());
	if (connection->node->config.logging.bulk_pull_logging ())
	{
		connection->node->logger.try_log ("Bulk sending finished");
	}
	connection->socket->async_write (send_buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->no_block_sent (ec, size_a);
	});
}

void futurehead::bulk_pull_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		debug_assert (size_a == 1);
		connection->finish_request ();
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			connection->node->logger.try_log ("Unable to send not-a-block");
		}
	}
}

futurehead::bulk_pull_server::bulk_pull_server (std::shared_ptr<futurehead::bootstrap_server> const & connection_a, std::unique_ptr<futurehead::bulk_pull> request_a) :
connection (connection_a),
request (std::move (request_a))
{
	set_current_end ();
}

/**
 * Bulk pull blocks related to an account
 */
void futurehead::bulk_pull_account_server::set_params ()
{
	debug_assert (request != nullptr);

	/*
	 * Parse the flags
	 */
	invalid_request = false;
	pending_include_address = false;
	pending_address_only = false;
	if (request->flags == futurehead::bulk_pull_account_flags::pending_address_only)
	{
		pending_address_only = true;
	}
	else if (request->flags == futurehead::bulk_pull_account_flags::pending_hash_amount_and_address)
	{
		/**
		 ** This is the same as "pending_hash_and_amount" but with the
		 ** sending address appended, for UI purposes mainly.
		 **/
		pending_include_address = true;
	}
	else if (request->flags == futurehead::bulk_pull_account_flags::pending_hash_and_amount)
	{
		/** The defaults are set above **/
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			connection->node->logger.try_log (boost::str (boost::format ("Invalid bulk_pull_account flags supplied %1%") % static_cast<uint8_t> (request->flags)));
		}

		invalid_request = true;

		return;
	}

	/*
	 * Initialize the current item from the requested account
	 */
	current_key.account = request->account;
	current_key.hash = 0;
}

void futurehead::bulk_pull_account_server::send_frontier ()
{
	/*
	 * This function is really the entry point into this class,
	 * so handle the invalid_request case by terminating the
	 * request without any response
	 */
	if (!invalid_request)
	{
		auto stream_transaction (connection->node->store.tx_begin_read ());

		// Get account balance and frontier block hash
		auto account_frontier_hash (connection->node->ledger.latest (stream_transaction, request->account));
		auto account_frontier_balance_int (connection->node->ledger.account_balance (stream_transaction, request->account));
		futurehead::uint128_union account_frontier_balance (account_frontier_balance_int);

		// Write the frontier block hash and balance into a buffer
		std::vector<uint8_t> send_buffer;
		{
			futurehead::vectorstream output_stream (send_buffer);
			write (output_stream, account_frontier_hash.bytes);
			write (output_stream, account_frontier_balance.bytes);
		}

		// Send the buffer to the requestor
		auto this_l (shared_from_this ());
		connection->socket->async_write (futurehead::shared_const_buffer (std::move (send_buffer)), [this_l](boost::system::error_code const & ec, size_t size_a) {
			this_l->sent_action (ec, size_a);
		});
	}
}

void futurehead::bulk_pull_account_server::send_next_block ()
{
	/*
	 * Get the next item from the queue, it is a tuple with the key (which
	 * contains the account and hash) and data (which contains the amount)
	 */
	auto block_data (get_next ());
	auto block_info_key (block_data.first.get ());
	auto block_info (block_data.second.get ());

	if (block_info_key != nullptr)
	{
		/*
		 * If we have a new item, emit it to the socket
		 */

		std::vector<uint8_t> send_buffer;
		if (pending_address_only)
		{
			futurehead::vectorstream output_stream (send_buffer);

			if (connection->node->config.logging.bulk_pull_logging ())
			{
				connection->node->logger.try_log (boost::str (boost::format ("Sending address: %1%") % block_info->source.to_string ()));
			}

			write (output_stream, block_info->source.bytes);
		}
		else
		{
			futurehead::vectorstream output_stream (send_buffer);

			if (connection->node->config.logging.bulk_pull_logging ())
			{
				connection->node->logger.try_log (boost::str (boost::format ("Sending block: %1%") % block_info_key->hash.to_string ()));
			}

			write (output_stream, block_info_key->hash.bytes);
			write (output_stream, block_info->amount.bytes);

			if (pending_include_address)
			{
				/**
				 ** Write the source address as well, if requested
				 **/
				write (output_stream, block_info->source.bytes);
			}
		}

		auto this_l (shared_from_this ());
		connection->socket->async_write (futurehead::shared_const_buffer (std::move (send_buffer)), [this_l](boost::system::error_code const & ec, size_t size_a) {
			this_l->sent_action (ec, size_a);
		});
	}
	else
	{
		/*
		 * Otherwise, finalize the connection
		 */
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			connection->node->logger.try_log (boost::str (boost::format ("Done sending blocks")));
		}

		send_finished ();
	}
}

std::pair<std::unique_ptr<futurehead::pending_key>, std::unique_ptr<futurehead::pending_info>> futurehead::bulk_pull_account_server::get_next ()
{
	std::pair<std::unique_ptr<futurehead::pending_key>, std::unique_ptr<futurehead::pending_info>> result;

	while (true)
	{
		/*
		 * For each iteration of this loop, establish and then
		 * destroy a database transaction, to avoid locking the
		 * database for a prolonged period.
		 */
		auto stream_transaction (connection->node->store.tx_begin_read ());
		auto stream (connection->node->store.pending_begin (stream_transaction, current_key));

		if (stream == futurehead::store_iterator<futurehead::pending_key, futurehead::pending_info> (nullptr))
		{
			break;
		}

		futurehead::pending_key key (stream->first);
		futurehead::pending_info info (stream->second);

		/*
		 * Get the key for the next value, to use in the next call or iteration
		 */
		current_key.account = key.account;
		current_key.hash = key.hash.number () + 1;

		/*
		 * Finish up if the response is for a different account
		 */
		if (key.account != request->account)
		{
			break;
		}

		/*
		 * Skip entries where the amount is less than the requested
		 * minimum
		 */
		if (info.amount < request->minimum_amount)
		{
			continue;
		}

		/*
		 * If the pending_address_only flag is set, de-duplicate the
		 * responses.  The responses are the address of the sender,
		 * so they are are part of the pending table's information
		 * and not key, so we have to de-duplicate them manually.
		 */
		if (pending_address_only)
		{
			if (!deduplication.insert (info.source).second)
			{
				/*
				 * If the deduplication map gets too
				 * large, clear it out.  This may
				 * result in some duplicates getting
				 * sent to the client, but we do not
				 * want to commit too much memory
				 */
				if (deduplication.size () > 4096)
				{
					deduplication.clear ();
				}
				continue;
			}
		}

		result.first = std::make_unique<futurehead::pending_key> (key);
		result.second = std::make_unique<futurehead::pending_info> (info);

		break;
	}

	return result;
}

void futurehead::bulk_pull_account_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		send_next_block ();
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			connection->node->logger.try_log (boost::str (boost::format ("Unable to bulk send block: %1%") % ec.message ()));
		}
	}
}

void futurehead::bulk_pull_account_server::send_finished ()
{
	/*
	 * The "bulk_pull_account" final sequence is a final block of all
	 * zeros.  If we are sending only account public keys (with the
	 * "pending_address_only" flag) then it will be 256-bits of zeros,
	 * otherwise it will be either 384-bits of zeros (if the
	 * "pending_include_address" flag is not set) or 640-bits of zeros
	 * (if that flag is set).
	 */
	std::vector<uint8_t> send_buffer;
	{
		futurehead::vectorstream output_stream (send_buffer);
		futurehead::uint256_union account_zero (0);
		futurehead::uint128_union balance_zero (0);

		write (output_stream, account_zero.bytes);

		if (!pending_address_only)
		{
			write (output_stream, balance_zero.bytes);
			if (pending_include_address)
			{
				write (output_stream, account_zero.bytes);
			}
		}
	}

	auto this_l (shared_from_this ());

	if (connection->node->config.logging.bulk_pull_logging ())
	{
		connection->node->logger.try_log ("Bulk sending for an account finished");
	}

	connection->socket->async_write (futurehead::shared_const_buffer (std::move (send_buffer)), [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->complete (ec, size_a);
	});
}

void futurehead::bulk_pull_account_server::complete (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		if (pending_address_only)
		{
			debug_assert (size_a == 32);
		}
		else
		{
			if (pending_include_address)
			{
				debug_assert (size_a == 80);
			}
			else
			{
				debug_assert (size_a == 48);
			}
		}

		connection->finish_request ();
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			connection->node->logger.try_log ("Unable to pending-as-zero");
		}
	}
}

futurehead::bulk_pull_account_server::bulk_pull_account_server (std::shared_ptr<futurehead::bootstrap_server> const & connection_a, std::unique_ptr<futurehead::bulk_pull_account> request_a) :
connection (connection_a),
request (std::move (request_a)),
current_key (0, 0)
{
	/*
	 * Setup the streaming response for the first call to "send_frontier" and  "send_next_block"
	 */
	set_params ();
}
