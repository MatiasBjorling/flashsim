/* Copyright 2011 Matias Bjørling */

/* Block Management
 *
 * This class handle allocation of block pools for the FTL
 * algorithms.
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <stdexcept>
#include "ssd.h"

using namespace ssd;


Block_manager::Block_manager(FtlParent &ftl) : ftl(ftl)
{
	/*
	 * Configuration of blocks.
	 * User-space is the number of blocks minus the
	 * requirements for map directory.
	 */
	max_blocks = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE - MAP_DIRECTORY_SIZE;

	if (FTL_IMPLEMENTATION == IMPL_FAST) // FAST
		max_log_blocks = LOG_PAGE_LIMIT;
	else
		max_log_blocks = max_blocks;

	max_map_pages = MAP_DIRECTORY_SIZE * BLOCK_SIZE;
	map_offset = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE*BLOCK_SIZE-max_map_pages;

	map_space_capacity = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE / (SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE * 32 / 8 / PAGE_SIZE);

	directoryCurrentPage = 0;
	simpleCurrentFree = 0;
	return;
}

Block_manager::~Block_manager(void)
{
	return;
}

/*
 * Retrieves a page using either simple approach (when not all
 * pages have been written or the complex that retrieves
 * it from a free page list.
 */
void Block_manager::get_page_block(Address &address)
{
	if ((simpleCurrentFree/BLOCK_SIZE) < max_blocks)
	{
		address.set_linear_address(simpleCurrentFree, BLOCK);
		simpleCurrentFree += BLOCK_SIZE;
	}
	else
	{
		assert(free_list.size() != 0);
		address.set_linear_address(free_list.back(), BLOCK);
		free_list.pop_back();
	}
}


Address Block_manager::get_free_block()
{
	return get_free_block(DATA);
}

/*
 * Handles block manager statistics when changing a
 * block to a data block from a log block or vice versa.
 */
void Block_manager::promote_block(block_type to_type)
{
	if (to_type == DATA)
	{
		data_active++;
		log_active--;
	}
	else if (to_type == LOG)
	{
		log_active++;
		data_active--;
	}
}

/*
 * Returns true if there are no space left for additional log pages.
 */
bool Block_manager::is_log_full()
{
	return log_active == max_log_blocks;
}

void Block_manager::print_statistics()
{
	printf("-----------------\n");
	printf("Block Statistics:\n");
	printf("-----------------\n");
	printf("Log blocks:  %lu\n", log_active);
	printf("Data blocks: %lu\n", data_active);
	printf("Free blocks: %lu\n", (max_blocks - (simpleCurrentFree/BLOCK_SIZE)) + free_list.size());
	printf("Invalid blocks: %lu\n", invalid_list.size());
	printf("-----------------\n");
}

void Block_manager::invalidate(Address &address, block_type type)
{
	invalid_list.push_back(address.get_linear_address());
	switch (type)
	{
	case DATA:
		data_active--;
		break;
	case LOG:
		log_active--;
		break;
	}
}



/*
 * Insert erase events into the event stream.
 * The strategy is to clean up all invalid pages instantly.
 */
void Block_manager::insert_events(Event &event)
{
	//print_statistics();

	// Goto last element and add eventual erase events.
	Event *eventOps = event.get_last_event(event);

	for (std::vector<ulong>::iterator it = invalid_list.begin(); it != invalid_list.end(); it++)
	{
		Event *erase_event = new Event(ERASE, event.get_logical_address(), 1, event.get_start_time());
		Address address = new Address(*it, BLOCK);

		printf("Erasing address: %u Block: %u\n", address.get_linear_address(), address.block);

		erase_event->set_address(address);

		eventOps->set_next(*erase_event);
		eventOps = erase_event;
	}

	invalid_list.clear();
}

Address Block_manager::get_free_block(block_type type)
{
	Address address;
	switch (type)
	{
	case DATA:
		get_page_block(address);
		data_active++;
		break;
	case LOG:
		if (log_active == max_log_blocks)
			throw std::bad_alloc();

		get_page_block(address);
		log_active++;
		break;
	}
	return address;
}

void Block_manager::simulate_map_write(Event &events)
{
	Event *eventOps = events.get_last_event(events);

	Address address = new Address(map_offset + directoryCurrentPage, PAGE);

	Event *writeEvent = new Event(WRITE, events.get_logical_address(), 1, events.get_start_time());
	writeEvent->set_address(address);

	eventOps->set_next(*writeEvent);
	eventOps = writeEvent;

	if (++directoryCurrentPage % BLOCK_SIZE == 0)
	{
		if (directoryCurrentPage == max_map_pages)
			directoryCurrentPage = 0;

		Address eraseAddress = new Address(map_offset + directoryCurrentPage, BLOCK);
		if (ftl.get_block_state(eraseAddress) == ACTIVE)
		{
			Event *eraseEvent = new Event(ERASE, events.get_logical_address(), 1, events.get_start_time());
			eraseEvent->set_address(eraseAddress);
			eventOps->set_next(*eraseEvent);
			eventOps = eraseEvent;
		}
	}
}
void Block_manager::simulate_map_read(Event &events)
{
	ulong inside_block = events.get_address().get_linear_address() / BLOCK_SIZE;
	if (!(directoryCachedPage >= inside_block && (directoryCachedPage + map_space_capacity) > inside_block))
	{
		Event *eventOps = &events;
		if (eventOps->get_next() != NULL)
			while (eventOps->get_next() == NULL)
				eventOps = eventOps->get_next();

		Event *readEvent = new Event(READ, events.get_logical_address(), 1, events.get_start_time());
		readEvent->set_address(events.get_address());
		eventOps->set_next(*readEvent);
	}
}
