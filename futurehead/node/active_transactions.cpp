#include <futurehead/lib/threading.hpp>
#include <futurehead/node/active_transactions.hpp>
#include <futurehead/node/confirmation_height_processor.hpp>
#include <futurehead/node/confirmation_solicitor.hpp>
#include <futurehead/node/election.hpp>
#include <futurehead/node/node.hpp>
#include <futurehead/node/repcrawler.hpp>
#include <futurehead/secure/blockstore.hpp>

#include <boost/format.hpp>
#include <boost/variant/get.hpp>

#include <numeric>

using namespace std::chrono;

futurehead::active_transactions::active_transactions (futurehead::node & node_a, futurehead::confirmation_height_processor & confirmation_height_processor_a) :
recently_dropped (node_a.stats),
confirmation_height_processor (confirmation_height_processor_a),
node (node_a),
multipliers_cb (20, 1.),
trended_active_multiplier (1.0),
generator (node_a.config, node_a.ledger, node_a.wallets, node_a.vote_processor, node_a.votes_cache, node_a.network),
check_all_elections_period (node_a.network_params.network.is_test_network () ? 10ms : 5s),
election_time_to_live (node_a.network_params.network.is_test_network () ? 0s : 2s),
prioritized_cutoff (std::max<size_t> (1, node_a.config.active_elections_size / 10)),
thread ([this]() {
	futurehead::thread_role::set (futurehead::thread_role::name::request_loop);
	request_loop ();
})
{
	// Register a callback which will get called after a block is cemented
	confirmation_height_processor.add_cemented_observer ([this](std::shared_ptr<futurehead::block> callback_block_a) {
		this->block_cemented_callback (callback_block_a);
	});

	// Register a callback which will get called if a block is already cemented
	confirmation_height_processor.add_block_already_cemented_observer ([this](futurehead::block_hash const & hash_a) {
		this->block_already_cemented_callback (hash_a);
	});

	futurehead::unique_lock<std::mutex> lock (mutex);
	condition.wait (lock, [& started = started] { return started; });
}

futurehead::active_transactions::~active_transactions ()
{
	stop ();
}

void futurehead::active_transactions::confirm_prioritized_frontiers (futurehead::transaction const & transaction_a)
{
	// Limit maximum count of elections to start
	auto rep_counts (node.wallets.reps ());
	bool representative (node.config.enable_voting && rep_counts.voting > 0);
	bool half_princpal_representative (representative && rep_counts.half_principal > 0);
	/* Check less frequently for regular nodes in auto mode */
	bool agressive_mode (half_princpal_representative || node.config.frontiers_confirmation == futurehead::frontiers_confirmation_mode::always);
	auto is_test_network = node.network_params.network.is_test_network ();
	auto roots_size = size ();
	auto check_time_exceeded = std::chrono::steady_clock::now () >= next_frontier_check;
	auto max_elections = 1000ull;
	auto low_active_elections = roots_size < max_elections;
	bool wallets_check_required = (!skip_wallets || !priority_wallet_cementable_frontiers.empty ()) && !agressive_mode;
	// Minimise dropping real-time transactions, set the number of frontiers added to a factor of the maximum number of possible active elections
	auto max_active = node.config.active_elections_size / 20;
	if (roots_size <= max_active && (check_time_exceeded || wallets_check_required || (!is_test_network && low_active_elections && agressive_mode)))
	{
		// When the number of active elections is low increase max number of elections for setting confirmation height.
		if (max_active > roots_size + max_elections)
		{
			max_elections = max_active - roots_size;
		}

		size_t elections_count (0);
		futurehead::unique_lock<std::mutex> lk (mutex);
		auto start_elections_for_prioritized_frontiers = [&transaction_a, &elections_count, max_elections, &lk, this](prioritize_num_uncemented & cementable_frontiers) {
			while (!cementable_frontiers.empty () && !this->stopped && elections_count < max_elections)
			{
				auto cementable_account_front_it = cementable_frontiers.get<tag_uncemented> ().begin ();
				auto cementable_account = *cementable_account_front_it;
				cementable_frontiers.get<tag_uncemented> ().erase (cementable_account_front_it);
				lk.unlock ();
				futurehead::account_info info;
				auto error = this->node.store.account_get (transaction_a, cementable_account.account, info);
				if (!error)
				{
					if (!this->confirmation_height_processor.is_processing_block (info.head))
					{
						futurehead::confirmation_height_info confirmation_height_info;
						error = this->node.store.confirmation_height_get (transaction_a, cementable_account.account, confirmation_height_info);
						release_assert (!error);

						if (info.block_count > confirmation_height_info.height)
						{
							auto block (this->node.store.block_get (transaction_a, info.head));
							auto previous_balance (this->node.ledger.balance (transaction_a, block->previous ()));
							auto insert_result = this->insert (block, previous_balance);
							if (insert_result.inserted)
							{
								insert_result.election->transition_active ();
								++elections_count;
							}
						}
					}
				}
				lk.lock ();
			}
		};
		start_elections_for_prioritized_frontiers (priority_cementable_frontiers);
		start_elections_for_prioritized_frontiers (priority_wallet_cementable_frontiers);

		auto request_interval (std::chrono::milliseconds (node.network_params.network.request_interval_ms));
		auto rel_time_next_frontier_check = request_interval * (agressive_mode ? 20 : 60);
		// Decrease check time for test network
		int test_network_factor = is_test_network ? 1000 : 1;

		next_frontier_check = steady_clock::now () + (rel_time_next_frontier_check / test_network_factor);
	}
}

