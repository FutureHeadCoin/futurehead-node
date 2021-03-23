#include <futurehead/node/ipc/action_handler.hpp>
#include <futurehead/node/ipc/flatbuffers_handler.hpp>
#include <futurehead/node/ipc/flatbuffers_util.hpp>
#include <futurehead/node/ipc/ipc_broker.hpp>
#include <futurehead/node/ipc/ipc_server.hpp>
#include <futurehead/node/node.hpp>

futurehead::ipc::broker::broker (futurehead::node & node_a) :
node (node_a)
{
}

std::shared_ptr<flatbuffers::Parser> futurehead::ipc::subscriber::get_parser (futurehead::ipc::ipc_config const & ipc_config_a)
{
	if (!parser)
	{
		parser = futurehead::ipc::flatbuffers_handler::make_flatbuffers_parser (ipc_config_a);
	}
	return parser;
}

void futurehead::ipc::broker::start ()
{
	node.observers.blocks.add ([this](futurehead::election_status const & status_a, futurehead::account const & account_a, futurehead::amount const & amount_a, bool is_state_send_a) {
		debug_assert (status_a.type != futurehead::election_status_type::ongoing);

		try
		{
			// The subscriber(s) may be gone after the count check, but the only consequence
			// is that broadcast is called only to not find any live sessions.
			if (confirmation_subscriber_count () > 0)
			{
				auto confirmation (std::make_shared<futureheadapi::EventConfirmationT> ());

				confirmation->account = account_a.to_account ();
				confirmation->amount = amount_a.to_string_dec ();
				switch (status_a.type)
				{
					case futurehead::election_status_type::active_confirmed_quorum:
						confirmation->confirmation_type = futureheadapi::TopicConfirmationType::TopicConfirmationType_active_quorum;
						break;
					case futurehead::election_status_type::active_confirmation_height:
						confirmation->confirmation_type = futureheadapi::TopicConfirmationType::TopicConfirmationType_active_confirmation_height;
						break;
					case futurehead::election_status_type::inactive_confirmation_height:
						confirmation->confirmation_type = futureheadapi::TopicConfirmationType::TopicConfirmationType_inactive;
						break;
					default:
						debug_assert (false);
						break;
				};
				confirmation->confirmation_type = futureheadapi::TopicConfirmationType::TopicConfirmationType_active_quorum;
				confirmation->block = futurehead::ipc::flatbuffers_builder::block_to_union (*status_a.winner, amount_a, is_state_send_a);
				confirmation->election_info = std::make_unique<futureheadapi::ElectionInfoT> ();
				confirmation->election_info->duration = status_a.election_duration.count ();
				confirmation->election_info->time = status_a.election_end.count ();
				confirmation->election_info->tally = status_a.tally.to_string_dec ();
				confirmation->election_info->block_count = status_a.block_count;
				confirmation->election_info->voter_count = status_a.voter_count;
				confirmation->election_info->request_count = status_a.confirmation_request_count;

				broadcast (confirmation);
			}
		}
		catch (futurehead::error const & err)
		{
			this->node.logger.always_log ("IPC: could not broadcast message: ", err.get_message ());
		}
	});
}

template <typename COLL, typename TOPIC_TYPE>
void subscribe_or_unsubscribe (futurehead::logger_mt & logger, COLL & subscriber_collection, std::weak_ptr<futurehead::ipc::subscriber> const & subscriber_a, TOPIC_TYPE topic_a)
{
	// Evict subscribers from dead sessions. Also remove current subscriber if unsubscribing.
	subscriber_collection.erase (std::remove_if (subscriber_collection.begin (), subscriber_collection.end (),
	                             [& logger = logger, topic_a, subscriber_a](auto & sub) {
		                             bool remove = false;
		                             auto subscriber_l = sub.subscriber.lock ();
		                             if (subscriber_l)
		                             {
			                             if (auto calling_subscriber_l = subscriber_a.lock ())
			                             {
				                             remove = topic_a->unsubscribe && subscriber_l->get_id () == calling_subscriber_l->get_id ();
				                             if (remove)
				                             {
					                             logger.always_log ("IPC: unsubscription from subscriber #", calling_subscriber_l->get_id ());
				                             }
			                             }
		                             }
		                             else
		                             {
			                             remove = true;
		                             }
		                             return remove;
	                             }),
	subscriber_collection.end ());

	if (!topic_a->unsubscribe)
	{
		subscriber_collection.emplace_back (subscriber_a, topic_a);
	}
}

void futurehead::ipc::broker::subscribe (std::weak_ptr<futurehead::ipc::subscriber> const & subscriber_a, std::shared_ptr<futureheadapi::TopicConfirmationT> const & confirmation_a)
{
	auto subscribers = confirmation_subscribers.lock ();
	subscribe_or_unsubscribe (node.logger, subscribers.get (), subscriber_a, confirmation_a);
}

