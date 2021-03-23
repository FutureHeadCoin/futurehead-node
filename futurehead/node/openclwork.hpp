#pragma once

#include <futurehead/lib/work.hpp>
#include <futurehead/node/openclconfig.hpp>
#include <futurehead/node/xorshift.hpp>

#include <boost/optional.hpp>

#include <atomic>
#include <mutex>
#include <vector>

#ifdef __APPLE__
#define CL_SILENCE_DEPRECATION
#include <OpenCL/opencl.h>
#else
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/cl.h>
#endif

namespace futurehead
{
class logger_mt;
class opencl_platform
{
public:
	cl_platform_id platform;
	std::vector<cl_device_id> devices;
};
class opencl_environment
{
public:
	opencl_environment (bool &);
	void dump (std::ostream & stream);
	std::vector<futurehead::opencl_platform> platforms;
};
class root;
class work_pool;
class opencl_work
{
public:
	opencl_work (bool &, futurehead::opencl_config const &, futurehead::opencl_environment &, futurehead::logger_mt &);
	~opencl_work ();
	boost::optional<uint64_t> generate_work (futurehead::work_version const, futurehead::root const &, uint64_t const);
	boost::optional<uint64_t> generate_work (futurehead::work_version const, futurehead::root const &, uint64_t const, std::atomic<int> &);
	static std::unique_ptr<opencl_work> create (bool, futurehead::opencl_config const &, futurehead::logger_mt &);
	futurehead::opencl_config const & config;
	std::mutex mutex;
	cl_context context;
	cl_mem attempt_buffer;
	cl_mem result_buffer;
	cl_mem item_buffer;
	cl_mem difficulty_buffer;
	cl_program program;
	cl_kernel kernel;
	cl_command_queue queue;
	futurehead::xorshift1024star rand;
	futurehead::logger_mt & logger;
};
}
