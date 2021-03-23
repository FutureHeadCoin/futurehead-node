#pragma once

#include <futurehead/node/common.hpp>
#include <futurehead/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <mutex>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace futurehead
{
class message_buffer;
namespace transport
{
	class udp_channels;
	class channel_udp final : public futurehead::transport::channel
	{
		friend class futurehead::transport::udp_channels;

	public:
		channel_udp (futurehead::transport::udp_channels &, futurehead::endpoint const &, uint8_t protocol_version);
		size_t hash_code () const override;
		bool operator== (futurehead::transport::channel const &) const override;
		void send_buffer (futurehead::shared_const_buffer const &, futurehead::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr, futurehead::buffer_drop_policy = futurehead::buffer_drop_policy::limiter) override;
		std::function<void(boost::system::error_code const &, size_t)> callback (futurehead::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const override;
		std::string to_string () const override;
		bool operator== (futurehead::transport::channel_udp const & other_a) const
		{
			return &channels == &other_a.channels && endpoint == other_a.endpoint;
		}

		futurehead::endpoint get_endpoint () const override
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			return endpoint;
		}

		futurehead::tcp_endpoint get_tcp_endpoint () const override
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			return futurehead::transport::map_endpoint_to_tcp (endpoint);
		}

		futurehead::transport::transport_type get_type () const override
		{
			return futurehead::transport::transport_type::udp;
		}

		std::chrono::steady_clock::time_point get_last_telemetry_req ()
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			return last_telemetry_req;
		}

		void set_last_telemetry_req (std::chrono::steady_clock::time_point const time_a)
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			last_telemetry_req = time_a;
		}

	private:
		futurehead::endpoint endpoint;
		futurehead::transport::udp_channels & channels;
		std::chrono::steady_clock::time_point last_telemetry_req{ std::chrono::steady_clock::time_point () };
	};
	class udp_channels final
	{
		friend class futurehead::transport::channel_udp;

	public:
		udp_channels (futurehead::node &, uint16_t);
		std::shared_ptr<futurehead::transport::channel_udp> insert (futurehead::endpoint const &, unsigned);
		void erase (futurehead::endpoint const &);
		size_t size () const;
		std::shared_ptr<futurehead::transport::channel_udp> channel (futurehead::endpoint const &) const;
		void random_fill (std::array<futurehead::endpoint, 8> &) const;
		std::unordered_set<std::shared_ptr<futurehead::transport::channel>> random_set (size_t, uint8_t = 0) const;
		bool store_all (bool = true);
		std::shared_ptr<futurehead::transport::channel_udp> find_node_id (futurehead::account const &);
		void clean_node_id (futurehead::account const &);
		void clean_node_id (futurehead::endpoint const &, futurehead::account const &);
		// Get the next peer for attempting a tcp bootstrap connection
		futurehead::tcp_endpoint bootstrap_peer (uint8_t connection_protocol_version_min);
		void receive ();
		void start ();
		void stop ();
		void send (futurehead::shared_const_buffer const & buffer_a, futurehead::endpoint endpoint_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a);
		futurehead::endpoint get_local_endpoint () const;
		void receive_action (futurehead::message_buffer *);
		void process_packets ();
		std::shared_ptr<futurehead::transport::channel> create (futurehead::endpoint const &);
		bool max_ip_connections (futurehead::endpoint const &);
		// Should we reach out to this endpoint with a keepalive message
		bool reachout (futurehead::endpoint const &);
		std::unique_ptr<container_info_component> collect_container_info (std::string const &);
		void purge (std::chrono::steady_clock::time_point const &);
		void ongoing_keepalive ();
		void list_below_version (std::vector<std::shared_ptr<futurehead::transport::channel>> &, uint8_t);
		void list (std::deque<std::shared_ptr<futurehead::transport::channel>> &, uint8_t = 0);
		void modify (std::shared_ptr<futurehead::transport::channel_udp>, std::function<void(std::shared_ptr<futurehead::transport::channel_udp>)>);
		futurehead::node & node;

	private:
		void close_socket ();
		class endpoint_tag
		{
		};
		class ip_address_tag
		{
		};
		class random_access_tag
		{
		};
		class last_packet_received_tag
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
		class channel_udp_wrapper final
		{
		public:
			std::shared_ptr<futurehead::transport::channel_udp> channel;
			channel_udp_wrapper (std::shared_ptr<futurehead::transport::channel_udp> const & channel_a) :
			channel (channel_a)
			{
			}
			futurehead::endpoint endpoint () const
			{
				return channel->get_endpoint ();
			}
			std::chrono::steady_clock::time_point last_packet_received () const
			{
				return channel->get_last_packet_received ();
			}
			std::chrono::steady_clock::time_point last_bootstrap_attempt () const
			{
				return channel->get_last_bootstrap_attempt ();
			}
			std::chrono::steady_clock::time_point last_telemetry_req () const
			{
				return channel->get_last_telemetry_req ();
			}
			boost::asio::ip::address ip_address () const
			{
				return endpoint ().address ();
			}
			futurehead::account node_id () const
			{
				return channel->get_node_id ();
			}
		};
		class endpoint_attempt final
		{
		public:
			futurehead::endpoint endpoint;
			std::chrono::steady_clock::time_point last_attempt{ std::chrono::steady_clock::now () };

			explicit endpoint_attempt (futurehead::endpoint const & endpoint_a) :
			endpoint (endpoint_a)
			{
			}
		};
		mutable std::mutex mutex;
		// clang-format off
		boost::multi_index_container<
		channel_udp_wrapper,
		mi::indexed_by<
			mi::random_access<mi::tag<random_access_tag>>,
			mi::ordered_non_unique<mi::tag<last_bootstrap_attempt_tag>,
				mi::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_bootstrap_attempt>>,
			mi::hashed_unique<mi::tag<endpoint_tag>,
				mi::const_mem_fun<channel_udp_wrapper, futurehead::endpoint, &channel_udp_wrapper::endpoint>>,
			mi::hashed_non_unique<mi::tag<node_id_tag>,
				mi::const_mem_fun<channel_udp_wrapper, futurehead::account, &channel_udp_wrapper::node_id>>,
			mi::ordered_non_unique<mi::tag<last_packet_received_tag>,
				mi::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_packet_received>>,
			mi::hashed_non_unique<mi::tag<ip_address_tag>,
				mi::const_mem_fun<channel_udp_wrapper, boost::asio::ip::address, &channel_udp_wrapper::ip_address>>>>
		channels;
		boost::multi_index_container<
		endpoint_attempt,
		mi::indexed_by<
			mi::hashed_unique<mi::tag<endpoint_tag>,
				mi::member<endpoint_attempt, futurehead::endpoint, &endpoint_attempt::endpoint>>,
			mi::ordered_non_unique<mi::tag<last_attempt_tag>,
				mi::member<endpoint_attempt, std::chrono::steady_clock::time_point, &endpoint_attempt::last_attempt>>>>
		attempts;
		// clang-format on
		boost::asio::strand<boost::asio::io_context::executor_type> strand;
		std::unique_ptr<boost::asio::ip::udp::socket> socket;
		futurehead::endpoint local_endpoint;
		std::atomic<bool> stopped{ false };
	};
} // namespace transport
} // namespace futurehead