void futurehead::ipc::broker::broadcast (std::shared_ptr<futureheadapi::EventConfirmationT> const & confirmation_a)
{
	using Filter = futureheadapi::TopicConfirmationTypeFilter;
	decltype (confirmation_a->election_info) election_info;
	futureheadapi::BlockUnion block;
	auto itr (confirmation_subscribers->begin ());
	while (itr != confirmation_subscribers->end ())
	{
		if (auto subscriber_l = itr->subscriber.lock ())
		{
			auto should_filter = [this, &itr, confirmation_a]() {
				debug_assert (itr->topic->options != nullptr);
				auto conf_filter (itr->topic->options->confirmation_type_filter);

				bool should_filter_conf_type_l (true);
				bool all_filter = conf_filter == Filter::TopicConfirmationTypeFilter_all;
				bool inactive_filter = conf_filter == Filter::TopicConfirmationTypeFilter_inactive;
				bool active_filter = conf_filter == Filter::TopicConfirmationTypeFilter_active || conf_filter == Filter::TopicConfirmationTypeFilter_active_quorum || conf_filter == Filter::TopicConfirmationTypeFilter_active_confirmation_height;

				if ((confirmation_a->confirmation_type == futureheadapi::TopicConfirmationType::TopicConfirmationType_active_quorum || confirmation_a->confirmation_type == futureheadapi::TopicConfirmationType::TopicConfirmationType_active_confirmation_height) && (all_filter || active_filter))
				{
					should_filter_conf_type_l = false;
				}
				else if (confirmation_a->confirmation_type == futureheadapi::TopicConfirmationType::TopicConfirmationType_inactive && (all_filter || inactive_filter))
				{
					should_filter_conf_type_l = false;
				}

				bool should_filter_account_l (itr->topic->options->all_local_accounts || !itr->topic->options->accounts.empty ());
				auto state (confirmation_a->block.AsBlockState ());
				if (state && !should_filter_conf_type_l)
				{
					if (itr->topic->options->all_local_accounts)
					{
						auto transaction_l (this->node.wallets.tx_begin_read ());
						futurehead::account source_l (0), destination_l (0);
						auto decode_source_ok_l (!source_l.decode_account (state->account));
						auto decode_destination_ok_l (!destination_l.decode_account (state->link_as_account));
						(void)decode_source_ok_l;
						(void)decode_destination_ok_l;
						debug_assert (decode_source_ok_l && decode_destination_ok_l);
						if (this->node.wallets.exists (transaction_l, source_l) || this->node.wallets.exists (transaction_l, destination_l))
						{
							should_filter_account_l = false;
						}
					}

					if (std::find (itr->topic->options->accounts.begin (), itr->topic->options->accounts.end (), state->account) != itr->topic->options->accounts.end () || std::find (itr->topic->options->accounts.begin (), itr->topic->options->accounts.end (), state->link_as_account) != itr->topic->options->accounts.end ())
					{
						should_filter_account_l = false;
					}
				}

				return should_filter_conf_type_l || should_filter_account_l;
			};
			// Apply any filters
			auto & options (itr->topic->options);
			if (options)
			{
				if (!options->include_election_info)
				{
					election_info = std::move (confirmation_a->election_info);
					confirmation_a->election_info = nullptr;
				}
				if (!options->include_block)
				{
					block = confirmation_a->block;
					confirmation_a->block.Reset ();
				}
			}
			if (!options || !should_filter ())
			{
				auto fb (futurehead::ipc::flatbuffer_producer::make_buffer (*confirmation_a));

				if (subscriber_l->get_active_encoding () == futurehead::ipc::payload_encoding::flatbuffers_json)
				{
					auto parser (subscriber_l->get_parser (node.config.ipc_config));

					// Convert response to JSON
					auto json (std::make_shared<std::string> ());
					if (!flatbuffers::GenerateText (*parser, fb->GetBufferPointer (), json.get ()))
					{
						throw futurehead::error ("Couldn't serialize response to JSON");
					}

					subscriber_l->async_send_message (reinterpret_cast<uint8_t const *> (json->data ()), json->size (), [json](const futurehead::error & err) {});
				}
				else
				{
					subscriber_l->async_send_message (fb->GetBufferPointer (), fb->GetSize (), [fb](const futurehead::error & err) {});
				}
			}

			// Restore full object, the next subscriber may request it
			if (election_info)
			{
				confirmation_a->election_info = std::move (election_info);
			}
			if (block.type != futureheadapi::Block::Block_NONE)
			{
				confirmation_a->block = block;
			}

			++itr;
		}
		else
		{
			itr = confirmation_subscribers->erase (itr);
		}
	}
}

size_t futurehead::ipc::broker::confirmation_subscriber_count () const
{
	return confirmation_subscribers->size ();
}

void futurehead::ipc::broker::service_register (std::string const & service_name_a, std::weak_ptr<futurehead::ipc::subscriber> const & subscriber_a)
{
	if (auto subscriber_l = subscriber_a.lock ())
	{
		subscriber_l->set_service_name (service_name_a);
	}
}

void futurehead::ipc::broker::service_stop (std::string const & service_name_a)
{
	auto subscribers = service_stop_subscribers.lock ();
	for (auto & subcription : subscribers.get ())
	{
		if (auto subscriber_l = subcription.subscriber.lock ())
		{
			if (subscriber_l->get_service_name () == service_name_a)
			{
				futureheadapi::EventServiceStopT event_stop;
				auto fb (futurehead::ipc::flatbuffer_producer::make_buffer (event_stop));
				subscriber_l->async_send_message (fb->GetBufferPointer (), fb->GetSize (), [fb](const futurehead::error & err) {});

				break;
			}
		}
	}
}

void futurehead::ipc::broker::subscribe (std::weak_ptr<futurehead::ipc::subscriber> const & subscriber_a, std::shared_ptr<futureheadapi::TopicServiceStopT> const & service_stop_a)
{
	auto subscribers = service_stop_subscribers.lock ();
	subscribe_or_unsubscribe (node.logger, subscribers.get (), subscriber_a, service_stop_a);
}