void futurehead::active_transactions::block_cemented_callback (std::shared_ptr<futurehead::block> const & block_a)
{
	auto transaction = node.store.tx_begin_read ();

	boost::optional<futurehead::election_status_type> election_status_type;
	if (!confirmation_height_processor.is_processing_block (block_a->hash ()))
	{
		election_status_type = confirm_block (transaction, block_a);
	}
	else
	{
		// This block was explicitly added to the confirmation height_processor
		election_status_type = futurehead::election_status_type::active_confirmed_quorum;
	}

	if (election_status_type.is_initialized ())
	{
		if (election_status_type == futurehead::election_status_type::inactive_confirmation_height)
		{
			futurehead::account account (0);
			futurehead::uint128_t amount (0);
			bool is_state_send (false);
			futurehead::account pending_account (0);
			node.process_confirmed_data (transaction, block_a, block_a->hash (), account, amount, is_state_send, pending_account);
			node.observers.blocks.notify (futurehead::election_status{ block_a, 0, std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()), std::chrono::duration_values<std::chrono::milliseconds>::zero (), 0, 1, 0, futurehead::election_status_type::inactive_confirmation_height }, account, amount, is_state_send);
		}
		else
		{
			auto hash (block_a->hash ());
			futurehead::unique_lock<std::mutex> election_winners_lk (election_winner_details_mutex);
			auto existing (election_winner_details.find (hash));
			if (existing != election_winner_details.end ())
			{
				auto election = existing->second;
				election_winner_details.erase (hash);
				election_winners_lk.unlock ();
				futurehead::unique_lock<std::mutex> lk (mutex);
				if (election->confirmed () && election->status.winner->hash () == hash)
				{
					add_recently_cemented (election->status);
					lk.unlock ();
					node.receive_confirmed (transaction, block_a, hash);
					futurehead::account account (0);
					futurehead::uint128_t amount (0);
					bool is_state_send (false);
					futurehead::account pending_account (0);
					node.process_confirmed_data (transaction, block_a, hash, account, amount, is_state_send, pending_account);
					lk.lock ();
					election->status.type = *election_status_type;
					election->status.confirmation_request_count = election->confirmation_request_count;
					auto status (election->status);
					lk.unlock ();
					node.observers.blocks.notify (status, account, amount, is_state_send);
					if (amount > 0)
					{
						node.observers.account_balance.notify (account, false);
						if (!pending_account.is_zero ())
						{
							node.observers.account_balance.notify (pending_account, true);
						}
					}
				}
			}
		}

		// Start or vote for the next unconfirmed block in this account
		auto const & account (!block_a->account ().is_zero () ? block_a->account () : block_a->sideband ().account);
		debug_assert (!account.is_zero ());
		activate (account);

		// Start or vote for the next unconfirmed block in the destination account
		auto const & destination (node.ledger.block_destination (transaction, *block_a));
		if (!destination.is_zero ())
		{
			activate (destination);
		}
	}
}

void futurehead::active_transactions::add_election_winner_details (futurehead::block_hash const & hash_a, std::shared_ptr<futurehead::election> const & election_a)
{
	futurehead::lock_guard<std::mutex> guard (election_winner_details_mutex);
	election_winner_details.emplace (hash_a, election_a);
}

void futurehead::active_transactions::remove_election_winner_details (futurehead::block_hash const & hash_a)
{
	futurehead::lock_guard<std::mutex> guard (election_winner_details_mutex);
	election_winner_details.erase (hash_a);
}

void futurehead::active_transactions::block_already_cemented_callback (futurehead::block_hash const & hash_a)
{
	// Depending on timing there is a situation where the election_winner_details is not reset.
	// This can happen when a block wins an election, and the block is confirmed + observer
	// called before the block hash gets added to election_winner_details. If the block is confirmed
	// callbacks have already been done, so we can safely just remove it.
	remove_election_winner_details (hash_a);
}

void futurehead::active_transactions::request_confirm (futurehead::unique_lock<std::mutex> & lock_a)
{
	debug_assert (!mutex.try_lock ());

	// Only representatives ready to receive batched confirm_req
	futurehead::confirmation_solicitor solicitor (node.network, node.network_params.network);
	solicitor.prepare (node.rep_crawler.principal_representatives (std::numeric_limits<size_t>::max ()));

	futurehead::vote_generator_session generator_session (generator);
	auto & sorted_roots_l (roots.get<tag_difficulty> ());
	auto const election_ttl_cutoff_l (std::chrono::steady_clock::now () - election_time_to_live);
	bool const check_all_elections_l (std::chrono::steady_clock::now () - last_check_all_elections > check_all_elections_period);
	size_t const this_loop_target_l (check_all_elections_l ? sorted_roots_l.size () : prioritized_cutoff);
	size_t unconfirmed_count_l (0);
	futurehead::timer<std::chrono::milliseconds> elapsed (futurehead::timer_state::started);

	/*
	 * Loop through active elections in descending order of proof-of-work difficulty, requesting confirmation
	 *
	 * Only up to a certain amount of elections are queued for confirmation request and block rebroadcasting. The remaining elections can still be confirmed if votes arrive
	 * Elections extending the soft config.active_elections_size limit are flushed after a certain time-to-live cutoff
	 * Flushed elections are later re-activated via frontier confirmation
	 */
	for (auto i = sorted_roots_l.begin (), n = sorted_roots_l.end (); i != n && unconfirmed_count_l < this_loop_target_l;)
	{
		auto & election_l (i->election);
		bool const confirmed_l (election_l->confirmed ());

		if (!election_l->prioritized () && unconfirmed_count_l < prioritized_cutoff)
		{
			election_l->prioritize_election (generator_session);
		}

		unconfirmed_count_l += !confirmed_l;
		bool const overflow_l (unconfirmed_count_l > node.config.active_elections_size && election_l->election_start < election_ttl_cutoff_l && !node.wallets.watcher->is_watched (i->root));
		if (overflow_l || election_l->transition_time (solicitor))
		{
			election_l->cleanup ();
			i = sorted_roots_l.erase (i);
		}
		else
		{
			++i;
		}
	}
	lock_a.unlock ();
	solicitor.flush ();
	generator_session.flush ();
	lock_a.lock ();
	activate_dependencies (lock_a);

	// This is updated after the loop to ensure slow machines don't do the full check often
	if (check_all_elections_l)
	{
		last_check_all_elections = std::chrono::steady_clock::now ();
		if (node.config.logging.timing_logging () && this_loop_target_l > prioritized_cutoff)
		{
			node.logger.try_log (boost::str (boost::format ("Processed %1% elections (%2% were already confirmed) in %3% %4%") % this_loop_target_l % (this_loop_target_l - unconfirmed_count_l) % elapsed.value ().count () % elapsed.unit ()));
		}
	}
}

void futurehead::active_transactions::frontiers_confirmation (futurehead::unique_lock<std::mutex> & lock_a)
{
	/*
 	 * Confirm frontiers when there aren't many confirmations already pending and node finished initial bootstrap
  	 */
	auto pending_confirmation_height_size (confirmation_height_processor.awaiting_processing_size ());
	auto bootstrap_weight_reached (node.ledger.cache.block_count >= node.ledger.bootstrap_weight_max_blocks);
	auto disabled_confirmation_mode = (node.config.frontiers_confirmation == futurehead::frontiers_confirmation_mode::disabled);
	auto conf_height_capacity_reached = pending_confirmation_height_size > confirmed_frontiers_max_pending_size;
	auto all_cemented = node.ledger.cache.block_count == node.ledger.cache.cemented_count;
	if (!disabled_confirmation_mode && bootstrap_weight_reached && !conf_height_capacity_reached && !all_cemented)
	{
		// Spend some time prioritizing accounts with the most uncemented blocks to reduce voting traffic
		auto request_interval = std::chrono::milliseconds (node.network_params.network.request_interval_ms);
		// Spend longer searching ledger accounts when there is a low amount of elections going on
		auto low_active = roots.size () < 1000;
		auto time_spent_prioritizing_ledger_accounts = request_interval / (low_active ? 20 : 100);
		auto time_spent_prioritizing_wallet_accounts = request_interval / 250;
		lock_a.unlock ();
		auto transaction = node.store.tx_begin_read ();
		prioritize_frontiers_for_confirmation (transaction, node.network_params.network.is_test_network () ? std::chrono::milliseconds (50) : time_spent_prioritizing_ledger_accounts, time_spent_prioritizing_wallet_accounts);
		confirm_prioritized_frontiers (transaction);
		lock_a.lock ();
	}
}

