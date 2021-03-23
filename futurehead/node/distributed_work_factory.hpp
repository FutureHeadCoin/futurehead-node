#pragma once

#include <futurehead/lib/numbers.hpp>
#include <futurehead/node/distributed_work.hpp>

#include <boost/optional/optional.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace futurehead
{
class node;
class distributed_work;
class root;

class distributed_work_factory final
{
public:
	distributed_work_factory (futurehead::node &);
	~distributed_work_factory ();
	bool make (futurehead::work_version const, futurehead::root const &, std::vector<std::pair<std::string, uint16_t>> const &, uint64_t, std::function<void(boost::optional<uint64_t>)> const &, boost::optional<futurehead::account> const & = boost::none);
	bool make (std::chrono::seconds const &, futurehead::work_request const &);
	void cancel (futurehead::root const &, bool const local_stop = false);
	void cleanup_finished ();
	void stop ();

	futurehead::node & node;
	std::unordered_map<futurehead::root, std::vector<std::weak_ptr<futurehead::distributed_work>>> items;
	std::mutex mutex;
	std::atomic<bool> stopped{ false };
};

class container_info_component;
std::unique_ptr<container_info_component> collect_container_info (distributed_work_factory & distributed_work, const std::string & name);
}
