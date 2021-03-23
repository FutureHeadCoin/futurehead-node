#include <futurehead/ipc_flatbuffers_lib/flatbuffer_producer.hpp>
#include <futurehead/ipc_flatbuffers_lib/generated/flatbuffers/futureheadapi_generated.h>
#include <futurehead/lib/asio.hpp>
#include <futurehead/lib/ipc.hpp>
#include <futurehead/lib/ipc_client.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/program_options.hpp>

#include <iostream>

#include <flatbuffers/flatbuffers.h>

namespace
{
void read_message_loop (std::shared_ptr<futurehead::ipc::ipc_client> const & connection)
{
	auto buffer (std::make_shared<std::vector<uint8_t>> ());
	connection->async_read_message (buffer, std::chrono::seconds::max (), [buffer, connection](futurehead::error error_a, size_t size_a) {
		if (!error_a)
		{
			auto verifier (flatbuffers::Verifier (buffer->data (), buffer->size ()));
			if (!futureheadapi::VerifyEnvelopeBuffer (verifier))
			{
				std::cerr << "Invalid message" << std::endl;
			}
			else
			{
				auto envelope (futureheadapi::GetEnvelope (buffer->data ()));
				if (envelope->message_type () == futureheadapi::Message_EventConfirmation)
				{
					std::cout << "Confirmation at " << envelope->time () << std::endl;
					auto conf (envelope->message_as_EventConfirmation ());
					std::cout << "  Account    : " << conf->account ()->str () << std::endl;
					std::cout << "  Amount     : " << conf->amount ()->str () << std::endl;
					std::cout << "  Block type : " << futureheadapi::EnumNamesBlock ()[conf->block_type ()] << std::endl;
					auto state_block = conf->block_as_BlockState ();
					if (state_block)
					{
						std::cout << "  Balance    : " << state_block->balance ()->str () << std::endl;
					}
				}

				read_message_loop (connection);
			}
		}
	});
}
}

/** A sample IPC/flatbuffers client that subscribes to confirmations from a local node */
int main (int argc, char * const * argv)
{
	boost::asio::io_context io_ctx;
	auto connection (std::make_shared<futurehead::ipc::ipc_client> (io_ctx));
	// The client only connects to a local live node for now; the test will
	// be improved later to handle various options, including port and address.
	std::string ipc_address = "::1";
	uint16_t ipc_port = 7157; //TO-CHANGE
	connection->async_connect (ipc_address, ipc_port, [connection](futurehead::error err) {
		if (!err)
		{
			futureheadapi::TopicConfirmationT conf;
			connection->async_write (futurehead::ipc::shared_buffer_from (conf), [connection](futurehead::error err, size_t size) {
				if (!err)
				{
					std::cout << "Awaiting confirmations..." << std::endl;
					read_message_loop (connection);
				}
			});
		}
		else
		{
			std::cerr << err.get_message () << std::endl;
		}
	});

	io_ctx.run ();
	return 0;
}