void futurehead::active_transactions::activate_dependencies (futurehead::unique_lock<std::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());
	decltype (pending_dependencies) pending_l;
	pending_l.swap (pending_dependencies);
	lock_a.unlock ();

	auto first_unconfirmed = [this](futurehead::transaction const & transaction_a, futurehead::account const & account_a, futurehead::block_hash const & confirmed_frontier_a) {
		if (!confirmed_frontier_a.is_zero ())
		{
			return this->node.store.block_successor (transaction_a, confirmed_frontier_a);
		}
		else
		{
			futurehead::account_info account_info_l;
			auto error = node.store.account_get (transaction_a, account_a, account_info_l);
			(void)error;
			debug_assert (!error);
			return account_info_l.open_block;
		}
	};

	// Store blocks to activate when the lock is re-acquired, adding the hash of the original election as a dependency
	std::vector<std::pair<std::shared_ptr<futurehead::block>, futurehead::block_hash>> activate_l;
	{
		auto transaction = node.store.tx_begin_read ();
		for (auto const & entry_l : pending_l)
		{
			auto const & hash_l (entry_l.first);
			auto const block_l (node.store.block_get (transaction, hash_l));
			if (block_l)
			{
				auto const height_l (entry_l.second);
				auto const previous_hash_l (block_l->previous ());
				if (!previous_hash_l.is_zero ())
				{
					/* Insert first unconfirmed block (pessimistic) and bisect the chain (likelihood) */
					auto const account (node.store.block_account_calculated (*block_l));
					futurehead::confirmation_height_info conf_info_l;
					if (!node.store.confirmation_height_get (transaction, account, conf_info_l))
					{
						if (height_l > conf_info_l.height + 1)
						{
							auto const successor_hash_l = first_unconfirmed (transaction, account, conf_info_l.frontier);
							if (!confirmation_height_processor.is_processing_block (successor_hash_l))
							{
								auto const successor_l = node.store.block_get (transaction, successor_hash_l);
								debug_assert (successor_l != nullptr);
								if (successor_l != nullptr)
								{
									activate_l.emplace_back (successor_l, hash_l);
								}
							}
						}
						if (height_l > conf_info_l.height + 2)
						{
							auto const jumps_l = std::min<uint64_t> (128, (height_l - conf_info_l.height) / 2);
							auto const backtracked_l (node.ledger.backtrack (transaction, block_l, jumps_l));
							if (backtracked_l != nullptr)
							{
								activate_l.emplace_back (backtracked_l, hash_l);
							}
						}
					}
				}
				/* If previous block not existing/not commited yet, block_source can cause segfault for state blocks
				So source check can be done only if previous != nullptr or previous is 0 (open account) */
				if (previous_hash_l.is_zero () || node.ledger.block_exists (previous_hash_l))
				{
					auto source_hash_l (node.ledger.block_source (transaction, *block_l));
					if (!source_hash_l.is_zero () && source_hash_l != previous_hash_l && blocks.find (source_hash_l) == blocks.end ())
					{
						auto source_l (node.store.block_get (transaction, source_hash_l));
						if (source_l != nullptr && !node.block_confirmed_or_being_confirmed (transaction, source_hash_l))
						{
							activate_l.emplace_back (source_l, hash_l);
						}
					}
				}
			}
		}
	}
	lock_a.lock ();
	for (auto const & entry_l : activate_l)
	{
		auto election = insert_impl (entry_l.first);
		if (election.inserted)
		{
			election.election->transition_active ();
			election.election->dependent_blocks.insert (entry_l.second);
		}
	}
}

void futurehead::active_transactions::request_loop ()
{
	futurehead::unique_lock<std::mutex> lock (mutex);
	started = true;
	lock.unlock ();
	condition.notify_all ();

	// The wallets and active_transactions objects are mutually dependent, so we need a fully
	// constructed node before proceeding.
	this->node.node_initialized_latch.wait ();

	lock.lock ();

	while (!stopped && !node.flags.disable_request_loop)
	{
		// Account for the time spent in request_confirm by defining the wakeup point beforehand
		const auto wakeup_l (std::chrono::steady_clock::now () + std::chrono::milliseconds (node.network_params.network.request_interval_ms));

		update_adjusted_multiplier ();
		// frontiers_confirmation should be above update_active_multiplier to ensure new sorted roots are updated
		frontiers_confirmation (lock);
		update_active_multiplier (lock);
		request_confirm (lock);

		// Sleep until all broadcasts are done, plus the remaining loop time
		if (!stopped)
		{
			condition.wait_until (lock, wakeup_l, [&wakeup_l, &stopped = stopped] { return stopped || std::chrono::steady_clock::now () >= wakeup_l; });
		}
	}
}

void futurehead::active_transactions::prioritize_account_for_confirmation (futurehead::active_transactions::prioritize_num_uncemented & cementable_frontiers_a, size_t & cementable_frontiers_size_a, futurehead::account const & account_a, futurehead::account_info const & info_a, uint64_t confirmation_height)
{
	if (info_a.block_count > confirmation_height && !confirmation_height_processor.is_processing_block (info_a.head))
	{
		auto num_uncemented = info_a.block_count - confirmation_height;
		futurehead::lock_guard<std::mutex> guard (mutex);
		auto it = cementable_frontiers_a.get<tag_account> ().find (account_a);
		if (it != cementable_frontiers_a.get<tag_account> ().end ())
		{
			if (it->blocks_uncemented != num_uncemented)
			{
				// Account already exists and there is now a different uncemented block count so update it in the container
				cementable_frontiers_a.get<tag_account> ().modify (it, [num_uncemented](futurehead::cementable_account & info) {
					info.blocks_uncemented = num_uncemented;
				});
			}
		}
		else
		{
			debug_assert (cementable_frontiers_size_a <= max_priority_cementable_frontiers);
			if (cementable_frontiers_size_a == max_priority_cementable_frontiers)
			{
				// The maximum amount of frontiers stored has been reached. Check if the current frontier
				// has more uncemented blocks than the lowest uncemented frontier in the collection if so replace it.
				auto least_uncemented_frontier_it = cementable_frontiers_a.get<tag_uncemented> ().end ();
				--least_uncemented_frontier_it;
				if (num_uncemented > least_uncemented_frontier_it->blocks_uncemented)
				{
					cementable_frontiers_a.get<tag_uncemented> ().erase (least_uncemented_frontier_it);
					cementable_frontiers_a.get<tag_account> ().emplace (account_a, num_uncemented);
				}
			}
			else
			{
				cementable_frontiers_a.get<tag_account> ().emplace (account_a, num_uncemented);
			}
		}
		cementable_frontiers_size_a = cementable_frontiers_a.size ();
	}
}

