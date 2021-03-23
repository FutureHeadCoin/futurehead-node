#include <futurehead/ipc_flatbuffers_lib/generated/flatbuffers/futureheadapi_generated.h>
#include <futurehead/lib/errors.hpp>
#include <futurehead/lib/numbers.hpp>
#include <futurehead/node/ipc/action_handler.hpp>
#include <futurehead/node/ipc/ipc_server.hpp>
#include <futurehead/node/node.hpp>

#include <iostream>

namespace
{
futurehead::account parse_account (std::string const & account, bool & out_is_deprecated_format)
{
	futurehead::account result (0);
	if (account.empty ())
	{
		throw futurehead::error (futurehead::error_common::bad_account_number);
	}
	if (result.decode_account (account))
	{
		throw futurehead::error (futurehead::error_common::bad_account_number);
	}
	else if (account[3] == '-' || account[4] == '-')
	{
		out_is_deprecated_format = true;
	}

	return result;
}
/** Returns the message as a Flatbuffers ObjectAPI type, managed by a unique_ptr */
template <typename T>
auto get_message (futureheadapi::Envelope const & envelope)
{
	auto raw (envelope.message_as<T> ()->UnPack ());
	return std::unique_ptr<typename T::NativeTableType> (raw);
}
}

/**
 * Mapping from message type to handler function.
 * @note This must be updated whenever a new message type is added to the Flatbuffers IDL.
 */
auto futurehead::ipc::action_handler::handler_map () -> std::unordered_map<futureheadapi::Message, std::function<void(futurehead::ipc::action_handler *, futureheadapi::Envelope const &)>, futurehead::ipc::enum_hash>
{
	static std::unordered_map<futureheadapi::Message, std::function<void(futurehead::ipc::action_handler *, futureheadapi::Envelope const &)>, futurehead::ipc::enum_hash> handlers;
	if (handlers.empty ())
	{
		handlers.emplace (futureheadapi::Message::Message_IsAlive, &futurehead::ipc::action_handler::on_is_alive);
		handlers.emplace (futureheadapi::Message::Message_TopicConfirmation, &futurehead::ipc::action_handler::on_topic_confirmation);
		handlers.emplace (futureheadapi::Message::Message_AccountWeight, &futurehead::ipc::action_handler::on_account_weight);
		handlers.emplace (futureheadapi::Message::Message_ServiceRegister, &futurehead::ipc::action_handler::on_service_register);
		handlers.emplace (futureheadapi::Message::Message_ServiceStop, &futurehead::ipc::action_handler::on_service_stop);
		handlers.emplace (futureheadapi::Message::Message_TopicServiceStop, &futurehead::ipc::action_handler::on_topic_service_stop);
	}
	return handlers;
}

futurehead::ipc::action_handler::action_handler (futurehead::node & node_a, futurehead::ipc::ipc_server & server_a, std::weak_ptr<futurehead::ipc::subscriber> const & subscriber_a, std::shared_ptr<flatbuffers::FlatBufferBuilder> const & builder_a) :
flatbuffer_producer (builder_a),
node (node_a),
ipc_server (server_a),
subscriber (subscriber_a)
{
}

void futurehead::ipc::action_handler::on_topic_confirmation (futureheadapi::Envelope const & envelope_a)
{
	auto confirmationTopic (get_message<futureheadapi::TopicConfirmation> (envelope_a));
	ipc_server.get_broker ().subscribe (subscriber, std::move (confirmationTopic));
	futureheadapi::EventAckT ack;
	create_response (ack);
}

void futurehead::ipc::action_handler::on_service_register (futureheadapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { futurehead::ipc::access_permission::api_service_register, futurehead::ipc::access_permission::service });
	auto query (get_message<futureheadapi::ServiceRegister> (envelope_a));
	ipc_server.get_broker ().service_register (query->service_name, this->subscriber);
	futureheadapi::SuccessT success;
	create_response (success);
}

void futurehead::ipc::action_handler::on_service_stop (futureheadapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { futurehead::ipc::access_permission::api_service_stop, futurehead::ipc::access_permission::service });
	auto query (get_message<futureheadapi::ServiceStop> (envelope_a));
	if (query->service_name == "node")
	{
		ipc_server.node.stop ();
	}
	else
	{
		ipc_server.get_broker ().service_stop (query->service_name);
	}
	futureheadapi::SuccessT success;
	create_response (success);
}

void futurehead::ipc::action_handler::on_topic_service_stop (futureheadapi::Envelope const & envelope_a)
{
	auto topic (get_message<futureheadapi::TopicServiceStop> (envelope_a));
	ipc_server.get_broker ().subscribe (subscriber, std::move (topic));
	futureheadapi::EventAckT ack;
	create_response (ack);
}

void futurehead::ipc::action_handler::on_account_weight (futureheadapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { futurehead::ipc::access_permission::api_account_weight, futurehead::ipc::access_permission::account_query });
	bool is_deprecated_format{ false };
	auto query (get_message<futureheadapi::AccountWeight> (envelope_a));
	auto balance (node.weight (parse_account (query->account, is_deprecated_format)));

	futureheadapi::AccountWeightResponseT response;
	response.voting_weight = balance.str ();
	create_response (response);
}

void futurehead::ipc::action_handler::on_is_alive (futureheadapi::Envelope const & envelope)
{
	futureheadapi::IsAliveT alive;
	create_response (alive);
}

bool futurehead::ipc::action_handler::has_access (futureheadapi::Envelope const & envelope_a, futurehead::ipc::access_permission permission_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access (credentials, permission_a);
}

bool futurehead::ipc::action_handler::has_access_to_all (futureheadapi::Envelope const & envelope_a, std::initializer_list<futurehead::ipc::access_permission> permissions_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access_to_all (credentials, permissions_a);
}

bool futurehead::ipc::action_handler::has_access_to_oneof (futureheadapi::Envelope const & envelope_a, std::initializer_list<futurehead::ipc::access_permission> permissions_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access_to_oneof (credentials, permissions_a);
}

void futurehead::ipc::action_handler::require (futureheadapi::Envelope const & envelope_a, futurehead::ipc::access_permission permission_a) const
{
	if (!has_access (envelope_a, permission_a))
	{
		throw futurehead::error (futurehead::error_common::access_denied);
	}
}

void futurehead::ipc::action_handler::require_all (futureheadapi::Envelope const & envelope_a, std::initializer_list<futurehead::ipc::access_permission> permissions_a) const
{
	if (!has_access_to_all (envelope_a, permissions_a))
	{
		throw futurehead::error (futurehead::error_common::access_denied);
	}
}

void futurehead::ipc::action_handler::require_oneof (futureheadapi::Envelope const & envelope_a, std::initializer_list<futurehead::ipc::access_permission> permissions_a) const
{
	if (!has_access_to_oneof (envelope_a, permissions_a))
	{
		throw futurehead::error (futurehead::error_common::access_denied);
	}
}
