/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_plane.cpp is part of FlashSim. */

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

/* Plane class
 * Brendan Tauras 2009-11-03
 *
 * The plane is the data storage hardware unit that contains blocks.
 * Plane-level merges are implemented in the plane.  Planes maintain wear
 * statistics for the FTL. */

#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Plane::Plane(const Die &parent, uint plane_size, double reg_read_delay, double reg_write_delay, long physical_address):
	size(plane_size),

	/* use a const pointer (Block * const data) to use as an array
	 * but like a reference, we cannot reseat the pointer */
	data((Block *) malloc(size * sizeof(Block))),

	parent(parent),

	/* assume all Blocks are same so first one can start as least worn */
	least_worn(0),

	/* set erases remaining to BLOCK_ERASES to match Block constructor args */
	erases_remaining(BLOCK_ERASES),

	/* assume hardware created at time 0 and had an implied free erasure */
	last_erase_time(0.0),

	free_blocks(size)
{
	uint i;

	if(reg_read_delay < 0.0)
	{  
		fprintf(stderr, "Plane error: %s: constructor received negative register read delay value\n\tsetting register read delay to 0.0\n", __func__);
		reg_read_delay = 0.0;
	}
	else
		this -> reg_read_delay = reg_read_delay;

	if(reg_write_delay < 0.0)
	{  
		fprintf(stderr, "Plane error: %s: constructor received negative register write delay value\n\tsetting register write delay to 0.0\n", __func__);
		reg_write_delay = 0.0;
	}
	else
		this -> reg_write_delay = reg_write_delay;

	/* next page only uses the block, page, and valid fields of the address
	 *    object so we can ignore setting the other fields
	 * plane does not know about higher-level hardware organization, so we cannot
	 *    set the other fields anyway */
	next_page.block = 0;
	next_page.page = 0;
	next_page.valid = PAGE;

	/* new cannot initialize an array with constructor args so
	 * 	malloc the array
	 * 	then use placement new to call the constructor for each element
	 * chose an array over container class so we don't have to rely on anything
	 * 	i.e. STL's std::vector */
	/* array allocated in initializer list:
 	 * data = (Block *) malloc(size * sizeof(Block)); */
	if(data == NULL){
		fprintf(stderr, "Plane error: %s: constructor unable to allocate Block data\n", __func__);
		exit(MEM_ERR);
	}

	for(i = 0; i < size; i++)
	{
		(void) new (&data[i]) Block(*this, BLOCK_SIZE, BLOCK_ERASES, BLOCK_ERASE_DELAY,physical_address+(i*BLOCK_SIZE));
	}


	return;
}

Plane::~Plane(void)
{
	assert(data != NULL);
	uint i;
	/* call destructor for each Block array element
	 * since we used malloc and placement new */
	for(i = 0; i < size; i++)
		data[i].~Block();
	free(data);
	return;
}

enum status Plane::read(Event &event)
{
	assert(event.get_address().block < size && event.get_address().valid > PLANE);
	return data[event.get_address().block].read(event);
}

enum status Plane::write(Event &event)
{
	assert(event.get_address().block < size && event.get_address().valid > PLANE && next_page.valid >= BLOCK);

	enum block_state prev = data[event.get_address().block].get_state();

	status s = data[event.get_address().block].write(event);

	if(event.get_address().block == next_page.block)
		/* if all blocks in the plane are full and this function fails,
		 * the next_page address valid field will be set to PLANE */
		(void) get_next_page();

	if(prev == FREE && data[event.get_address().block].get_state() != FREE)
		free_blocks--;

	return s;
}

enum status Plane::replace(Event &event)
{
	assert(event.get_address().block < size);
	return data[event.get_replace_address().block].replace(event);
}


/* if no errors
 * 	updates last_erase_time if later time
 * 	updates erases_remaining if smaller value
 * returns 1 for success, 0 for failure */
enum status Plane::erase(Event &event)
{
	assert(event.get_address().block < size && event.get_address().valid > PLANE);
	enum status status = data[event.get_address().block]._erase(event);

	/* update values if no errors */
	if(status == 1)
	{
		update_wear_stats();
		free_blocks++;

		/* set next free page if plane was completely full */
		if(next_page.valid < PAGE)
			(void) get_next_page();
	}
	return status;
}

/* handle everything for a merge operation
 * 	address.block and address_merge.block must be valid
 * 	move event::address valid pages to event::address_merge empty pages
 * creates own events for resulting read/write operations
 * supports blocks that have different sizes */
enum status Plane::_merge(Event &event)
{
	assert(event.get_address().block < size && event.get_address().valid > PLANE);
	assert(reg_read_delay >= 0.0 && reg_write_delay >= 0.0);
	uint i;
	uint merge_count = 0;
	uint merge_avail = 0;
	uint num_merged = 0;
	double total_delay = 0;

	/* get and check address validity and size of blocks involved in the merge */
	const Address &address = event.get_address();
	const Address &merge_address = event.get_merge_address();
	assert(address.compare(merge_address) >= BLOCK);
	assert(address.block < size && merge_address.block < size);
	uint block_size = data[address.block].get_size();
	uint merge_block_size = data[merge_address.block].get_size();

	/* how many pages must be moved */
	for(i = 0; i < block_size; i++)
		if(data[address.block].get_state(i) == VALID)
			merge_count++;
	
	/* how many pages are available */
	for(i = 0; i < merge_block_size; i++)
		if(data[merge_address.block].get_state(i) == EMPTY)
			merge_avail++;