void futurehead::active_transactions::prioritize_frontiers_for_confirmation (futurehead::transaction const & transaction_a, std::chrono::milliseconds ledger_account_traversal_max_time_a, std::chrono::milliseconds wallet_account_traversal_max_time_a)
{
	// Don't try to prioritize when there are a large number of pending confirmation heights as blocks can be cemented in the meantime, making the prioritization less reliable
	if (confirmation_height_processor.awaiting_processing_size () < confirmed_frontiers_max_pending_size)
	{
		size_t priority_cementable_frontiers_size;
		size_t priority_wallet_cementable_frontiers_size;
		{
			futurehead::lock_guard<std::mutex> guard (mutex);
			priority_cementable_frontiers_size = priority_cementable_frontiers.size ();
			priority_wallet_cementable_frontiers_size = priority_wallet_cementable_frontiers.size ();
		}
		futurehead::timer<std::chrono::milliseconds> wallet_account_timer;
		wallet_account_timer.start ();

		if (!skip_wallets)
		{
			// Prioritize wallet accounts first
			{
				futurehead::lock_guard<std::mutex> lock (node.wallets.mutex);
				auto wallet_transaction (node.wallets.tx_begin_read ());
				auto const & items = node.wallets.items;
				if (items.empty ())
				{
					skip_wallets = true;
				}
				for (auto item_it = items.cbegin (); item_it != items.cend (); ++item_it)
				{
					// Skip this wallet if it has been traversed already while there are others still awaiting
					if (wallet_ids_already_iterated.find (item_it->first) != wallet_ids_already_iterated.end ())
					{
						continue;
					}

					futurehead::account_info info;
					auto & wallet (item_it->second);
					futurehead::lock_guard<std::recursive_mutex> wallet_lock (wallet->store.mutex);

					auto & next_wallet_frontier_account = next_wallet_id_accounts.emplace (item_it->first, wallet_store::special_count).first->second;

					auto i (wallet->store.begin (wallet_transaction, next_wallet_frontier_account));
					auto n (wallet->store.end ());
					futurehead::confirmation_height_info confirmation_height_info;
					for (; i != n; ++i)
					{
						auto const & account (i->first);
						if (!node.store.account_get (transaction_a, account, info) && !node.store.confirmation_height_get (transaction_a, account, confirmation_height_info))
						{
							// If it exists in normal priority collection delete from there.
							auto it = priority_cementable_frontiers.find (account);
							if (it != priority_cementable_frontiers.end ())
							{
								futurehead::lock_guard<std::mutex> guard (mutex);
								priority_cementable_frontiers.erase (it);
								priority_cementable_frontiers_size = priority_cementable_frontiers.size ();
							}

							prioritize_account_for_confirmation (priority_wallet_cementable_frontiers, priority_wallet_cementable_frontiers_size, account, info, confirmation_height_info.height);

							if (wallet_account_timer.since_start () >= wallet_account_traversal_max_time_a)
							{
								break;
							}
						}
						next_wallet_frontier_account = account.number () + 1;
					}
					// Go back to the beginning when we have reached the end of the wallet accounts for this wallet
					if (i == n)
					{
						wallet_ids_already_iterated.emplace (item_it->first);
						next_wallet_id_accounts.at (item_it->first) = wallet_store::special_count;

						// Skip wallet accounts when they have all been traversed
						if (std::next (item_it) == items.cend ())
						{
							wallet_ids_already_iterated.clear ();
							skip_wallets = true;
						}
					}
				}
			}
		}

		futurehead::timer<std::chrono::milliseconds> timer;
		timer.start ();

		auto i (node.store.latest_begin (transaction_a, next_frontier_account));
		auto n (node.store.latest_end ());
		futurehead::confirmation_height_info confirmation_height_info;
		for (; i != n && !stopped; ++i)
		{
			auto const & account (i->first);
			auto const & info (i->second);
			if (priority_wallet_cementable_frontiers.find (account) == priority_wallet_cementable_frontiers.end ())
			{
				if (!node.store.confirmation_height_get (transaction_a, account, confirmation_height_info))
				{
					prioritize_account_for_confirmation (priority_cementable_frontiers, priority_cementable_frontiers_size, account, info, confirmation_height_info.height);
				}
			}
			next_frontier_account = account.number () + 1;
			if (timer.since_start () >= ledger_account_traversal_max_time_a)
			{
				break;
			}
		}

		// Go back to the beginning when we have reached the end of the accounts and start with wallet accounts next time
		if (i == n)
		{
			next_frontier_account = 0;
			skip_wallets = false;
		}
	}
}

void futurehead::active_transactions::stop ()
{
	futurehead::unique_lock<std::mutex> lock (mutex);
	if (!started)
	{
		condition.wait (lock, [& started = started] { return started; });
	}
	stopped = true;
	lock.unlock ();
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
	generator.stop ();
	lock.lock ();
	roots.clear ();
}

futurehead::election_insertion_result futurehead::active_transactions::insert_impl (std::shared_ptr<futurehead::block> const & block_a, boost::optional<futurehead::uint128_t> const & previous_balance_a, std::function<void(std::shared_ptr<futurehead::block>)> const & confirmation_action_a)
{
	debug_assert (block_a->has_sideband ());
	futurehead::election_insertion_result result;
	if (!stopped)
	{
		auto root (block_a->qualified_root ());
		auto existing (roots.get<tag_root> ().find (root));
		if (existing == roots.get<tag_root> ().end ())
		{
			if (recently_confirmed.get<tag_root> ().find (root) == recently_confirmed.get<tag_root> ().end ())
			{
				result.inserted = true;
				auto hash (block_a->hash ());
				auto epoch (block_a->sideband ().details.epoch);
				futurehead::uint128_t previous_balance (previous_balance_a.value_or (0));
				debug_assert (!(previous_balance_a.value_or (0) > 0 && block_a->previous ().is_zero ()));
				if (!previous_balance_a.is_initialized () && !block_a->previous ().is_zero ())
				{
					auto transaction (node.store.tx_begin_read ());
					if (node.ledger.block_exists (block_a->previous ()))
					{
						previous_balance = node.ledger.balance (transaction, block_a->previous ());
					}
				}
				double multiplier (normalized_multiplier (*block_a));
				bool prioritized = roots.size () < prioritized_cutoff || multiplier > last_prioritized_multiplier.value_or (0);
				result.election = futurehead::make_shared<futurehead::election> (node, block_a, confirmation_action_a, prioritized);
				roots.get<tag_root> ().emplace (futurehead::active_transactions::conflict_info{ root, multiplier, multiplier, result.election, epoch, previous_balance });
				blocks.emplace (hash, result.election);
				add_adjust_difficulty (hash);
				result.election->insert_inactive_votes_cache (hash);
				node.stats.inc (futurehead::stat::type::election, prioritized ? futurehead::stat::detail::election_priority : futurehead::stat::detail::election_non_priority);
			}
		}
		else
		{
			result.election = existing->election;
		}
	}
	return result;
}

