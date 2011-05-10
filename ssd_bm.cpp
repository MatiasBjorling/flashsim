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
#include <algorithm>
#include <queue>
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
		max_log_blocks = FAST_LOG_PAGE_LIMIT;
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
		address.set_linear_address(free_list.back()->get_physical_address(), BLOCK);
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
	invalid_list.push_back(ftl.get_block_pointer(address));

	switch (type)
	{
	case DATA:
		data_active--;
		break;
	case LOG:
		log_active--;
		break;
	case LOG_SEQ:
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

	// Calculate if GC should be activated.

	float used;
	if (FTL_IMPLEMENTATION >= 3)
	{
		used = (int)invalid_list.size() + (int)active_list.size() - (int)free_list.size();
		//printf("Invalid: %i Active: %i Free: %i Total: %i\n", (int)invalid_list.size(), (int)active_list.size(), (int)free_list.size(), SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE);
	} else {
		//printf("Invalid: %i Log: %i Data: %i Free: %i Total: %i\n", (int)invalid_list.size(), log_active, data_active, (int)free_list.size(), SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE);
		used = (int)invalid_list.size() + (int)log_active + (int)data_active - (int)free_list.size();
	}
	float total = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE;
	float ratio = used/total;

	//printf("ratio: %f\n", ratio);
	if (ratio < 0.7)
		return;

	uint num_to_erase = 50; // Magic number

	// First step and least expensive it to go though invalid list.
	while (num_to_erase != 0 && invalid_list.size() != 0)
	{
		Event erase_event = Event(ERASE, event.get_logical_address(), 1, event.get_start_time()+event.get_time_taken());

		Address address = new Address(invalid_list.back()->get_physical_address(), BLOCK);

		free_list.push_back(invalid_list.back());

		invalid_list.pop_back();

		printf("Erasing address: %lu Block: %u\n", address.get_linear_address(), address.block);

		erase_event.set_address(address);

		ftl.controller.issue(erase_event);
		event.consolidate_metaevent(erase_event);

		num_to_erase--;
		ftl.controller.stats.numFTLErase++;
	}

	// Then go though the active blocks via the priority queue of active pages.
	// We limit it to page-mapping algorithms.
	while (num_to_erase != 0 && active_list.size() != 0)
	{
		std::sort(active_list.rbegin(), active_list.rend());

		Block *blockErase = active_list.back();
		active_list.pop_back();

		// Let the FTL handle cleanup of the block.
		ftl.cleanup_block(event, blockErase);

		// Create erase event and attach to current event queue.
		Event erase_event = Event(ERASE, event.get_logical_address(), 1, event.get_start_time()+event.get_time_taken());
		Address address = Address(blockErase->get_physical_address(), BLOCK);
		printf("1Erasing address: %lu Block: %u\n", blockErase->get_physical_address(), address.block);
		erase_event.set_address(address);

		free_list.push_back(blockErase);

		// Execute erase
		ftl.controller.issue(erase_event);
		event.consolidate_metaevent(erase_event);

		num_to_erase--;
		ftl.controller.stats.numFTLErase++;
	}
}

Address Block_manager::get_free_block(block_type type)
{
	Address address;
	get_page_block(address);
	switch (type)
	{
	case DATA:
		ftl.controller.get_block_pointer(address)->set_block_type(DATA);
		data_active++;
		break;
	case LOG:
		if (log_active == max_log_blocks)
			throw std::bad_alloc();

		ftl.controller.get_block_pointer(address)->set_block_type(LOG);
		log_active++;
		break;
	default:
		break;
	}

	if (FTL_IMPLEMENTATION >= 3)
		active_list.push_back(ftl.get_block_pointer(address));

	return address;
}

void Block_manager::simulate_map_write(Event &events)
{
	return;
	Event eraseEvent = Event(ERASE, events.get_logical_address(), 1, events.get_start_time()+events.get_time_taken());
	Event writeEvent = Event(WRITE, events.get_logical_address(), 1, events.get_start_time()+events.get_time_taken());

	writeEvent.set_address(Address(map_offset + directoryCurrentPage, PAGE));

	if (++directoryCurrentPage % BLOCK_SIZE == 0)
	{
		if (directoryCurrentPage == max_map_pages)
			directoryCurrentPage = 0;

		Address eraseAddress = Address(map_offset + directoryCurrentPage, BLOCK);
		if (ftl.get_block_state(eraseAddress) == ACTIVE)
		{
			eraseEvent.set_address(eraseAddress);
			eraseEvent.set_noop(true);

			writeEvent.set_next(eraseEvent);

			// Statistics
			ftl.controller.stats.numGCErase++;
		}
	}

	ftl.controller.issue(writeEvent);

	events.consolidate_metaevent(writeEvent);

	// Statistics
	ftl.controller.stats.numGCWrite++;
}
void Block_manager::simulate_map_read(Event &events)
{
	return;
	ulong inside_block = events.get_address().get_linear_address() / BLOCK_SIZE;
	if (!(directoryCachedPage >= inside_block && (directoryCachedPage + map_space_capacity) > inside_block))
	{
		Event readEvent = Event(READ, events.get_logical_address(), 1, events.get_start_time()+events.get_time_taken());
		readEvent.set_address(events.get_address());
		readEvent.set_noop(true);

		ftl.controller.issue(readEvent);

		events.consolidate_metaevent(readEvent);

		ftl.controller.stats.numGCRead++;
	}
}
