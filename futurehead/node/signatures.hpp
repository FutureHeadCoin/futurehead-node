#pragma once

#include <futurehead/boost/asio/thread_pool.hpp>
#include <futurehead/lib/utility.hpp>

#include <atomic>
#include <future>
#include <mutex>

namespace futurehead
{
class signature_check_set final
{
public:
	signature_check_set (size_t size, unsigned char const ** messages, size_t * message_lengths, unsigned char const ** pub_keys, unsigned char const ** signatures, int * verifications) :
	size (size), messages (messages), message_lengths (message_lengths), pub_keys (pub_keys), signatures (signatures), verifications (verifications)
	{
	}

	size_t size;
	unsigned char const ** messages;
	size_t * message_lengths;
	unsigned char const ** pub_keys;
	unsigned char const ** signatures;
	int * verifications;
};

/** Multi-threaded signature checker */
class signature_checker final
{
public:
	signature_checker (unsigned num_threads);
	~signature_checker ();
	void verify (signature_check_set &);
	void stop ();
	void flush ();

	static size_t constexpr batch_size = 256;

private:
	struct Task final
	{
		Task (futurehead::signature_check_set & check, size_t pending) :
		check (check), pending (pending)
		{
		}
		~Task ()
		{
			release_assert (pending == 0);
		}
		futurehead::signature_check_set & check;
		std::atomic<size_t> pending;
	};

	bool verify_batch (const futurehead::signature_check_set & check_a, size_t index, size_t size);
	void verify_async (futurehead::signature_check_set & check_a, size_t num_batches, std::promise<void> & promise);
	void set_thread_names (unsigned num_threads);
	boost::asio::thread_pool thread_pool;
	std::atomic<int> tasks_remaining{ 0 };
	const bool single_threaded;
	unsigned num_threads;
	std::atomic<bool> stopped{ false };
};
}