futurehead::election_insertion_result futurehead::active_transactions::insert (std::shared_ptr<futurehead::block> const & block_a, boost::optional<futurehead::uint128_t> const & previous_balance_a, std::function<void(std::shared_ptr<futurehead::block>)> const & confirmation_action_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return insert_impl (block_a, previous_balance_a, confirmation_action_a);
}

// Validate a vote and apply it to the current election if one exists
futurehead::vote_code futurehead::active_transactions::vote (std::shared_ptr<futurehead::vote> vote_a)
{
	// If none of the hashes are active, votes are not republished
	bool at_least_one (false);
	// If all hashes were recently confirmed then it is a replay
	unsigned recently_confirmed_counter (0);
	bool replay (false);
	bool processed (false);
	{
		futurehead::lock_guard<std::mutex> lock (mutex);
		for (auto vote_block : vote_a->blocks)
		{
			futurehead::election_vote_result result;
			auto & recently_confirmed_by_hash (recently_confirmed.get<tag_hash> ());
			if (vote_block.which ())
			{
				auto block_hash (boost::get<futurehead::block_hash> (vote_block));
				auto existing (blocks.find (block_hash));
				if (existing != blocks.end ())
				{
					at_least_one = true;
					result = existing->second->vote (vote_a->account, vote_a->sequence, block_hash);
				}
				else if (recently_confirmed_by_hash.count (block_hash) == 0)
				{
					add_inactive_votes_cache (block_hash, vote_a->account);
				}
				else
				{
					++recently_confirmed_counter;
				}
			}
			else
			{
				auto block (boost::get<std::shared_ptr<futurehead::block>> (vote_block));
				auto existing (roots.get<tag_root> ().find (block->qualified_root ()));
				if (existing != roots.get<tag_root> ().end ())
				{
					at_least_one = true;
					result = existing->election->vote (vote_a->account, vote_a->sequence, block->hash ());
				}
				else if (recently_confirmed_by_hash.count (block->hash ()) == 0)
				{
					add_inactive_votes_cache (block->hash (), vote_a->account);
				}
				else
				{
					++recently_confirmed_counter;
				}
			}
			processed = processed || result.processed;
			replay = replay || result.replay;
		}
	}

	if (at_least_one)
	{
		// Republish vote if it is new and the node does not host a principal representative (or close to)
		if (processed)
		{
			auto const reps (node.wallets.reps ());
			if (!reps.have_half_rep () && !reps.exists (vote_a->account))
			{
				node.network.flood_vote (vote_a, 0.5f);
			}
		}
		return replay ? futurehead::vote_code::replay : futurehead::vote_code::vote;
	}
	else if (recently_confirmed_counter == vote_a->blocks.size ())
	{
		return futurehead::vote_code::replay;
	}
	else
	{
		return futurehead::vote_code::indeterminate;
	}
}

bool futurehead::active_transactions::active (futurehead::qualified_root const & root_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return roots.get<tag_root> ().find (root_a) != roots.get<tag_root> ().end ();
}

bool futurehead::active_transactions::active (futurehead::block const & block_a)
{
	return active (block_a.qualified_root ());
}

std::shared_ptr<futurehead::election> futurehead::active_transactions::election (futurehead::qualified_root const & root_a) const
{
	std::shared_ptr<futurehead::election> result;
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto existing = roots.get<tag_root> ().find (root_a);
	if (existing != roots.get<tag_root> ().end ())
	{
		result = existing->election;
	}
	return result;
}

std::shared_ptr<futurehead::block> futurehead::active_transactions::winner (futurehead::block_hash const & hash_a) const
{
	std::shared_ptr<futurehead::block> result;
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto existing = blocks.find (hash_a);
	if (existing != blocks.end ())
	{
		result = existing->second->status.winner;
	}
	return result;
}

futurehead::election_insertion_result futurehead::active_transactions::activate (futurehead::account const & account_a)
{
	futurehead::election_insertion_result result;
	auto transaction (node.store.tx_begin_read ());
	futurehead::account_info account_info;
	if (!node.store.account_get (transaction, account_a, account_info))
	{
		futurehead::confirmation_height_info conf_info;
		auto error = node.store.confirmation_height_get (transaction, account_a, conf_info);
		debug_assert (!error);
		if (!error && conf_info.height < account_info.block_count)
		{
			debug_assert (conf_info.frontier != account_info.head);
			auto hash = conf_info.height == 0 ? account_info.open_block : node.store.block_successor (transaction, conf_info.frontier);
			auto block = node.store.block_get (transaction, hash);
			release_assert (block != nullptr);
			if (node.ledger.can_vote (transaction, *block))
			{
				result = insert (block);
				if (result.election)
				{
					if (result.inserted)
					{
						result.election->transition_active ();
					}
					else if (result.election->prioritized ())
					{
						// Generate vote for ongoing election
						result.election->generate_votes (block->hash ());
					}
				}
			}
		}
	}
	return result;
}

bool futurehead::active_transactions::update_difficulty (futurehead::block const & block_a)
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	auto existing_election (roots.get<tag_root> ().find (block_a.qualified_root ()));
	bool error = existing_election == roots.get<tag_root> ().end () || update_difficulty_impl (existing_election, block_a);
	return error;
}

bool futurehead::active_transactions::update_difficulty_impl (futurehead::active_transactions::roots_iterator const & root_it_a, futurehead::block const & block_a)
{
	debug_assert (!mutex.try_lock ());
	double multiplier (normalized_multiplier (block_a, root_it_a));
	bool error = multiplier <= root_it_a->multiplier;
	if (!error)
	{
		if (node.config.logging.active_update_logging ())
		{
			node.logger.try_log (boost::str (boost::format ("Election %1% difficulty updated with block %2% from multiplier %3% to %4%") % root_it_a->root.to_string () % block_a.hash ().to_string () % root_it_a->multiplier % multiplier));
		}
		roots.get<tag_root> ().modify (root_it_a, [multiplier](futurehead::active_transactions::conflict_info & info_a) {
			info_a.multiplier = multiplier;
		});
		add_adjust_difficulty (block_a.hash ());
		node.stats.inc (futurehead::stat::type::election, futurehead::stat::detail::election_difficulty_update);
	}
	return error;
}

