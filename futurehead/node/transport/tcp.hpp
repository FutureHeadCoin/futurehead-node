#pragma once

#include <futurehead/node/common.hpp>
#include <futurehead/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <unordered_set>

namespace mi = boost::multi_index;

namespace futurehead
{
class bootstrap_server;
enum class bootstrap_server_type;
class tcp_message_item final
{
public:
	std::shared_ptr<futurehead::message> message;
	futurehead::tcp_endpoint endpoint;
	futurehead::account node_id;
	std::shared_ptr<futurehead::socket> socket;
	futurehead::bootstrap_server_type type;
};
namespace transport
{
	class tcp_channels;
	class channel_tcp : public futurehead::transport::channel
	{
		friend class futurehead::transport::tcp_channels;

	public:
		channel_tcp (futurehead::node &, std::weak_ptr<futurehead::socket>);
		~channel_tcp ();
		size_t hash_code () const override;
		bool operator== (futurehead::transport::channel const &) const override;
		void send_buffer (futurehead::shared_const_buffer const &, futurehead::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr, futurehead::buffer_drop_policy = futurehead::buffer_drop_policy::limiter) override;
		std::function<void(boost::system::error_code const &, size_t)> callback (futurehead::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const override;
		std::function<void(boost::system::error_code const &, size_t)> tcp_callback (futurehead::stat::detail, futurehead::tcp_endpoint const &, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const;
		std::string to_string () const override;
		bool operator== (futurehead::transport::channel_tcp const & other_a) const
		{
			return &node == &other_a.node && socket.lock () == other_a.socket.lock ();
		}
		std::weak_ptr<futurehead::socket> socket;
		std::weak_ptr<futurehead::bootstrap_server> response_server;
		/* Mark for temporary channels. Usually remote ports of these channels are ephemeral and received from incoming connections to server.
		If remote part has open listening port, temporary channel will be replaced with direct connection to listening port soon. But if other side is behing NAT or firewall this connection can be pemanent. */
		std::atomic<bool> temporary{ false };

		futurehead::endpoint get_endpoint () const override
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			if (auto socket_l = socket.lock ())
			{
				return futurehead::transport::map_tcp_to_endpoint (socket_l->remote_endpoint ());
			}
			else
			{
				return futurehead::endpoint (boost::asio::ip::address_v6::any (), 0);
			}
		}

		futurehead::tcp_endpoint get_tcp_endpoint () const override
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			if (auto socket_l = socket.lock ())
			{
				return socket_l->remote_endpoint ();
			}
			else
			{
				return futurehead::tcp_endpoint (boost::asio::ip::address_v6::any (), 0);
			}
		}

		futurehead::transport::transport_type get_type () const override
		{
			return futurehead::transport::transport_type::tcp;
		}
	};
	class tcp_channels final
	{
		friend class futurehead::transport::channel_tcp;
		friend class telemetry_simultaneous_requests_Test;

	public:
		tcp_channels (futurehead::node &);
		bool insert (std::shared_ptr<futurehead::transport::channel_tcp>, std::shared_ptr<futurehead::socket>, std::shared_ptr<futurehead::bootstrap_server>);
		void erase (futurehead::tcp_endpoint const &);
		size_t size () const;
		std::shared_ptr<futurehead::transport::channel_tcp> find_channel (futurehead::tcp_endpoint const &) const;
		void random_fill (std::array<futurehead::endpoint, 8> &) const;
		std::unordered_set<std::shared_ptr<futurehead::transport::channel>> random_set (size_t, uint8_t = 0, bool = false) const;
		bool store_all (bool = true);
		std::shared_ptr<futurehead::transport::channel_tcp> find_node_id (futurehead::account const &);
		// Get the next peer for attempting a tcp connection
		futurehead::tcp_endpoint bootstrap_peer (uint8_t connection_protocol_version_min);
		void receive ();
		void start ();
		void stop ();
		void process_messages ();
		void process_message (futurehead::message const &, futurehead::tcp_endpoint const &, futurehead::account const &, std::shared_ptr<futurehead::socket>, futurehead::bootstrap_server_type);
		bool max_ip_connections (futurehead::tcp_endpoint const &);
		// Should we reach out to this endpoint with a keepalive message
		bool reachout (futurehead::endpoint const &);
		std::unique_ptr<container_info_component> collect_container_info (std::string const &);
		void purge (std::chrono::steady_clock::time_point const &);
		void ongoing_keepalive ();
		void list_below_version (std::vector<std::shared_ptr<futurehead::transport::channel>> &, uint8_t);
		void list (std::deque<std::shared_ptr<futurehead::transport::channel>> &, uint8_t = 0, bool = true);
		void modify (std::shared_ptr<futurehead::transport::channel_tcp>, std::function<void(std::shared_ptr<futurehead::transport::channel_tcp>)>);
		void update (futurehead::tcp_endpoint const &);
		// Connection start
		void start_tcp (futurehead::endpoint const &, std::function<void(std::shared_ptr<futurehead::transport::channel>)> const & = nullptr);
		void start_tcp_receive_node_id (std::shared_ptr<futurehead::transport::channel_tcp>, futurehead::endpoint const &, std::shared_ptr<std::vector<uint8_t>>, std::function<void(std::shared_ptr<futurehead::transport::channel>)> const &);
		void udp_fallback (futurehead::endpoint const &, std::function<void(std::shared_ptr<futurehead::transport::channel>)> const &);
		void push_node_id_handshake_socket (std::shared_ptr<futurehead::socket> const & socket_a);
		void remove_node_id_handshake_socket (std::shared_ptr<futurehead::socket> const & socket_a);
		bool node_id_handhake_sockets_empty () const;
		futurehead::node & node;

