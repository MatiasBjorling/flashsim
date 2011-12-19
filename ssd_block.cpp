/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_block.cpp is part of FlashSim. */

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

/* Block class
 * Brendan Tauras 2009-10-26
 *
 * The block is the data storage hardware unit where erases are implemented.
 * Blocks maintain wear statistics for the FTL. */

#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Block::Block(const Plane &parent, uint block_size, ulong erases_remaining, double erase_delay, long physical_address):
	pages_invalid(0),
	physical_address(physical_address),
	size(block_size),

	/* use a const pointer (Page * const data) to use as an array
	 * but like a reference, we cannot reseat the pointer */
	data((Page *) malloc(block_size * sizeof(Page))),
	parent(parent),
	pages_valid(0),

	state(FREE),

	/* set erases remaining to BLOCK_ERASES to match Block constructor args 
	 * in Plane class
	 * this is the cheap implementation but can change to pass through classes */
	erases_remaining(erases_remaining),

	/* assume hardware created at time 0 and had an implied free erasure */
	last_erase_time(0.0),
	erase_delay(erase_delay),

	modification_time(-1)

{
	uint i;

	if(erase_delay < 0.0)
	{
		fprintf(stderr, "Block warning: %s: constructor received negative erase delay value\n\tsetting erase delay to 0.0\n", __func__);
		erase_delay = 0.0;
	}

	/* new cannot initialize an array with constructor args so
	 * 	malloc the array
	 * 	then use placement new to call the constructor for each element
	 * chose an array over container class so we don't have to rely on anything
	 * 	i.e. STL's std::vector */
	/* array allocated in initializer list:
	 * data = (Page *) malloc(size * sizeof(Page)); */
	if(data == NULL){
		fprintf(stderr, "Block error: %s: constructor unable to allocate Page data\n", __func__);
		exit(MEM_ERR);
	}

	for(i = 0; i < size; i++)
		(void) new (&data[i]) Page(*this, PAGE_READ_DELAY, PAGE_WRITE_DELAY);

	// Creates the active cost structure in the block manager.
	// It assumes that it is created lineary.
	Block_manager::instance()->cost_insert(this);

	return;
}

Block::~Block(void)
{
	assert(data != NULL);
	uint i;
	/* call destructor for each Page array element
	 * since we used malloc and placement new */
	for(i = 0; i < size; i++)
		data[i].~Page();
	free(data);
	return;
}

enum status Block::read(Event &event)
{
	assert(data != NULL);
	return data[event.get_address().page]._read(event);
}

enum status Block::write(Event &event)
{
	assert(data != NULL);
	enum status ret = data[event.get_address().page]._write(event);

	if(event.get_noop() == false)
	{
		pages_valid++;
		state = ACTIVE;
		modification_time = event.get_start_time();

		Block_manager::instance()->update_block(this);
	}
	return ret;
}

enum status Block::replace(Event &event)
{
	invalidate_page(event.get_replace_address().page);
	return SUCCESS;
}

/* updates Event time_taken
 * sets Page statuses to EMPTY
 * updates last_erase_time and erases_remaining
 * returns 1 for success, 0 for failure */
enum status Block::_erase(Event &event)
{
	assert(data != NULL && erase_delay >= 0.0);
	uint i;

	if (!event.get_noop())
	{
		if(erases_remaining < 1)
		{
			fprintf(stderr, "Block error: %s: No erases remaining when attempting to erase\n", __func__);
			return FAILURE;
		}

		for(i = 0; i < size; i++)
		{
			//assert(data[i].get_state() == INVALID);
			data[i].set_state(EMPTY);
		}


		event.incr_time_taken(erase_delay);
		last_erase_time = event.get_start_time() + event.get_time_taken();
		erases_remaining--;
		pages_valid = 0;
		pages_invalid = 0;
		state = FREE;

		Block_manager::instance()->update_block(this);
	}

	return SUCCESS;
}

const Plane &Block::get_parent(void) const
{
	return parent;
}

ssd::uint Block::get_pages_valid(void) const
{
	return pages_valid;
}

ssd::uint Block::get_pages_invalid(void) const
{
	return pages_invalid;
}


enum block_state Block::get_state(void) const
{
	return state;
}

enum page_state Block::get_state(uint page) const
{
	assert(data != NULL && page < size);
	return data[page].get_state();
}

enum page_state Block::get_state(const Address &address) const
{
   assert(data != NULL && address.page < size && address.valid >= BLOCK);
   return data[address.page].get_state();
}

double Block::get_last_erase_time(void) const
{
	return last_erase_time;
}

ssd::ulong Block::get_erases_remaining(void) const
{
	return erases_remaining;
}

ssd::uint Block::get_size(void) const
{
	return size;
}

void Block::invalidate_page(uint page)
{
	assert(page < size);

	if (data[page].get_state() == INVALID )
		return;

	//assert(data[page].get_state() == VALID);

	data[page].set_state(INVALID);

	pages_invalid++;

	Block_manager::instance()->update_block(this);

	/* update block state */
	if(pages_invalid >= size)
		state = INACTIVE;
	else if(pages_valid > 0 || pages_invalid > 0)
		state = ACTIVE;
	else
		state = FREE;

	return;
}

double Block::get_modification_time(void) const
{
	return modification_time;
}

/* method to find the next usable (empty) page in this block
 * method is called by write and erase methods and in Plane::get_next_page() */
enum status Block::get_next_page(Address &address) const
{
	uint i;

	for(i = 0; i < size; i++)
	{
		if(data[i].get_state() == EMPTY)
		{
			address.set_linear_address(i + physical_address - physical_address % BLOCK_SIZE, PAGE);
			return SUCCESS;
		}
	}
	return FAILURE;
}

long Block::get_physical_address(void) const
{
	return physical_address;
}

Block *Block::get_pointer(void)
{
	return this;
}

block_type Block::get_block_type(void) const
{
	return this->btype;
}

void Block::set_block_type(block_type value)
{
	this->btype = value;
}
