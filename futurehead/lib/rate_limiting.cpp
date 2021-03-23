#include <futurehead/lib/locks.hpp>
#include <futurehead/lib/rate_limiting.hpp>
#include <futurehead/lib/utility.hpp>

#include <limits>

futurehead::rate::token_bucket::token_bucket (size_t max_token_count_a, size_t refill_rate_a)
{
	// A token count of 0 indicates unlimited capacity. We use 1e9 as
	// a sentinel, allowing largest burst to still be computed.
	if (max_token_count_a == 0 || refill_rate_a == 0)
	{
		refill_rate_a = max_token_count_a = 1e9;
	}
	max_token_count = smallest_size = current_size = max_token_count_a;
	refill_rate = refill_rate_a;
	last_refill = std::chrono::steady_clock::now ();
}

bool futurehead::rate::token_bucket::try_consume (unsigned tokens_required_a)
{
	debug_assert (tokens_required_a <= 1e9);
	futurehead::lock_guard<std::mutex> lk (bucket_mutex);
	refill ();
	bool possible = current_size >= tokens_required_a;
	if (possible)
	{
		current_size -= tokens_required_a;
	}
	else if (tokens_required_a == 1e9)
	{
		current_size = 0;
	}

	// Keep track of smallest observed bucket size so burst size can be computed (for tests and stats)
	smallest_size = std::min (smallest_size, current_size);

	return possible || refill_rate == 1e9;
}

void futurehead::rate::token_bucket::refill ()
{
	auto now (std::chrono::steady_clock::now ());
	double tokens_to_add = std::chrono::duration_cast<std::chrono::nanoseconds> (now - last_refill).count () / 1e9 * refill_rate;
	current_size = std::min (current_size + tokens_to_add, static_cast<double> (max_token_count));
	last_refill = std::chrono::steady_clock::now ();
}

size_t futurehead::rate::token_bucket::largest_burst () const
{
	futurehead::lock_guard<std::mutex> lk (bucket_mutex);
	return max_token_count - smallest_size;
}
