#include <futurehead/lib/threading.hpp>

#include <iostream>

namespace
{
thread_local futurehead::thread_role::name current_thread_role = futurehead::thread_role::name::unknown;
}

futurehead::thread_role::name futurehead::thread_role::get ()
{
	return current_thread_role;
}

std::string futurehead::thread_role::get_string (futurehead::thread_role::name role)
{
	std::string thread_role_name_string;

	switch (role)
	{
		case futurehead::thread_role::name::unknown:
			thread_role_name_string = "<unknown>";
			break;
		case futurehead::thread_role::name::io:
			thread_role_name_string = "I/O";
			break;
		case futurehead::thread_role::name::work:
			thread_role_name_string = "Work pool";
			break;
		case futurehead::thread_role::name::packet_processing:
			thread_role_name_string = "Pkt processing";
			break;
		case futurehead::thread_role::name::alarm:
			thread_role_name_string = "Alarm";
			break;
		case futurehead::thread_role::name::vote_processing:
			thread_role_name_string = "Vote processing";
			break;
		case futurehead::thread_role::name::block_processing:
			thread_role_name_string = "Blck processing";
			break;
		case futurehead::thread_role::name::request_loop:
			thread_role_name_string = "Request loop";
			break;
		case futurehead::thread_role::name::wallet_actions:
			thread_role_name_string = "Wallet actions";
			break;
		case futurehead::thread_role::name::work_watcher:
			thread_role_name_string = "Work watcher";
			break;
		case futurehead::thread_role::name::bootstrap_initiator:
			thread_role_name_string = "Bootstrap init";
			break;
		case futurehead::thread_role::name::bootstrap_connections:
			thread_role_name_string = "Bootstrap conn";
			break;
		case futurehead::thread_role::name::voting:
			thread_role_name_string = "Voting";
			break;
		case futurehead::thread_role::name::signature_checking:
			thread_role_name_string = "Signature check";
			break;
		case futurehead::thread_role::name::rpc_request_processor:
			thread_role_name_string = "RPC processor";
			break;
		case futurehead::thread_role::name::rpc_process_container:
			thread_role_name_string = "RPC process";
			break;
		case futurehead::thread_role::name::confirmation_height_processing:
			thread_role_name_string = "Conf height";
			break;
		case futurehead::thread_role::name::worker:
			thread_role_name_string = "Worker";
			break;
		case futurehead::thread_role::name::request_aggregator:
			thread_role_name_string = "Req aggregator";
			break;
		case futurehead::thread_role::name::state_block_signature_verification:
			thread_role_name_string = "State block sig";
			break;
		case futurehead::thread_role::name::epoch_upgrader:
			thread_role_name_string = "Epoch upgrader";
			break;
	}

	/*
		 * We want to constrain the thread names to 15
		 * characters, since this is the smallest maximum
		 * length supported by the platforms we support
		 * (specifically, Linux)
		 */
	debug_assert (thread_role_name_string.size () < 16);
	return (thread_role_name_string);
}

std::string futurehead::thread_role::get_string ()
{
	return get_string (current_thread_role);
}

void futurehead::thread_role::set (futurehead::thread_role::name role)
{
	auto thread_role_name_string (get_string (role));

	futurehead::thread_role::set_os_name (thread_role_name_string);

	current_thread_role = role;
}

void futurehead::thread_attributes::set (boost::thread::attributes & attrs)
{
	auto attrs_l (&attrs);
	attrs_l->set_stack_size (8000000); //8MB
}

futurehead::thread_runner::thread_runner (boost::asio::io_context & io_ctx_a, unsigned service_threads_a) :
io_guard (boost::asio::make_work_guard (io_ctx_a))
{
	boost::thread::attributes attrs;
	futurehead::thread_attributes::set (attrs);
	for (auto i (0u); i < service_threads_a; ++i)
	{
		threads.emplace_back (attrs, [&io_ctx_a]() {
			futurehead::thread_role::set (futurehead::thread_role::name::io);
			try
			{
				io_ctx_a.run ();
			}
			catch (std::exception const & ex)
			{
				std::cerr << ex.what () << std::endl;
#ifndef NDEBUG
				throw;
#endif
			}
			catch (...)
			{
#ifndef NDEBUG
				/*
				 * In a release build, catch and swallow the
				 * io_context exception, in debug mode pass it
				 * on
				 */
				throw;
#endif
			}
		});
	}
}

futurehead::thread_runner::~thread_runner ()
{
	join ();
}

void futurehead::thread_runner::join ()
{
	io_guard.reset ();
	for (auto & i : threads)
	{
		if (i.joinable ())
		{
			i.join ();
		}
	}
}

void futurehead::thread_runner::stop_event_processing ()
{
	io_guard.get_executor ().context ().stop ();
}
