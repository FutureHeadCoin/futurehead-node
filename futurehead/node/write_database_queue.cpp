#include <futurehead/lib/utility.hpp>
#include <futurehead/node/write_database_queue.hpp>

#include <algorithm>

futurehead::write_guard::write_guard (std::function<void()> guard_finish_callback_a) :
guard_finish_callback (guard_finish_callback_a)
{
}

futurehead::write_guard::write_guard (futurehead::write_guard && write_guard_a) noexcept :
guard_finish_callback (std::move (write_guard_a.guard_finish_callback)),
owns (write_guard_a.owns)
{
	write_guard_a.owns = false;
	write_guard_a.guard_finish_callback = nullptr;
}

futurehead::write_guard & futurehead::write_guard::operator= (futurehead::write_guard && write_guard_a) noexcept
{
	owns = write_guard_a.owns;
	guard_finish_callback = std::move (write_guard_a.guard_finish_callback);

	write_guard_a.owns = false;
	write_guard_a.guard_finish_callback = nullptr;
	return *this;
}

futurehead::write_guard::~write_guard ()
{
	if (owns)
	{
		guard_finish_callback ();
	}
}

bool futurehead::write_guard::is_owned () const
{
	return owns;
}

void futurehead::write_guard::release ()
{
	debug_assert (owns);
	if (owns)
	{
		guard_finish_callback ();
	}
	owns = false;
}

futurehead::write_database_queue::write_database_queue () :
guard_finish_callback ([& queue = queue, &mutex = mutex, &cv = cv]() {
	{
		futurehead::lock_guard<std::mutex> guard (mutex);
		queue.pop_front ();
	}
	cv.notify_all ();
})
{
}

futurehead::write_guard futurehead::write_database_queue::wait (futurehead::writer writer)
{
	futurehead::unique_lock<std::mutex> lk (mutex);
	// Add writer to the end of the queue if it's not already waiting
	auto exists = std::find (queue.cbegin (), queue.cend (), writer) != queue.cend ();
	if (!exists)
	{
		queue.push_back (writer);
	}

	while (queue.front () != writer)
	{
		cv.wait (lk);
	}

	return write_guard (guard_finish_callback);
}

bool futurehead::write_database_queue::contains (futurehead::writer writer)
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	return std::find (queue.cbegin (), queue.cend (), writer) != queue.cend ();
}

bool futurehead::write_database_queue::process (futurehead::writer writer)
{
	auto result = false;
	{
		futurehead::lock_guard<std::mutex> guard (mutex);
		// Add writer to the end of the queue if it's not already waiting
		auto exists = std::find (queue.cbegin (), queue.cend (), writer) != queue.cend ();
		if (!exists)
		{
			queue.push_back (writer);
		}

		result = (queue.front () == writer);
	}

	if (!result)
	{
		cv.notify_all ();
	}

	return result;
}

futurehead::write_guard futurehead::write_database_queue::pop ()
{
	return write_guard (guard_finish_callback);
}
