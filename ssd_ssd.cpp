/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_ssd.cpp is part of FlashSim. */

/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/

/* Ssd class
 * Brendan Tauras 2009-11-03
 *
 * The SSD is the single main object that will be created to simulate a real
 * SSD.  Creating a SSD causes all other objects in the SSD to be created.  The
 * event_arrive method is where events will arrive from DiskSim. */

#include <cmath>
#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

using namespace ssd;

/* use caution when editing the initialization list - initialization actually
 * occurs in the order of declaration in the class definition and not in the
 * order listed here */
Ssd::Ssd(uint ssd_size): 
	size(ssd_size), 
	controller(*this), 
	ram(RAM_READ_DELAY, RAM_WRITE_DELAY), 
	bus(size, BUS_CTRL_DELAY, BUS_DATA_DELAY, BUS_TABLE_SIZE, BUS_MAX_CONNECT), 

	/* use a const pointer (Package * const data) to use as an array
	 * but like a reference, we cannot reseat the pointer */
	data((Package *) malloc(ssd_size * sizeof(Package))), 

	/* set erases remaining to BLOCK_ERASES to match Block constructor args 
	 *	in Plane class
	 * this is the cheap implementation but can change to pass through classes */
	erases_remaining(BLOCK_ERASES), 

	/* assume all Planes are same so first one can start as least worn */
	least_worn(0), 

	/* assume hardware created at time 0 and had an implied free erasure */
	last_erase_time(0.0)
{
	uint i;

	/* new cannot initialize an array with constructor args so
	 *		malloc the array
	 *		then use placement new to call the constructor for each element
	 * chose an array over container class so we don't have to rely on anything
	 * 	i.e. STL's std::vector */
	/* array allocated in initializer list:
	 * data = (Package *) malloc(ssd_size * sizeof(Package)); */
	if(data == NULL){
		fprintf(stderr, "Ssd error: %s: constructor unable to allocate Package data\n", __func__);
		exit(MEM_ERR);
	}
	for (i = 0; i < ssd_size; i++)
	{
		(void) new (&data[i]) Package(*this, bus.get_channel(i), PACKAGE_SIZE, PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE*BLOCK_SIZE*i);
	}
	
	// Check for 32bit machine. We do not allow page data on 32bit machines.
	if (PAGE_ENABLE_DATA == 1 && sizeof(void*) == 4)
	{
		fprintf(stderr, "Ssd error: %s: The simulator requires a 64bit kernel when using data pages. Disabling data pages.\n", __func__);
		exit(MEM_ERR);
	}

	if (PAGE_ENABLE_DATA)
	{
		/* Allocate memory for data pages */
		ulong pageSize = ((ulong)(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE)) * (ulong)PAGE_SIZE;
#ifdef __APPLE__
		page_data = mmap(NULL, pageSize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
#else
		page_data = mmap64(NULL, pageSize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1 ,0);
#endif

		if (page_data == MAP_FAILED)
		{
			fprintf(stderr, "Ssd error: %s: constructor unable to allocate page data.\n", __func__);
			switch (errno)
			{
			case EACCES:
				break;
			}
			printf("%i\n",errno);
			exit(MEM_ERR);
		}
	}

	assert(VIRTUAL_BLOCK_SIZE > 0);
	assert(VIRTUAL_PAGE_SIZE > 0);

	return;
}

Ssd::~Ssd(void)
{
	/* explicitly call destructors and use free
	 * since we used malloc and placement new */
	for (uint i = 0; i < size; i++)
	{
		data[i].~Package();
	}
	free(data);
	ulong pageSize = ((ulong)(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE)) * (ulong)PAGE_SIZE;
	munmap(page_data, pageSize);

	return;
}

double Ssd::event_arrive(enum event_type type, ulong logical_address, uint size, double start_time)
{
	return event_arrive(type, logical_address, size, start_time, NULL);
}

/* This is the function that will be called by DiskSim
 * Provide the event (request) type (see enum in ssd.h),
 * 	logical_address (page number), size of request in pages, and the start
 * 	time (arrive time) of the request
 * The SSD will process the request and return the time taken to process the
 * 	request.  Remember to use the same time units as in the config file. */
double Ssd::event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, void *buffer)
{
	assert(start_time >= 0.0);

	if (VIRTUAL_PAGE_SIZE == 1)
		assert((long long int) logical_address <= (long long int) SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE);
	else
		assert((long long int) logical_address*VIRTUAL_PAGE_SIZE <= (long long int) SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE);

	/* allocate the event and address dynamically so that the allocator can
	 * handle efficiency issues for us */
	Event *event = NULL;

	if((event = new Event(type, logical_address , size, start_time)) == NULL)
	{
		fprintf(stderr, "Ssd error: %s: could not allocate Event\n", __func__);
		exit(MEM_ERR);
	}

	event->set_payload(buffer);

	if(controller.event_arrive(*event) != SUCCESS)
	{
		fprintf(stderr, "Ssd error: %s: request failed:\n", __func__);
		event -> print(stderr);
	}

	/* use start_time as a temporary for returning time taken to service event */
	start_time = event -> get_time_taken();
	delete event;
	return start_time;
}

/*
 * Returns a pointer to the global buffer of the Ssd.
 * It is up to the user to not read out of bound and only
 * read the intended size. i.e. the page size.
 */