bool futurehead::active_transactions::restart (std::shared_ptr<futurehead::block> const & block_a, futurehead::write_transaction const & transaction_a)
{
	// Only guaranteed to restart the election if the new block is received within 2 minutes of its election being dropped
	constexpr std::chrono::minutes recently_dropped_cutoff{ 2 };
	bool error = true;
	if (recently_dropped.find (block_a->qualified_root ()) > std::chrono::steady_clock::now () - recently_dropped_cutoff)
	{
		auto hash (block_a->hash ());
		auto ledger_block (node.store.block_get (transaction_a, hash));
		if (ledger_block != nullptr && ledger_block->block_work () != block_a->block_work () && !node.block_confirmed_or_being_confirmed (transaction_a, hash))
		{
			if (block_a->difficulty () > ledger_block->difficulty ())
			{
				// Re-writing the block is necessary to avoid the same work being received later to force restarting the election
				// The existing block is re-written, not the arriving block, as that one might not have gone through a full signature check
				ledger_block->block_work_set (block_a->block_work ());

				auto block_count = node.ledger.cache.block_count.load ();
				node.store.block_put (transaction_a, hash, *ledger_block);
				debug_assert (node.ledger.cache.block_count.load () == block_count);

				// Restart election for the upgraded block, previously dropped from elections
				auto previous_balance = node.ledger.balance (transaction_a, ledger_block->previous ());
				auto insert_result = insert (ledger_block, previous_balance);
				if (insert_result.inserted)
				{
					error = false;
					insert_result.election->transition_active ();
					recently_dropped.erase (ledger_block->qualified_root ());
					node.stats.inc (futurehead::stat::type::election, futurehead::stat::detail::election_restart);
				}
			}
		}
	}
	return error;
}

double futurehead::active_transactions::normalized_multiplier (futurehead::block const & block_a, boost::optional<futurehead::active_transactions::roots_iterator> const & root_it_a) const
{
	debug_assert (!mutex.try_lock ());
	auto difficulty (block_a.difficulty ());
	uint64_t threshold (0);
	bool sideband_not_found (false);
	if (block_a.has_sideband ())
	{
		threshold = futurehead::work_threshold (block_a.work_version (), block_a.sideband ().details);
	}
	else if (root_it_a.is_initialized ())
	{
		auto election (*root_it_a);
		debug_assert (election != roots.end ());
		auto find_block (election->election->blocks.find (block_a.hash ()));
		if (find_block != election->election->blocks.end () && find_block->second->has_sideband ())
		{
			threshold = futurehead::work_threshold (block_a.work_version (), find_block->second->sideband ().details);
		}
		else
		{
			// This can have incorrect results during an epoch upgrade, but it only affects prioritization
			bool is_send = election->previous_balance > block_a.balance ().number ();
			bool is_receive = election->previous_balance < block_a.balance ().number ();
			futurehead::block_details details (election->epoch, is_send, is_receive, false);

			threshold = futurehead::work_threshold (block_a.work_version (), details);
			sideband_not_found = true;
		}
	}
	double multiplier (futurehead::difficulty::to_multiplier (difficulty, threshold));
	debug_assert (multiplier >= 1 || sideband_not_found);
	if (multiplier >= 1)
	{
		multiplier = futurehead::normalized_multiplier (multiplier, threshold);
	}
	else
	{
		// Inferred threshold was incorrect
		multiplier = 1;
	}
	return multiplier;
}

void futurehead::active_transactions::add_adjust_difficulty (futurehead::block_hash const & hash_a)
{
	debug_assert (!mutex.try_lock ());
	adjust_difficulty_list.push_back (hash_a);
}

void futurehead::active_transactions::update_adjusted_multiplier ()
{
	debug_assert (!mutex.try_lock ());
	std::unordered_set<futurehead::block_hash> processed_blocks;
	while (!adjust_difficulty_list.empty ())
	{
		auto const & adjust_difficulty_item (adjust_difficulty_list.front ());
		std::deque<std::pair<futurehead::block_hash, int64_t>> remaining_blocks;
		remaining_blocks.emplace_back (adjust_difficulty_item, 0);
		adjust_difficulty_list.pop_front ();
		std::vector<std::pair<futurehead::qualified_root, int64_t>> elections_list;
		double sum (0.);
		int64_t highest_level (0);
		int64_t lowest_level (0);
		while (!remaining_blocks.empty ())
		{
			auto const & item (remaining_blocks.front ());
			auto hash (item.first);
			auto level (item.second);
			if (processed_blocks.find (hash) == processed_blocks.end ())
			{
				auto existing (blocks.find (hash));
				if (existing != blocks.end () && !existing->second->confirmed () && existing->second->status.winner->hash () == hash)
				{
					auto previous (existing->second->status.winner->previous ());
					if (!previous.is_zero ())
					{
						remaining_blocks.emplace_back (previous, level + 1);
					}
					auto source (existing->second->status.winner->source ());
					if (!source.is_zero () && source != previous)
					{
						remaining_blocks.emplace_back (source, level + 1);
					}
					auto link (existing->second->status.winner->link ());
					if (!link.is_zero () && !node.ledger.is_epoch_link (link) && link != previous)
					{
						remaining_blocks.emplace_back (link, level + 1);
					}
					for (auto & dependent_block : existing->second->dependent_blocks)
					{
						remaining_blocks.emplace_back (dependent_block, level - 1);
					}
					processed_blocks.insert (hash);
					futurehead::qualified_root root (previous, existing->second->status.winner->root ());
					auto existing_root (roots.get<tag_root> ().find (root));
					if (existing_root != roots.get<tag_root> ().end ())
					{
						sum += existing_root->multiplier;
						elections_list.emplace_back (root, level);
						if (level > highest_level)
						{
							highest_level = level;
						}
						else if (level < lowest_level)
						{
							lowest_level = level;
						}
					}
				}
			}
			remaining_blocks.pop_front ();
		}
		if (!elections_list.empty ())
		{
			double avg_multiplier = sum / elections_list.size ();
			double min_unit = 32.0 * avg_multiplier * std::numeric_limits<double>::epsilon ();
			debug_assert (min_unit > 0);

			// Set adjusted multiplier
			for (auto & item : elections_list)
			{
				auto existing_root (roots.get<tag_root> ().find (item.first));
				double multiplier_a = avg_multiplier + (double)item.second * min_unit;
				if (existing_root->adjusted_multiplier != multiplier_a)
				{
					roots.get<tag_root> ().modify (existing_root, [multiplier_a](futurehead::active_transactions::conflict_info & info_a) {
						info_a.adjusted_multiplier = multiplier_a;
					});
				}
			}
		}
	}
}

