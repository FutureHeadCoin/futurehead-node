#pragma once

#include <futurehead/lib/locks.hpp>
#include <futurehead/lib/rate_limiting.hpp>
#include <futurehead/lib/stats.hpp>
#include <futurehead/node/common.hpp>
#include <futurehead/node/socket.hpp>

namespace futurehead
{
class bandwidth_limiter final
{
public:
	// initialize with limit 0 = unbounded
	bandwidth_limiter (const double, const size_t);
	bool should_drop (const size_t &);

private:
	futurehead::rate::token_bucket bucket;
};

namespace transport
{
	class message;
	futurehead::endpoint map_endpoint_to_v6 (futurehead::endpoint const &);
	futurehead::endpoint map_tcp_to_endpoint (futurehead::tcp_endpoint const &);
	futurehead::tcp_endpoint map_endpoint_to_tcp (futurehead::endpoint const &);
	// Unassigned, reserved, self
	bool reserved_address (futurehead::endpoint const &, bool = false);
	static std::chrono::seconds constexpr syn_cookie_cutoff = std::chrono::seconds (5);
	enum class transport_type : uint8_t
	{
		undefined = 0,
		udp = 1,
		tcp = 2
	};
	class channel
	{
	public:
		channel (futurehead::node &);
		virtual ~channel () = default;
		virtual size_t hash_code () const = 0;
		virtual bool operator== (futurehead::transport::channel const &) const = 0;
		void send (futurehead::message const &, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr, futurehead::buffer_drop_policy = futurehead::buffer_drop_policy::limiter);
		virtual void send_buffer (futurehead::shared_const_buffer const &, futurehead::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr, futurehead::buffer_drop_policy = futurehead::buffer_drop_policy::limiter) = 0;
		virtual std::function<void(boost::system::error_code const &, size_t)> callback (futurehead::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const = 0;
		virtual std::string to_string () const = 0;
		virtual futurehead::endpoint get_endpoint () const = 0;
		virtual futurehead::tcp_endpoint get_tcp_endpoint () const = 0;
		virtual futurehead::transport::transport_type get_type () const = 0;

		std::chrono::steady_clock::time_point get_last_bootstrap_attempt () const
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			return last_bootstrap_attempt;
		}

		void set_last_bootstrap_attempt (std::chrono::steady_clock::time_point const time_a)
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			last_bootstrap_attempt = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_received () const
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			return last_packet_received;
		}

		void set_last_packet_received (std::chrono::steady_clock::time_point const time_a)
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			last_packet_received = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_sent () const
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			return last_packet_sent;
		}

		void set_last_packet_sent (std::chrono::steady_clock::time_point const time_a)
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			last_packet_sent = time_a;
		}

		boost::optional<futurehead::account> get_node_id_optional () const
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			return node_id;
		}

		futurehead::account get_node_id () const
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			if (node_id.is_initialized ())
			{
				return node_id.get ();
			}
			else
			{
				return 0;
			}
		}

		void set_node_id (futurehead::account node_id_a)
		{
			futurehead::lock_guard<std::mutex> lk (channel_mutex);
			node_id = node_id_a;
		}

		uint8_t get_network_version () const
		{
			return network_version;
		}

		void set_network_version (uint8_t network_version_a)
		{
			network_version = network_version_a;
		}

		mutable std::mutex channel_mutex;

	private:
		std::chrono::steady_clock::time_point last_bootstrap_attempt{ std::chrono::steady_clock::time_point () };
		std::chrono::steady_clock::time_point last_packet_received{ std::chrono::steady_clock::time_point () };
		std::chrono::steady_clock::time_point last_packet_sent{ std::chrono::steady_clock::time_point () };
		boost::optional<futurehead::account> node_id{ boost::none };
		std::atomic<uint8_t> network_version{ 0 };

	protected:
		futurehead::node & node;
	};
} // namespace transport
} // namespace futurehead

namespace std
{
template <>
struct hash<::futurehead::transport::channel>
{
	size_t operator() (::futurehead::transport::channel const & channel_a) const
	{
		return channel_a.hash_code ();
	}
};
template <>
struct equal_to<std::reference_wrapper<::futurehead::transport::channel const>>
{
	bool operator() (std::reference_wrapper<::futurehead::transport::channel const> const & lhs, std::reference_wrapper<::futurehead::transport::channel const> const & rhs) const
	{
		return lhs.get () == rhs.get ();
	}
};
}

namespace boost
{
template <>
struct hash<::futurehead::transport::channel>
{
	size_t operator() (::futurehead::transport::channel const & channel_a) const
	{
		std::hash<::futurehead::transport::channel> hash;
		return hash (channel_a);
	}
};
template <>
struct hash<std::reference_wrapper<::futurehead::transport::channel const>>
{
	size_t operator() (std::reference_wrapper<::futurehead::transport::channel const> const & channel_a) const
	{
		std::hash<::futurehead::transport::channel> hash;
		return hash (channel_a.get ());
	}
};
}
