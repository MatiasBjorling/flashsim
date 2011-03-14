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

Block_manager::Block_manager(Ftl &ftl)
{
	max_blocks = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE;
	this->max_log_blocks = max_blocks;
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
void Block_manager::get_page(Address &address)
{
	if (simpleCurrentFree < max_blocks)
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

void Block_manager::promote_block(block_type to_type)
{
	if (to_type == DATA)
	{
		data_active++;
		log_active--;
	} else if (to_type == LOG)
	{
		log_active++;
		data_active--;
	}
}


void Block_manager::print_statistics()
{
	printf("-----------------\n");
	printf("Block Statistics:\n");
	printf("-----------------\n");
	printf("Log blocks:  %i\n", log_active);
	printf("Data blocks: %i\n", data_active);
	printf("Free blocks: %i\n", (max_blocks - simpleCurrentFree) / BLOCK_SIZE + free_list.size());
	printf("Invalid blocks: %i\n", invalid_list.size());
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
	Event *eventOps = &event;
	while (eventOps->get_next() == NULL)
		eventOps = eventOps->get_next();

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
		get_page(address);
		data_active++;
		break;

	case LOG:
		if (log_active == max_log_blocks)
			throw std::bad_alloc();

		get_page(address);
		log_active++;
		break;
	}
	return address;
}


