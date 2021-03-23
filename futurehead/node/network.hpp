#pragma once

#include <futurehead/node/common.hpp>
#include <futurehead/node/peer_exclusion.hpp>
#include <futurehead/node/transport/tcp.hpp>
#include <futurehead/node/transport/udp.hpp>
#include <futurehead/secure/network_filter.hpp>

#include <boost/thread/thread.hpp>

#include <memory>
#include <queue>
#include <unordered_set>
namespace futurehead
{
class channel;
class node;
class stats;
class transaction;
class message_buffer final
{
public:
	uint8_t * buffer{ nullptr };
	size_t size{ 0 };
	futurehead::endpoint endpoint;
};
/**
  * A circular buffer for servicing futurehead realtime messages.
  * This container follows a producer/consumer model where the operating system is producing data in to
  * buffers which are serviced by internal threads.
  * If buffers are not serviced fast enough they're internally dropped.
  * This container has a maximum space to hold N buffers of M size and will allocate them in round-robin order.
  * All public methods are thread-safe
*/
class message_buffer_manager final
{
public:
	// Stats - Statistics
	// Size - Size of each individual buffer
	// Count - Number of buffers to allocate
	message_buffer_manager (futurehead::stat & stats, size_t, size_t);
	// Return a buffer where message data can be put
	// Method will attempt to return the first free buffer
	// If there are no free buffers, an unserviced buffer will be dequeued and returned
	// Function will block if there are no free or unserviced buffers
	// Return nullptr if the container has stopped
	futurehead::message_buffer * allocate ();
	// Queue a buffer that has been filled with message data and notify servicing threads
	void enqueue (futurehead::message_buffer *);
	// Return a buffer that has been filled with message data
	// Function will block until a buffer has been added
	// Return nullptr if the container has stopped
	futurehead::message_buffer * dequeue ();
	// Return a buffer to the freelist after is has been serviced
	void release (futurehead::message_buffer *);
	// Stop container and notify waiting threads
	void stop ();

private:
	futurehead::stat & stats;
	std::mutex mutex;
	futurehead::condition_variable condition;
	boost::circular_buffer<futurehead::message_buffer *> free;
	boost::circular_buffer<futurehead::message_buffer *> full;
	std::vector<uint8_t> slab;
	std::vector<futurehead::message_buffer> entries;
	bool stopped;
};
class tcp_message_manager final
{
public:
	tcp_message_manager (unsigned incoming_connections_max_a);
	void put_message (futurehead::tcp_message_item const & item_a);
	futurehead::tcp_message_item get_message ();
	// Stop container and notify waiting threads
	void stop ();

private:
	std::mutex mutex;
	futurehead::condition_variable producer_condition;
	futurehead::condition_variable consumer_condition;
	std::deque<futurehead::tcp_message_item> entries;
	unsigned max_entries;
	static unsigned const max_entries_per_connection = 16;
	bool stopped{ false };