void *Ssd::get_result_buffer()
{
	return global_buffer;
}

/* read write erase and merge should only pass on the event
 * 	the Controller should lock the bus channels
 * technically the Package is conceptual, but we keep track of statistics
 * 	and addresses with Packages, so send Events through Package but do not 
 * 	have Package do anything but update its statistics and pass on to Die */
enum status Ssd::read(Event &event)
{
	assert(data != NULL && event.get_address().package < size && event.get_address().valid >= PACKAGE);
	return data[event.get_address().package].read(event);
}

enum status Ssd::write(Event &event)
{
	assert(data != NULL && event.get_address().package < size && event.get_address().valid >= PACKAGE);
	return data[event.get_address().package].write(event);
}

enum status Ssd::replace(Event &event)
{
	assert(data != NULL && event.get_replace_address().package < size);
	if (event.get_replace_address().valid == PAGE)
		return data[event.get_replace_address().package].replace(event);
	else
		return SUCCESS;
}


enum status Ssd::erase(Event &event)
{
	assert(data != NULL && event.get_address().package < size && event.get_address().valid >= PACKAGE);
	enum status status = data[event.get_address().package].erase(event);

	/* update values if no errors */
	if (status == SUCCESS)
		update_wear_stats(event.get_address());
	return status;
}

enum status Ssd::merge(Event &event)
{
	assert(data != NULL && event.get_address().package < size && event.get_address().valid >= PACKAGE);
	return data[event.get_address().package].merge(event);
}

enum status Ssd::merge_replacement_block(Event &event)
{
	//assert(data != NULL && event.get_address().package < size && event.get_address().valid >= PACKAGE && event.get_log_address().valid >= PACKAGE);
	return SUCCESS;
}

/* add up the erases remaining for all packages in the ssd*/
ssd::ulong Ssd::get_erases_remaining(const Address &address) const
{
	assert (data != NULL);
	
	if (address.package < size && address.valid >= PACKAGE)
		return data[address.package].get_erases_remaining(address);
	else return erases_remaining;
}

void Ssd::update_wear_stats(const Address &address)
{
	assert(data != NULL);
	uint i;
	uint max_index = 0;
	ulong max = data[0].get_erases_remaining(address);
	for(i = 1; i < size; i++)
		if(data[i].get_erases_remaining(address) > max)
			max_index = i;
	least_worn = max_index;
	erases_remaining = max;
	last_erase_time = data[max_index].get_last_erase_time(address);
	return;
}

void Ssd::get_least_worn(Address &address) const
{
	assert(data != NULL && least_worn < size);
	address.package = least_worn;
	address.valid = PACKAGE;
	data[least_worn].get_least_worn(address);
	return;
}

double Ssd::get_last_erase_time(const Address &address) const
{
	assert(data != NULL);
	if(address.package < size && address.valid >= PACKAGE)
		return data[address.package].get_last_erase_time(address);
	else
		return last_erase_time;
}

enum page_state Ssd::get_state(const Address &address) const
{
	assert(data != NULL);
	assert(address.package < size && address.valid >= PACKAGE);
	return data[address.package].get_state(address);
}

enum block_state Ssd::get_block_state(const Address &address) const
{
	assert(data != NULL);
	assert(address.package < size && address.valid >= PACKAGE);
	return data[address.package].get_block_state(address);
}

void Ssd::get_free_page(Address &address) const
{
	assert(address.package < size && address.valid >= PACKAGE);
	data[address.package].get_free_page(address);
	return;
}

ssd::uint Ssd::get_num_free(const Address &address) const
{  
	return 0;
/* 	return data[address.package].get_num_free(address); */
}

ssd::uint Ssd::get_num_valid(const Address &address) const
{  
	assert(address.valid >= PACKAGE);
	return data[address.package].get_num_valid(address);
}

ssd::uint Ssd::get_num_invalid(const Address &address) const
{
	assert(address.valid >= PACKAGE);
	return data[address.package].get_num_invalid(address);
}

void Ssd::print_statistics()
{
	controller.stats.print_statistics();
}

void Ssd::reset_statistics()
{
	controller.stats.reset_statistics();
}

void Ssd::write_statistics(FILE *stream)
{
	controller.stats.write_statistics(stream);
}

void Ssd::print_ftl_statistics()
{
	controller.print_ftl_statistics();
}

void Ssd::write_header(FILE *stream)
{
	controller.stats.write_header(stream);
}

Block *Ssd::get_block_pointer(const Address & address)
{
	assert(address.valid >= PACKAGE);
	return data[address.package].get_block_pointer(address);
}

const Controller &Ssd::get_controller(void) const
{
	return controller;
}

/**
 * Returns the next ready time. The ready time is the latest point in time when one of the channels are ready to serve new requests.
 */
double Ssd::ready_at(void)
{
	double next_ready_time = std::numeric_limits<double>::max();

	for (int i=0;i<size;i++)
	{
		double ready_time = bus.get_channel(i).ready_time();

		if (ready_time != -1 && ready_time < next_ready_time)
				next_ready_time = ready_time;
	}

	if (next_ready_time == std::numeric_limits<double>::max())
		return -1;
	else
		return next_ready_time;
}
