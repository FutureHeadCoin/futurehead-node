#include <futurehead/lib/alarm.hpp>
#include <futurehead/lib/threading.hpp>

bool futurehead::operation::operator> (futurehead::operation const & other_a) const
{
	return wakeup > other_a.wakeup;
}

futurehead::alarm::alarm (boost::asio::io_context & io_ctx_a) :
io_ctx (io_ctx_a),
thread ([this]() {
	futurehead::thread_role::set (futurehead::thread_role::name::alarm);
	run ();
})
{
}

futurehead::alarm::~alarm ()
{
	add (std::chrono::steady_clock::now (), nullptr);
	thread.join ();
}

void futurehead::alarm::run ()
{
	futurehead::unique_lock<std::mutex> lock (mutex);
	auto done (false);
	while (!done)
	{
		if (!operations.empty ())
		{
			auto & operation (operations.top ());
			if (operation.function)
			{
				if (operation.wakeup <= std::chrono::steady_clock::now ())
				{
					io_ctx.post (operation.function);
					operations.pop ();
				}
				else
				{
					auto wakeup (operation.wakeup);
					condition.wait_until (lock, wakeup);
				}
			}
			else
			{
				done = true;
			}
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void futurehead::alarm::add (std::chrono::steady_clock::time_point const & wakeup_a, std::function<void()> const & operation)
{
	{
		futurehead::lock_guard<std::mutex> guard (mutex);
		operations.push (futurehead::operation ({ wakeup_a, operation }));
	}
	condition.notify_all ();
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (alarm & alarm, const std::string & name)
{
	size_t count;
	{
		futurehead::lock_guard<std::mutex> guard (alarm.mutex);
		count = alarm.operations.size ();
	}
	auto sizeof_element = sizeof (decltype (alarm.operations)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "operations", count, sizeof_element }));
	return composite;
}