void futurehead::active_transactions::update_active_multiplier (futurehead::unique_lock<std::mutex> & lock_a)
{
	debug_assert (!mutex.try_lock ());
	last_prioritized_multiplier.reset ();
	double multiplier (1.);
	// Heurestic to filter out non-saturated network and frontier confirmation
	if (roots.size () >= prioritized_cutoff || (node.network_params.network.is_test_network () && !roots.empty ()))
	{
		auto & sorted_roots = roots.get<tag_difficulty> ();
		std::vector<double> prioritized;
		prioritized.reserve (std::min (sorted_roots.size (), prioritized_cutoff));
		for (auto it (sorted_roots.begin ()), end (sorted_roots.end ()); it != end && prioritized.size () < prioritized_cutoff; ++it)
		{
			if (!it->election->confirmed ())
			{
				prioritized.push_back (it->adjusted_multiplier);
			}
		}
		if (prioritized.size () > 10 || (node.network_params.network.is_test_network () && !prioritized.empty ()))
		{
			multiplier = prioritized[prioritized.size () / 2];
		}
		if (!prioritized.empty ())
		{
			last_prioritized_multiplier = prioritized.back ();
		}
	}
	debug_assert (multiplier >= futurehead::difficulty::to_multiplier (node.network_params.network.publish_thresholds.entry, node.network_params.network.publish_thresholds.base));
	multipliers_cb.push_front (multiplier);
	auto sum (std::accumulate (multipliers_cb.begin (), multipliers_cb.end (), double(0)));
	double avg_multiplier (sum / multipliers_cb.size ());
	auto difficulty = futurehead::difficulty::from_multiplier (avg_multiplier, node.default_difficulty (futurehead::work_version::work_1));
	debug_assert (difficulty >= node.network_params.network.publish_thresholds.entry);

	trended_active_multiplier = avg_multiplier;
	node.observers.difficulty.notify (difficulty);
}

uint64_t futurehead::active_transactions::active_difficulty ()
{
	return futurehead::difficulty::from_multiplier (active_multiplier (), node.default_difficulty (futurehead::work_version::work_1));
}

uint64_t futurehead::active_transactions::limited_active_difficulty (futurehead::block const & block_a)
{
	uint64_t threshold (0);
	if (block_a.has_sideband ())
	{
		threshold = futurehead::work_threshold (block_a.work_version (), block_a.sideband ().details);
	}
	else
	{
		threshold = node.default_difficulty (block_a.work_version ());
	}
	return limited_active_difficulty (block_a.work_version (), threshold);
}

uint64_t futurehead::active_transactions::limited_active_difficulty (futurehead::work_version const version_a, uint64_t const threshold_a)
{
	auto difficulty (futurehead::difficulty::from_multiplier (futurehead::denormalized_multiplier (active_multiplier (), threshold_a), threshold_a));
	return std::min (difficulty, node.max_work_generate_difficulty (version_a));
}

double futurehead::active_transactions::active_multiplier ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return trended_active_multiplier;
}

// List of active blocks in elections
std::deque<std::shared_ptr<futurehead::block>> futurehead::active_transactions::list_blocks ()
{
	std::deque<std::shared_ptr<futurehead::block>> result;
	futurehead::lock_guard<std::mutex> lock (mutex);
	for (auto & root : roots)
	{
		result.push_back (root.election->status.winner);
	}
	return result;
}

std::deque<futurehead::election_status> futurehead::active_transactions::list_recently_cemented ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return recently_cemented;
}

void futurehead::active_transactions::add_recently_cemented (futurehead::election_status const & status_a)
{
	recently_cemented.push_back (status_a);
	if (recently_cemented.size () > node.config.confirmation_history_size)
	{
		recently_cemented.pop_front ();
	}
}

void futurehead::active_transactions::add_recently_confirmed (futurehead::qualified_root const & root_a, futurehead::block_hash const & hash_a)
{
	recently_confirmed.get<tag_sequence> ().emplace_back (root_a, hash_a);
	if (recently_confirmed.size () > recently_confirmed_size)
	{
		recently_confirmed.get<tag_sequence> ().pop_front ();
	}
}

void futurehead::active_transactions::erase (futurehead::block const & block_a)
{
	futurehead::unique_lock<std::mutex> lock (mutex);
	auto root_it (roots.get<tag_root> ().find (block_a.qualified_root ()));
	if (root_it != roots.get<tag_root> ().end ())
	{
		root_it->election->cleanup ();
		root_it->election->adjust_dependent_difficulty ();
		roots.get<tag_root> ().erase (root_it);
		lock.unlock ();
		node.logger.try_log (boost::str (boost::format ("Election erased for block block %1% root %2%") % block_a.hash ().to_string () % block_a.root ().to_string ()));
	}
}

bool futurehead::active_transactions::empty ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return roots.empty ();
}

size_t futurehead::active_transactions::size ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return roots.size ();
}

bool futurehead::active_transactions::publish (std::shared_ptr<futurehead::block> block_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto existing (roots.get<tag_root> ().find (block_a->qualified_root ()));
	auto result (true);
	if (existing != roots.get<tag_root> ().end ())
	{
		update_difficulty_impl (existing, *block_a);
		auto election (existing->election);
		result = election->publish (block_a);
		if (!result)
		{
			blocks.emplace (block_a->hash (), election);
			node.stats.inc (futurehead::stat::type::election, futurehead::stat::detail::election_block_conflict);
		}
	}
	return result;
}

// Returns the type of election status requiring callbacks calling later
boost::optional<futurehead::election_status_type> futurehead::active_transactions::confirm_block (futurehead::transaction const & transaction_a, std::shared_ptr<futurehead::block> block_a)
{
	auto hash (block_a->hash ());
	futurehead::unique_lock<std::mutex> lock (mutex);
	auto existing (blocks.find (hash));
	boost::optional<futurehead::election_status_type> status_type;
	if (existing != blocks.end ())
	{
		if (existing->second->status.winner && existing->second->status.winner->hash () == hash)
		{
			if (!existing->second->confirmed ())
			{
				existing->second->confirm_once (futurehead::election_status_type::active_confirmation_height);
				status_type = futurehead::election_status_type::active_confirmation_height;
			}
			else
			{
#ifndef NDEBUG
				futurehead::unique_lock<std::mutex> election_winners_lk (election_winner_details_mutex);
				debug_assert (election_winner_details.find (hash) != election_winner_details.cend ());
#endif
				status_type = futurehead::election_status_type::active_confirmed_quorum;
			}
		}
		else
		{
			status_type = boost::optional<futurehead::election_status_type>{};
		}
	}
	else
	{
		status_type = futurehead::election_status_type::inactive_confirmation_height;
	}

	return status_type;
}

size_t futurehead::active_transactions::priority_cementable_frontiers_size ()
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	return priority_cementable_frontiers.size ();
}

size_t futurehead::active_transactions::priority_wallet_cementable_frontiers_size ()
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	return priority_wallet_cementable_frontiers.size ();
}

boost::circular_buffer<double> futurehead::active_transactions::difficulty_trend ()
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	return multipliers_cb;
}

size_t futurehead::active_transactions::inactive_votes_cache_size ()
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	return inactive_votes_cache.size ();
}

