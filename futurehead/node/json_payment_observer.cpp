#include <futurehead/lib/json_error_response.hpp>
#include <futurehead/node/ipc/ipc_server.hpp>
#include <futurehead/node/json_handler.hpp>
#include <futurehead/node/json_payment_observer.hpp>
#include <futurehead/node/node.hpp>
#include <futurehead/node/payment_observer_processor.hpp>

futurehead::json_payment_observer::json_payment_observer (futurehead::node & node_a, std::function<void(std::string const &)> const & response_a, futurehead::account const & account_a, futurehead::amount const & amount_a) :
node (node_a),
account (account_a),
amount (amount_a),
response (response_a)
{
	completed.clear ();
}

void futurehead::json_payment_observer::start (uint64_t timeout)
{
	auto this_l (shared_from_this ());
	node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout), [this_l]() {
		this_l->complete (futurehead::payment_status::nothing);
	});
}

void futurehead::json_payment_observer::observe ()
{
	if (node.balance (account) >= amount.number ())
	{
		complete (futurehead::payment_status::success);
	}
}

void futurehead::json_payment_observer::complete (futurehead::payment_status status)
{
	auto already (completed.test_and_set ());
	if (!already)
	{
		if (node.config.logging.log_ipc ())
		{
			node.logger.always_log (boost::str (boost::format ("Exiting json_payment_observer for account %1% status %2%") % account.to_account () % static_cast<unsigned> (status)));
		}
		switch (status)
		{
			case futurehead::payment_status::nothing:
			{
				boost::property_tree::ptree response_l;
				response_l.put ("deprecated", "1");
				response_l.put ("status", "nothing");
				std::stringstream ostream;
				boost::property_tree::write_json (ostream, response_l);
				response (ostream.str ());
				break;
			}
			case futurehead::payment_status::success:
			{
				boost::property_tree::ptree response_l;
				response_l.put ("deprecated", "1");
				response_l.put ("status", "success");
				std::stringstream ostream;
				boost::property_tree::write_json (ostream, response_l);
				response (ostream.str ());
				break;
			}
			default:
			{
				json_error_response (response, "Internal payment error");
				break;
			}
		}
		node.payment_observer_processor.erase (account);
	}
}