	private:
		class endpoint_tag
		{
		};
		class ip_address_tag
		{
		};
		class random_access_tag
		{
		};
		class last_packet_sent_tag
		{
		};
		class last_bootstrap_attempt_tag
		{
		};
		class last_attempt_tag
		{
		};
		class node_id_tag
		{
		};
		class version_tag
		{
		};

		class channel_tcp_wrapper final
		{
		public:
			std::shared_ptr<futurehead::transport::channel_tcp> channel;
			std::shared_ptr<futurehead::socket> socket;
			std::shared_ptr<futurehead::bootstrap_server> response_server;
			channel_tcp_wrapper (std::shared_ptr<futurehead::transport::channel_tcp> const & channel_a, std::shared_ptr<futurehead::socket> const & socket_a, std::shared_ptr<futurehead::bootstrap_server> const & server_a) :
			channel (channel_a), socket (socket_a), response_server (server_a)
			{
			}
			futurehead::tcp_endpoint endpoint () const
			{
				return channel->get_tcp_endpoint ();
			}
			std::chrono::steady_clock::time_point last_packet_sent () const
			{
				return channel->get_last_packet_sent ();
			}
			std::chrono::steady_clock::time_point last_bootstrap_attempt () const
			{
				return channel->get_last_bootstrap_attempt ();
			}
			boost::asio::ip::address ip_address () const
			{
				return endpoint ().address ();
			}
			futurehead::account node_id () const
			{
				auto node_id (channel->get_node_id ());
				debug_assert (!node_id.is_zero ());
				return node_id;
			}
			uint8_t network_version () const
			{
				return channel->get_network_version ();
			}
		};
		class tcp_endpoint_attempt final
		{
		public:
			futurehead::tcp_endpoint endpoint;
			boost::asio::ip::address address;
			std::chrono::steady_clock::time_point last_attempt{ std::chrono::steady_clock::now () };

			explicit tcp_endpoint_attempt (futurehead::tcp_endpoint const & endpoint_a) :
			endpoint (endpoint_a),
			address (endpoint_a.address ())
			{
			}
		};
		mutable std::mutex mutex;
		// clang-format off
		boost::multi_index_container<channel_tcp_wrapper,
		mi::indexed_by<
			mi::random_access<mi::tag<random_access_tag>>,
			mi::ordered_non_unique<mi::tag<last_bootstrap_attempt_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, std::chrono::steady_clock::time_point, &channel_tcp_wrapper::last_bootstrap_attempt>>,
			mi::hashed_unique<mi::tag<endpoint_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, futurehead::tcp_endpoint, &channel_tcp_wrapper::endpoint>>,
			mi::hashed_non_unique<mi::tag<node_id_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, futurehead::account, &channel_tcp_wrapper::node_id>>,
			mi::ordered_non_unique<mi::tag<last_packet_sent_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, std::chrono::steady_clock::time_point, &channel_tcp_wrapper::last_packet_sent>>,
			mi::ordered_non_unique<mi::tag<version_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, uint8_t, &channel_tcp_wrapper::network_version>>,			
			mi::hashed_non_unique<mi::tag<ip_address_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, boost::asio::ip::address, &channel_tcp_wrapper::ip_address>>>>
		channels;
		boost::multi_index_container<tcp_endpoint_attempt,
		mi::indexed_by<
			mi::hashed_unique<mi::tag<endpoint_tag>,
				mi::member<tcp_endpoint_attempt, futurehead::tcp_endpoint, &tcp_endpoint_attempt::endpoint>>,
			mi::hashed_non_unique<mi::tag<ip_address_tag>,
				mi::member<tcp_endpoint_attempt, boost::asio::ip::address, &tcp_endpoint_attempt::address>>,
			mi::ordered_non_unique<mi::tag<last_attempt_tag>,
				mi::member<tcp_endpoint_attempt, std::chrono::steady_clock::time_point, &tcp_endpoint_attempt::last_attempt>>>>
		attempts;
		// clang-format on
		// This owns the sockets until the node_id_handshake has been completed. Needed to prevent self referencing callbacks, they are periodically removed if any are dangling.
		std::vector<std::shared_ptr<futurehead::socket>> node_id_handshake_sockets;
		std::atomic<bool> stopped{ false };
	};
} // namespace transport
} // namespace futurehead
