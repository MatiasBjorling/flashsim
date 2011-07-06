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

	max_blocks = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE;
	max_log_blocks = max_blocks;

	if (FTL_IMPLEMENTATION == IMPL_FAST)
		max_log_blocks = FAST_LOG_PAGE_LIMIT;

	// Block-based map lookup simulation
	max_map_pages = MAP_DIRECTORY_SIZE * BLOCK_SIZE;
	//map_space_capacity = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE / (SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE * 32 / 8 / PAGE_SIZE);

	directoryCurrentPage = 0;
	simpleCurrentFree = 0;
	num_insert_events = 0;

	data_active = 0;
	log_active = 0;
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
	if (simpleCurrentFree < max_blocks*BLOCK_SIZE)
	{
		address.set_linear_address(simpleCurrentFree, BLOCK);
		simpleCurrentFree += BLOCK_SIZE;
	}
	else
	{
		assert(free_list.size() != 0);
		address.set_linear_address(free_list.front()->get_physical_address(), BLOCK);
		free_list.erase(free_list.begin());
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

void Block_manager::invalidate(Address address, block_type type)
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

bool Block_manager::block_comparitor (Block const *x,Block const *y) {
	// We assumme we have to read all pages to write out valid pages. (therefore 1+u) else (1-u)
	// cost/benefit = (age * (1+u)) / 2u

	if (x->get_modification_time() == -1)
		return false;

	if (y->get_modification_time() == -1)
		return true;

	float ratio1 = (BLOCK_SIZE - (x->get_pages_valid() - x->get_pages_invalid())) / BLOCK_SIZE;
	float ratio2 = (BLOCK_SIZE - (y->get_pages_valid() - y->get_pages_invalid())) / BLOCK_SIZE;

	double bc1 = ( 1+ratio1) / (1+(2*ratio1));
	double bc2 = ( 1+ratio2) / (1+(2*ratio2));

	return bc1 < bc2;
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
	if (FTL_IMPLEMENTATION == IMPL_DFTL || FTL_IMPLEMENTATION == IMPL_BIMODAL)
	{
		used = (int)invalid_list.size() + (int)active_list.size() - (int)free_list.size();
		//if (active_list.size() % 10 == 0)
			//printf("Invalid: %i Active: %i Free: %i Total: %i\n", (int)invalid_list.size(), (int)active_list.size(), (int)free_list.size(), SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE);
	} else {
		//printf("Invalid: %i Log: %i Data: %i Free: %i Total: %i\n", (int)invalid_list.size(), log_active, data_active, (int)free_list.size(), SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE);
		used = (int)invalid_list.size() + (int)log_active + (int)data_active - (int)free_list.size();
	}
	float total = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE;
	float ratio = used/total;

	if (ratio < 0.90) // Magic number
		return;

	uint num_to_erase = 5; // More Magic!

	// First step and least expensive is to go though invalid list.
	while (num_to_erase != 0 && invalid_list.size() != 0)
	{
		Event erase_event = Event(ERASE, event.get_logical_address(), 1, event.get_start_time());
		erase_event.set_address(Address(invalid_list.back()->get_physical_address(), BLOCK));
		ftl.controller.issue(erase_event);
		event.incr_time_taken(erase_event.get_time_taken());
		//event.consolidate_metaevent(erase_event);

		free_list.push_back(invalid_list.back());
		invalid_list.pop_back();

		//printf("Erasing address: %lu Block: %u\n", address.get_linear_address(), address.block);
		num_to_erase--;
		ftl.controller.stats.numFTLErase++;
	}

	num_insert_events++;


	if (FTL_IMPLEMENTATION == IMPL_DFTL || FTL_IMPLEMENTATION == IMPL_BIMODAL)
	{
		// Then go though the active blocks via the priority queue of active pages.
		// We limit it to page-mapping algorithms.
		if (num_insert_events % (BLOCK_SIZE*100) == 0)
			std::sort(active_list.begin(), active_list.end(), &block_comparitor); // Do a full sort
		else if (active_list.size() > num_to_erase*2)
			std::sort(active_list.begin(), active_list.begin()+num_to_erase*2, &block_comparitor); // Only sort the beginning of the list.

		while (num_to_erase != 0 && active_list.size() > 1)
		{
			Block *blockErase = active_list.front();

			// If there is no gain, then don't move the pages.
			if (blockErase->get_pages_invalid() == 0)
			{
				num_to_erase--;
				continue;
			}


			if (blockErase->get_physical_address() == event.get_address().get_linear_address() - event.get_address().get_linear_address() % BLOCK_SIZE)
			{
				blockErase = active_list[1];
				active_list.erase(active_list.begin()+1);
			} else
				active_list.erase(active_list.begin());

			// Let the FTL handle cleanup of the block.
			ftl.cleanup_block(event, blockErase);

			// Create erase event and attach to current event queue.
			Event erase_event = Event(ERASE, event.get_logical_address(), 1, event.get_start_time());
			erase_event.set_address(Address(blockErase->get_physical_address(), BLOCK));

			free_list.push_back(blockErase);

			// Execute erase
			ftl.controller.issue(erase_event);

			event.incr_time_taken(erase_event.get_time_taken());
			//event.consolidate_metaevent(erase_event);

			num_to_erase--;
			ftl.controller.stats.numFTLErase++;
		}
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

	if (FTL_IMPLEMENTATION == IMPL_DFTL || FTL_IMPLEMENTATION == IMPL_BIMODAL)
		active_list.push_back(ftl.get_block_pointer(address));

	return address;
}

void Block_manager::erase_and_invalidate(Event &event, Address &address, block_type btype)
{
	Event erase_event = Event(ERASE, event.get_logical_address(), 1, event.get_start_time()+event.get_time_taken());

	Block *block = ftl.get_block_pointer(address);

	free_list.push_back(block);

	if (FTL_IMPLEMENTATION >= IMPL_DFTL)
	{
		std::vector<Block*>::iterator result = std::find(active_list.begin(), active_list.end(), block);
		if (result != active_list.end())
			active_list.erase(result);
	}

	//printf("Erasing address: %lu Block: %u\n", address.get_linear_address(), address.block);

	erase_event.set_address(address);

	ftl.controller.issue(erase_event);
	//event.consolidate_metaevent(erase_event);
	event.incr_time_taken(erase_event.get_time_taken());
	ftl.controller.stats.numFTLErase++;

	switch (btype)
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

int Block_manager::get_num_free_blocks()
{
	if (simpleCurrentFree < max_blocks*BLOCK_SIZE)
		return (simpleCurrentFree / BLOCK_SIZE) + free_list.size();
	else
		return free_list.size();
}

void Block_manager::simulate_map_write(Event &events)
{
//	Event writeEvent = Event(WRITE, events.get_logical_address(), 1, events.get_start_time()+events.get_time_taken());
//	Event eraseEvent = Event(ERASE, events.get_logical_address(), 1, events.get_start_time()+events.get_time_taken());
//
//	//writeEvent.set_address(Address(map_offset + directoryCurrentPage, PAGE));
//	writeEvent.set_address(Address(0, PAGE));
//	writeEvent.set_noop(true);
//
//	if (++directoryCurrentPage % BLOCK_SIZE == 0)
//	{
//		if (directoryCurrentPage == max_map_pages)
//			directoryCurrentPage = 0;
//
//		//Address eraseAddress = Address(map_offset + directoryCurrentPage, BLOCK);
//		eraseEvent.set_address(Address(0, PAGE));
//		eraseEvent.set_noop(true);
//
//		writeEvent.set_next(eraseEvent);
//
//		// Statistics
//		ftl.controller.stats.numGCErase++;
//	}
//
//	if (ftl.controller.issue(writeEvent) == FAILURE)
//		printf("Issue Error\n");
//
//	printf("%f\n",writeEvent.get_time_taken());
//	events.consolidate_metaevent(writeEvent);
//
//	// Statistics
//	ftl.controller.stats.numGCWrite++;
}
void Block_manager::simulate_map_read(Event &events)
{
//	ulong inside_block = events.get_address().get_linear_address() / BLOCK_SIZE;
//	if (!(directoryCachedPage >= inside_block && (directoryCachedPage + map_space_capacity) > inside_block))
//	{
//		Event readEvent = Event(READ, events.get_logical_address(), 1, events.get_start_time()+events.get_time_taken());
//		readEvent.set_address(Address(0, PAGE));
//		readEvent.set_noop(true);
//
//		ftl.controller.issue(readEvent);
//
//		events.consolidate_metaevent(readEvent);
//
//		ftl.controller.stats.numGCRead++;
//	}
}
