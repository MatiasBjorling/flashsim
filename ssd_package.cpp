/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_package.cpp is part of FlashSim. */

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

/* Package class
 * Brendan Tauras 2009-11-03
 *
 * The package is the highest level data storage hardware unit.  While the
 * package is a virtual component, events are passed through the package for
 * organizational reasons, including helping to simplify maintaining wear
 * statistics for the FTL. */

#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Package::Package(const ssd::Ssd &parent, Channel &channel, uint package_size, long physical_address):
	size(package_size),

	/* use a const pointer (Die * const data) to use as an array
	 * but like a reference, we cannot reseat the pointer */
	data((Die *) malloc(package_size * sizeof(Die))),
	parent(parent),

	/* assume all Dies are same so first one can start as least worn */
	least_worn(0),

	/* set erases remaining to BLOCK_ERASES to match Block constructor args 
	 * in Plane class
	 * this is the cheap implementation but can change to pass through classes */
	erases_remaining(BLOCK_ERASES),

	/* assume hardware created at time 0 and had an implied free erasure */
	last_erase_time(0.0)
{
	uint i;

	/* new cannot initialize an array with constructor args so
	 * 	malloc the array
	 * 	then use placement new to call the constructor for each element
	 * chose an array over container class so we don't have to rely on anything
	 * 	i.e. STL's std::vector */
	/* array allocated in initializer list:
	 *	data = (Die *) malloc(size * sizeof(Die)); */
	if(data == NULL){
		fprintf(stderr, "Package error: %s: constructor unable to allocate Die data\n", __func__);
		exit(MEM_ERR);
	}

	for(i = 0; i < size; i++)
		(void) new (&data[i]) Die(*this, channel, DIE_SIZE, physical_address+(DIE_SIZE*PLANE_SIZE*BLOCK_SIZE*i));

	return;
}

Package::~Package(void)
{
	assert(data != NULL);
	uint i;
	/* call destructor for each Block array element
	 * since we used malloc and placement new */
	for(i = 0; i < size; i++)
		data[i].~Die();
	free(data);
	return;
}

enum status Package::read(Event &event)
{
	assert(data != NULL && event.get_address().die < size && event.get_address().valid > PACKAGE);
	return data[event.get_address().die].read(event);
}

enum status Package::write(Event &event)
{
	assert(data != NULL && event.get_address().die < size && event.get_address().valid > PACKAGE);
	return data[event.get_address().die].write(event);
}

enum status Package::replace(Event &event)
{
	assert(data != NULL);
	return data[event.get_replace_address().die].replace(event);
}

enum status Package::erase(Event &event)
{
	assert(data != NULL && event.get_address().die < size && event.get_address().valid > PACKAGE);
	enum status status = data[event.get_address().die].erase(event);
	if(status == SUCCESS)
		update_wear_stats(event.get_address());
	return status;
}

enum status Package::merge(Event &event)
{
	assert(data != NULL && event.get_address().die < size && event.get_address().valid > PACKAGE);
	return data[event.get_address().die].merge(event);
}

const Ssd &Package::get_parent(void) const
{
	return parent;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
double Package::get_last_erase_time(const Address &address) const
{
	assert(data != NULL);
	if(address.valid > PACKAGE && address.die < size)
		return data[address.die].get_last_erase_time(address);
	else
		return last_erase_time;
}

/* if given a valid Die address, call the Die's method
 * else return local value */
ssd::ulong Package::get_erases_remaining(const Address &address) const
{
	assert(data != NULL);
	if(address.valid > PACKAGE && address.die < size)
		return data[address.die].get_erases_remaining(address);
	else
		return erases_remaining;
}

ssd::uint ssd::Package::get_num_invalid(const Address & address) const
{
	assert(address.valid >= DIE);
	return data[address.die].get_num_invalid(address);
}

/* Plane with the most erases remaining is the least worn */
void Package::update_wear_stats(const Address &address)
{
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

/* update given address -> package to least worn package */
void Package::get_least_worn(Address &address) const
{
	assert(least_worn < size);
	address.die = least_worn;
	address.valid = DIE;
	data[least_worn].get_least_worn(address);
	return;
}

enum page_state Package::get_state(const Address &address) const
{
	assert(data != NULL && address.die < size && address.valid >= PACKAGE);
	return data[address.die].get_state(address);
}

enum block_state Package::get_block_state(const Address &address) const
{
	assert(data != NULL && address.die < size && address.valid >= PACKAGE);
	return data[address.die].get_block_state(address);
}

void Package::get_free_page(Address &address) const
{
	assert(address.die < size && address.valid >= DIE);
	data[address.die].get_free_page(address);
	return;
}
ssd::uint Package::get_num_free(const Address &address) const
{
	assert(address.valid >= DIE);
	return data[address.die].get_num_free(address);
}

ssd::uint Package::get_num_valid(const Address &address) const
{
	assert(address.valid >= DIE);
	return data[address.die].get_num_valid(address);
}

Block *Package::get_block_pointer(const Address & address)
{
	assert(address.valid >= DIE);
	return data[address.die].get_block_pointer(address);
}
