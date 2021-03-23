#include <futurehead/lib/threading.hpp>
#include <futurehead/lib/worker.hpp>

futurehead::worker::worker () :
thread ([this]() {
	futurehead::thread_role::set (futurehead::thread_role::name::worker);
	this->run ();
})
{
}

void futurehead::worker::run ()
{
	futurehead::unique_lock<std::mutex> lk (mutex);
	while (!stopped)
	{
		if (!queue.empty ())
		{
			auto func = queue.front ();
			queue.pop_front ();
			lk.unlock ();
			func ();
			// So that we reduce locking for anything being pushed as that will
			// most likely be on an io-thread
			std::this_thread::yield ();
			lk.lock ();
		}
		else
		{
			cv.wait (lk);
		}
	}
}

futurehead::worker::~worker ()
{
	stop ();
}

void futurehead::worker::push_task (std::function<void()> func_a)
{
	{
		futurehead::lock_guard<std::mutex> guard (mutex);
		if (!stopped)
		{
			queue.emplace_back (func_a);
		}
	}

	cv.notify_one ();
}

void futurehead::worker::stop ()
{
	{
		futurehead::unique_lock<std::mutex> lk (mutex);
		stopped = true;
		queue.clear ();
	}
	cv.notify_one ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (futurehead::worker & worker, const std::string & name)
{
	size_t count;
	{
		futurehead::lock_guard<std::mutex> guard (worker.mutex);
		count = worker.queue.size ();
	}
	auto sizeof_element = sizeof (decltype (worker.queue)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<futurehead::container_info_leaf> (futurehead::container_info{ "queue", count, sizeof_element }));
	return composite;
}
