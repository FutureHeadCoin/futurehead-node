#include <futurehead/lib/memory.hpp>
#include <futurehead/secure/common.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
/** This allocator records the size of all allocations that happen */
template <class T>
class record_allocations_new_delete_allocator
{
public:
	using value_type = T;

	explicit record_allocations_new_delete_allocator (std::vector<size_t> * allocated) :
	allocated (allocated)
	{
	}

	template <typename U>
	record_allocations_new_delete_allocator (const record_allocations_new_delete_allocator<U> & a)
	{
		allocated = a.allocated;
	}

	template <typename U>
	record_allocations_new_delete_allocator & operator= (const record_allocations_new_delete_allocator<U> &) = delete;

	T * allocate (size_t num_to_allocate)
	{
		auto size_allocated = (sizeof (T) * num_to_allocate);
		allocated->push_back (size_allocated);
		return static_cast<T *> (::operator new (size_allocated));
	}

	void deallocate (T * p, size_t num_to_deallocate)
	{
		::operator delete (p);
	}

	std::vector<size_t> * allocated;
};

template <typename T>
size_t get_allocated_size ()
{
	std::vector<size_t> allocated;
	record_allocations_new_delete_allocator<T> alloc (&allocated);
	(void)std::allocate_shared<T, record_allocations_new_delete_allocator<T>> (alloc);
	debug_assert (allocated.size () == 1);
	return allocated.front ();
}
}

TEST (memory_pool, validate_cleanup)
{
	// This might be turned off, e.g on Mac for instance, so don't do this test
	if (!futurehead::get_use_memory_pools ())
	{
		return;
	}

	futurehead::make_shared<futurehead::open_block> ();
	futurehead::make_shared<futurehead::receive_block> ();
	futurehead::make_shared<futurehead::send_block> ();
	futurehead::make_shared<futurehead::change_block> ();
	futurehead::make_shared<futurehead::state_block> ();
	futurehead::make_shared<futurehead::vote> ();

	ASSERT_TRUE (futurehead::purge_singleton_pool_memory<futurehead::open_block> ());
	ASSERT_TRUE (futurehead::purge_singleton_pool_memory<futurehead::receive_block> ());
	ASSERT_TRUE (futurehead::purge_singleton_pool_memory<futurehead::send_block> ());
	ASSERT_TRUE (futurehead::purge_singleton_pool_memory<futurehead::state_block> ());
	ASSERT_TRUE (futurehead::purge_singleton_pool_memory<futurehead::vote> ());

	// Change blocks have the same size as open_block so won't deallocate any memory
	ASSERT_FALSE (futurehead::purge_singleton_pool_memory<futurehead::change_block> ());

	ASSERT_EQ (futurehead::determine_shared_ptr_pool_size<futurehead::open_block> (), get_allocated_size<futurehead::open_block> () - sizeof (size_t));
	ASSERT_EQ (futurehead::determine_shared_ptr_pool_size<futurehead::receive_block> (), get_allocated_size<futurehead::receive_block> () - sizeof (size_t));
	ASSERT_EQ (futurehead::determine_shared_ptr_pool_size<futurehead::send_block> (), get_allocated_size<futurehead::send_block> () - sizeof (size_t));
	ASSERT_EQ (futurehead::determine_shared_ptr_pool_size<futurehead::change_block> (), get_allocated_size<futurehead::change_block> () - sizeof (size_t));
	ASSERT_EQ (futurehead::determine_shared_ptr_pool_size<futurehead::state_block> (), get_allocated_size<futurehead::state_block> () - sizeof (size_t));
	ASSERT_EQ (futurehead::determine_shared_ptr_pool_size<futurehead::vote> (), get_allocated_size<futurehead::vote> () - sizeof (size_t));
}