	/* fail if not enough space to do the merge */
	if(merge_count > merge_avail)
	{
		fprintf(stderr, "Plane error: %s: Not enough space to merge block %d into block %d\n", __func__, address.block, merge_address.block);
		return FAILURE;
	}

	/* create event classes to handle read and write events for the merge */
	Address read(address);
	Address write(merge_address);
	read.page = 0;
	read.valid = PAGE;
	write.page = 0;
	write.valid = PAGE;
	Event read_event(READ, 0, 1, event.get_start_time());
	Event write_event(WRITE, 0, 1, event.get_start_time());
	read_event.set_address(read);
	write_event.set_address(write);
	
	/* calculate merge delay and add to event time
	 * use i as an error counter */
	for(i = 0; num_merged < merge_count && read.page < block_size; read.page++)
	{
		/* find next page to read from */
		if(data[read.block].get_state(read.page) == VALID)
		{
			/* read from page and set status to invalid */
			if(data[read.block].read(read_event) == 0)
			{
				fprintf(stderr, "Plane error: %s: Read for merge block %d into %d failed\n", __func__, read.block, write.block);
				i++;
			}
			data[read.block].invalidate_page(read.page);

			/* get time taken for read and plane register write
			 * read event time will accumulate and be added at end */
			total_delay += reg_write_delay;

			/* keep advancing from last page written to */
			for(; write.page < merge_block_size; write.page++)
			{
				/* find next page to write to */
				if(data[write.block].get_state(write.page) == EMPTY)
				{
					/* write to page (page::_write() sets status to valid) */
					if(data[merge_address.block].write(write_event) == 0)
					{
						fprintf(stderr, "Plane error: %s: Write for merge block %d into %d failed\n", __func__, address.block, merge_address.block);
						i++;
					}

					/* get time taken for plane register read
					 * write event time will accumulate and be added at end */
					total_delay += reg_read_delay;
					num_merged++;
					break;
				}
			}
		}
	}
	total_delay += read_event.get_time_taken() + write_event.get_time_taken();
	event.incr_time_taken(total_delay);

	/* update next_page for the get_free_page method if we used the page */
	if(next_page.valid < PAGE)
		(void) get_next_page();

	if(i == 0)
		return SUCCESS;
	else
	{
		fprintf(stderr, "Plane error: %s: %u failures during merge operation\n", __func__, i);
		return FAILURE;
	}
}

ssd::uint Plane::get_size(void) const
{
	return size;
}

const Die &Plane::get_parent(void) const
{
	return parent;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
double Plane::get_last_erase_time(const Address &address) const
{
	assert(data != NULL);
	if(address.valid > PLANE && address.block < size)
		return data[address.block].get_last_erase_time();
	else
		return last_erase_time;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
ssd::ulong Plane::get_erases_remaining(const Address &address) const
{
	assert(data != NULL);
	if(address.valid > PLANE && address.block < size)
		return data[address.block].get_erases_remaining();
	else
		return erases_remaining;
}

/* Block with the most erases remaining is the least worn */
void Plane::update_wear_stats(void)
{
	uint i;
	uint max_index = 0;
	ulong max = data[0].get_erases_remaining();
	for(i = 1; i < size; i++)
		if(data[i].get_erases_remaining() > max)
			max_index = i;
	least_worn = max_index;
	erases_remaining = max;
	last_erase_time = data[max_index].get_last_erase_time();
	return;
}

/* update given address.block to least worn block */
void Plane::get_least_worn(Address &address) const
{
	assert(least_worn < size);
	address.block = least_worn;
	address.valid = BLOCK;
	return;
}

enum page_state Plane::get_state(const Address &address) const
{  
	assert(data != NULL && address.block < size && address.valid >= PLANE);
	return data[address.block].get_state(address);
}

enum block_state Plane::get_block_state(const Address &address) const
{
	assert(data != NULL && address.block < size && address.valid >= PLANE);
	return data[address.block].get_state();
}

/* update address to next free page in plane
 * error condition will result in (address.valid < PAGE) */
void Plane::get_free_page(Address &address) const
{
	assert(data[address.block].get_pages_valid() < BLOCK_SIZE);

	address.page = data[address.block].get_pages_valid();
	address.valid = PAGE;
	address.set_linear_address(address.get_linear_address()+ address.page - (address.get_linear_address()%BLOCK_SIZE));
	return;
}

/* internal method to keep track of the next usable (free or active) page in
 *    this plane
 * method is called by write and erase methods and calls Block::get_next_page()
 *    such that the get_free_page method can run in constant time */
enum status Plane::get_next_page(void)
{
	return SUCCESS;

	uint i;
	next_page.valid = PLANE;

	for(i = 0; i < size; i++)
	{
		if(data[i].get_state() != INACTIVE)
		{
			next_page.valid = BLOCK;
			if(data[i].get_next_page(next_page) == SUCCESS)
			{
				next_page.block = i;
				return SUCCESS;
			}
		}
	}
	return FAILURE;
}

/* free_blocks is updated in the write and erase methods */
ssd::uint Plane::get_num_free(const Address &address) const
{
	assert(address.valid >= PLANE);
	return free_blocks;
}

ssd::uint Plane::get_num_valid(const Address &address) const
{
	assert(address.valid >= PLANE);
	return data[address.block].get_pages_valid();
}

ssd::uint Plane::get_num_invalid(const Address & address) const
{
	assert(address.valid >= PLANE);
	return data[address.block].get_pages_invalid();
}

Block *Plane::get_block_pointer(const Address & address)
{
	assert(address.valid >= PLANE);
	return data[address.block].get_pointer();
}