	friend class network_tcp_message_manager_Test;
};
/**
  * Node ID cookies for node ID handshakes
*/
class syn_cookies final
{
public:
	syn_cookies (size_t);
	void purge (std::chrono::steady_clock::time_point const &);
	// Returns boost::none if the IP is rate capped on syn cookie requests,
	// or if the endpoint already has a syn cookie query
	boost::optional<futurehead::uint256_union> assign (futurehead::endpoint const &);
	// Returns false if valid, true if invalid (true on error convention)
	// Also removes the syn cookie from the store if valid
	bool validate (futurehead::endpoint const &, futurehead::account const &, futurehead::signature const &);
	std::unique_ptr<container_info_component> collect_container_info (std::string const &);
	size_t cookies_size ();

private:
	class syn_cookie_info final
	{
	public:
		futurehead::uint256_union cookie;
		std::chrono::steady_clock::time_point created_at;
	};
	mutable std::mutex syn_cookie_mutex;
	std::unordered_map<futurehead::endpoint, syn_cookie_info> cookies;
	std::unordered_map<boost::asio::ip::address, unsigned> cookies_per_ip;
	size_t max_cookies_per_ip;
};
class network final
{
public:
	network (futurehead::node &, uint16_t);
	~network ();
	void start ();
	void stop ();
	void flood_message (futurehead::message const &, futurehead::buffer_drop_policy const = futurehead::buffer_drop_policy::limiter, float const = 1.0f);
	void flood_keepalive (float const scale_a = 1.0f)
	{
		futurehead::keepalive message;
		random_fill (message.peers);
		flood_message (message, futurehead::buffer_drop_policy::limiter, scale_a);
	}
	void flood_keepalive_self (float const scale_a = 0.5f)
	{
		futurehead::keepalive message;
		fill_keepalive_self (message.peers);
		flood_message (message, futurehead::buffer_drop_policy::limiter, scale_a);
	}
	void flood_vote (std::shared_ptr<futurehead::vote> const &, float scale);
	void flood_vote_pr (std::shared_ptr<futurehead::vote> const &);
	// Flood block to all PRs and a random selection of non-PRs
	void flood_block_initial (std::shared_ptr<futurehead::block> const &);
	// Flood block to a random selection of peers
	void flood_block (std::shared_ptr<futurehead::block> const &, futurehead::buffer_drop_policy const = futurehead::buffer_drop_policy::limiter);
	void flood_block_many (std::deque<std::shared_ptr<futurehead::block>>, std::function<void()> = nullptr, unsigned = broadcast_interval_ms);
	void merge_peers (std::array<futurehead::endpoint, 8> const &);
	void merge_peer (futurehead::endpoint const &);
	void send_keepalive (std::shared_ptr<futurehead::transport::channel>);
	void send_keepalive_self (std::shared_ptr<futurehead::transport::channel>);
	void send_node_id_handshake (std::shared_ptr<futurehead::transport::channel>, boost::optional<futurehead::uint256_union> const & query, boost::optional<futurehead::uint256_union> const & respond_to);
	void send_confirm_req (std::shared_ptr<futurehead::transport::channel>, std::shared_ptr<futurehead::block>);
	void broadcast_confirm_req (std::shared_ptr<futurehead::block>);
	void broadcast_confirm_req_base (std::shared_ptr<futurehead::block>, std::shared_ptr<std::vector<std::shared_ptr<futurehead::transport::channel>>>, unsigned, bool = false);
	void broadcast_confirm_req_batched_many (std::unordered_map<std::shared_ptr<futurehead::transport::channel>, std::deque<std::pair<futurehead::block_hash, futurehead::root>>>, std::function<void()> = nullptr, unsigned = broadcast_interval_ms, bool = false);
	void broadcast_confirm_req_many (std::deque<std::pair<std::shared_ptr<futurehead::block>, std::shared_ptr<std::vector<std::shared_ptr<futurehead::transport::channel>>>>>, std::function<void()> = nullptr, unsigned = broadcast_interval_ms);
	std::shared_ptr<futurehead::transport::channel> find_node_id (futurehead::account const &);
	std::shared_ptr<futurehead::transport::channel> find_channel (futurehead::endpoint const &);
	void process_message (futurehead::message const &, std::shared_ptr<futurehead::transport::channel>);
	bool not_a_peer (futurehead::endpoint const &, bool);
	// Should we reach out to this endpoint with a keepalive message
	bool reachout (futurehead::endpoint const &, bool = false);
	std::deque<std::shared_ptr<futurehead::transport::channel>> list (size_t, uint8_t = 0, bool = true);
	std::deque<std::shared_ptr<futurehead::transport::channel>> list_non_pr (size_t);
	// Desired fanout for a given scale
	size_t fanout (float scale = 1.0f) const;
	void random_fill (std::array<futurehead::endpoint, 8> &) const;
	void fill_keepalive_self (std::array<futurehead::endpoint, 8> &) const;
	// Note: The minimum protocol version is used after the random selection, so number of peers can be less than expected.
	std::unordered_set<std::shared_ptr<futurehead::transport::channel>> random_set (size_t, uint8_t = 0, bool = false) const;
	// Get the next peer for attempting a tcp bootstrap connection
	futurehead::tcp_endpoint bootstrap_peer (bool = false);
	futurehead::endpoint endpoint ();
	void cleanup (std::chrono::steady_clock::time_point const &);
	void ongoing_cleanup ();
	// Node ID cookies cleanup
	futurehead::syn_cookies syn_cookies;
	void ongoing_syn_cookie_cleanup ();
	void ongoing_keepalive ();
	size_t size () const;
	float size_sqrt () const;
	bool empty () const;
	void erase (futurehead::transport::channel const &);
	void erase_below_version (uint8_t);
	futurehead::message_buffer_manager buffer_container;
	boost::asio::ip::udp::resolver resolver;
	std::vector<boost::thread> packet_processing_threads;
	futurehead::bandwidth_limiter limiter;
	futurehead::peer_exclusion excluded_peers;
	futurehead::tcp_message_manager tcp_message_manager;
	futurehead::node & node;
	futurehead::network_filter publish_filter;
	futurehead::transport::udp_channels udp_channels;
	futurehead::transport::tcp_channels tcp_channels;
	std::atomic<uint16_t> port{ 0 };
	std::function<void()> disconnect_observer;
	// Called when a new channel is observed
	std::function<void(std::shared_ptr<futurehead::transport::channel>)> channel_observer;
	std::atomic<bool> stopped{ false };
	static unsigned const broadcast_interval_ms = 10;
	static size_t const buffer_size = 512;
	static size_t const confirm_req_hashes_max = 7;
	static size_t const confirm_ack_hashes_max = 12;
};
std::unique_ptr<container_info_component> collect_container_info (network & network, const std::string & name);
}
