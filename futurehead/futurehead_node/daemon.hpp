namespace boost
{
namespace filesystem
{
	class path;
}
}

namespace futurehead
{
class node_flags;
}
namespace futurehead_daemon
{
class daemon
{
public:
	void run (boost::filesystem::path const &, futurehead::node_flags const & flags);
};
}