void futurehead::active_transactions::add_inactive_votes_cache (futurehead::block_hash const & hash_a, futurehead::account const & representative_a)
{
	// Check principal representative status
	if (node.ledger.weight (representative_a) > node.minimum_principal_weight ())
	{
		auto & inactive_by_hash (inactive_votes_cache.get<tag_hash> ());
		auto existing (inactive_by_hash.find (hash_a));
		if (existing != inactive_by_hash.end ())
		{
			if (!existing->confirmed || !existing->bootstrap_started)
			{
				auto is_new (false);
				inactive_by_hash.modify (existing, [representative_a, &is_new](futurehead::inactive_cache_information & info) {
					auto it = std::find (info.voters.begin (), info.voters.end (), representative_a);
					is_new = (it == info.voters.end ());
					if (is_new)
					{
						info.arrival = std::chrono::steady_clock::now ();
						info.voters.push_back (representative_a);
					}
				});

				if (is_new)
				{
					bool confirmed (false);
					if (inactive_votes_bootstrap_check (existing->voters, hash_a, confirmed) && !existing->bootstrap_started)
					{
						inactive_by_hash.modify (existing, [](futurehead::inactive_cache_information & info) {
							info.bootstrap_started = true;
						});
					}
					if (confirmed && !existing->confirmed)
					{
						inactive_by_hash.modify (existing, [](futurehead::inactive_cache_information & info) {
							info.confirmed = true;
						});
					}
				}
			}
		}
		else
		{
			std::vector<futurehead::account> representative_vector (1, representative_a);
			bool confirmed (false);
			bool start_bootstrap (inactive_votes_bootstrap_check (representative_vector, hash_a, confirmed));
			auto & inactive_by_arrival (inactive_votes_cache.get<tag_arrival> ());
			inactive_by_arrival.emplace (futurehead::inactive_cache_information{ std::chrono::steady_clock::now (), hash_a, representative_vector, start_bootstrap, confirmed });
			if (inactive_votes_cache.size () > node.flags.inactive_votes_cache_size)
			{
				inactive_by_arrival.erase (inactive_by_arrival.begin ());
			}
		}
	}
}

futurehead::inactive_cache_information futurehead::active_transactions::find_inactive_votes_cache (futurehead::block_hash const & hash_a)
{
	auto & inactive_by_hash (inactive_votes_cache.get<tag_hash> ());
	auto existing (inactive_by_hash.find (hash_a));
	if (existing != inactive_by_hash.end ())
	{
		return *existing;
	}
	else
	{
		return futurehead::inactive_cache_information{};
	}
}

void futurehead::active_transactions::erase_inactive_votes_cache (futurehead::block_hash const & hash_a)
{
	inactive_votes_cache.get<tag_hash> ().erase (hash_a);
}

bool futurehead::active_transactions::inactive_votes_bootstrap_check (std::vector<futurehead::account> const & voters_a, futurehead::block_hash const & hash_a, bool & confirmed_a)
{
	uint128_t tally;
	for (auto const & voter : voters_a)
	{
		tally += node.ledger.weight (voter);
	}
	bool start_bootstrap (false);
	if (tally >= node.config.online_weight_minimum.number ())
	{
		start_bootstrap = true;
		confirmed_a = true;
	}
	else if (!node.flags.disable_legacy_bootstrap && tally > node.gap_cache.bootstrap_threshold ())
	{
		start_bootstrap = true;
	}
	if (start_bootstrap && !node.ledger.block_exists (hash_a))
	{
		auto node_l (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + node.network_params.bootstrap.gap_cache_bootstrap_start_interval, [node_l, hash_a]() {
			auto transaction (node_l->store.tx_begin_read ());
			if (!node_l->store.block_exists (transaction, hash_a))
			{
				if (!node_l->bootstrap_initiator.in_progress ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Missing block %1% which has enough votes to warrant lazy bootstrapping it") % hash_a.to_string ()));
				}
				if (!node_l->flags.disable_lazy_bootstrap)
				{
					node_l->bootstrap_initiator.bootstrap_lazy (hash_a);
				}
				else if (!node_l->flags.disable_legacy_bootstrap)
				{
					node_l->bootstrap_initiator.bootstrap ();
				}
			}
		});
	}
	return start_bootstrap;
}

size_t futurehead::active_transactions::election_winner_details_size ()
{
	futurehead::lock_guard<std::mutex> guard (election_winner_details_mutex);
	return election_winner_details.size ();
}

futurehead::cementable_account::cementable_account (futurehead::account const & account_a, size_t blocks_uncemented_a) :
account (account_a), blocks_uncemented (blocks_uncemented_a)
{
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (active_transactions & active_transactions, const std::string & name)
{
	size_t roots_count;
	size_t blocks_count;
	size_t recently_confirmed_count;
	size_t recently_cemented_count;

	{
		futurehead::lock_guard<std::mutex> guard (active_transactions.mutex);
		roots_count = active_transactions.roots.size ();
		blocks_count = active_transactions.blocks.size ();
		recently_confirmed_count = active_transactions.recently_confirmed.size ();
		recently_cemented_count = active_transactions.recently_cemented.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "roots", roots_count, sizeof (decltype (active_transactions.roots)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "blocks", blocks_count, sizeof (decltype (active_transactions.blocks)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "election_winner_details", active_transactions.election_winner_details_size (), sizeof (decltype (active_transactions.election_winner_details)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "recently_confirmed", recently_confirmed_count, sizeof (decltype (active_transactions.recently_confirmed)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "recently_cemented", recently_cemented_count, sizeof (decltype (active_transactions.recently_cemented)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "priority_wallet_cementable_frontiers_count", active_transactions.priority_wallet_cementable_frontiers_size (), sizeof (futurehead::cementable_account) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "priority_cementable_frontiers_count", active_transactions.priority_cementable_frontiers_size (), sizeof (futurehead::cementable_account) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "inactive_votes_cache_count", active_transactions.inactive_votes_cache_size (), sizeof (futurehead::gap_information) }));
	composite->add_component (collect_container_info (active_transactions.generator, "generator"));
	return composite;
}

futurehead::dropped_elections::dropped_elections (futurehead::stat & stats_a) :
stats (stats_a)
{
}

void futurehead::dropped_elections::add (futurehead::qualified_root const & root_a)
{
	stats.inc (futurehead::stat::type::election, futurehead::stat::detail::election_drop);
	futurehead::lock_guard<std::mutex> guard (mutex);
	auto & items_by_sequence = items.get<tag_sequence> ();
	items_by_sequence.emplace_back (futurehead::election_timepoint{ std::chrono::steady_clock::now (), root_a });
	if (items.size () > capacity)
	{
		items_by_sequence.pop_front ();
	}
}

void futurehead::dropped_elections::erase (futurehead::qualified_root const & root_a)
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	items.get<tag_root> ().erase (root_a);
}

std::chrono::steady_clock::time_point futurehead::dropped_elections::find (futurehead::qualified_root const & root_a) const
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	auto & items_by_root = items.get<tag_root> ();
	auto existing (items_by_root.find (root_a));
	if (existing != items_by_root.end ())
	{
		return existing->time;
	}
	else
	{
		return std::chrono::steady_clock::time_point{};
	}
}

size_t futurehead::dropped_elections::size () const
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	return items.size ();
}
