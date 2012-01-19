/* Copyright 2011 Matias Bjørling */

/* dftp_ftl.cpp  */

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

/* Implementation of the DFTL described in the paper
 * "DFTL: A Flasg Translation Layer Employing Demand-based Selective Caching og Page-level Address Mappings"
 *
 * Global Mapping Table GMT
 * Global Translation Directory GTD (Maintained in memory)
 * Cached Mapping Table CMT (Uses LRU to pick victim)
 *
 * Dlpn/Dppn Data Logical/Physical Page Number
 * Mlpn/Mppn Translation Logical/Physical Page Number
 */


#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <queue>
#include <iostream>
#include "../ssd.h"

using namespace ssd;

FtlImpl_Dftl::FtlImpl_Dftl(Controller &controller):
	FtlImpl_DftlParent(controller)
{
	uint ssdSize = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;
	printf("Total size to map: %uKB\n", ssdSize * PAGE_SIZE / 1024);
	printf("Using DFTL.\n");
	return;
}

FtlImpl_Dftl::~FtlImpl_Dftl(void)
{
	return;
}

enum status FtlImpl_Dftl::read(Event &event)
{
	uint dlpn = event.get_logical_address();

	resolve_mapping(event, false);
	MPage current = trans_map[dlpn];
	if (current.ppn == -1)
	{
		event.set_address(Address(0, PAGE));
		event.set_noop(true);
	}
	else
		event.set_address(Address(current.ppn, PAGE));


	controller.stats.numFTLRead++;

	return controller.issue(event);
}

enum status FtlImpl_Dftl::write(Event &event)
{
	uint dlpn = event.get_logical_address();

	resolve_mapping(event, true);

	// Important order. As get_free_data_page might change current.
	long free_page = get_free_data_page(event);

	MPage current = trans_map[dlpn];

	Address a = Address(current.ppn, PAGE);
	if (current.ppn != -1)
		event.set_replace_address(a);

	update_translation_map(current, free_page);
	trans_map.replace(trans_map.begin()+dlpn, current);

	Address b = Address(free_page, PAGE);
	event.set_address(b);

	controller.stats.numFTLWrite++;

	return controller.issue(event);
}

enum status FtlImpl_Dftl::trim(Event &event)
{
	uint dlpn = event.get_logical_address();

	event.set_address(Address(0, PAGE));

	MPage current = trans_map[dlpn];

	if (current.ppn != -1)
	{
		Address address = Address(current.ppn, PAGE);
		Block *block = controller.get_block_pointer(address);
		block->invalidate_page(address.page);

		evict_specific_page_from_cache(event, dlpn);

		update_translation_map(current, -1);

		trans_map.replace(trans_map.begin()+dlpn, current);
	}

	controller.stats.numFTLTrim++;

	return controller.issue(event);
}

void FtlImpl_Dftl::cleanup_block(Event &event, Block *block)
{
	std::map<long, long> invalidated_translation;
	/*
	 * 1. Copy only valid pages in the victim block to the current data block
	 * 2. Invalidate old pages
	 * 3. mark their corresponding translation pages for update
	 */
	for (uint i=0;i<BLOCK_SIZE;i++)
	{
		assert(block->get_state(i) != EMPTY);
		// When valid, two events are create, one for read and one for write. They are chained and the controller are
		// called to execute them. The execution time is then added to the real event.
		if (block->get_state(i) == VALID)
		{
			// Set up events.
			Event readEvent = Event(READ, event.get_logical_address(), 1, event.get_start_time());
			readEvent.set_address(Address(block->get_physical_address()+i, PAGE));

			// Execute read event
			if (controller.issue(readEvent) == FAILURE)
				printf("Data block copy failed.");

			// Get new address to write to and invalidate previous
			Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time()+readEvent.get_time_taken());
			Address dataBlockAddress = Address(get_free_data_page(event, false), PAGE);

			writeEvent.set_address(dataBlockAddress);

			writeEvent.set_replace_address(Address(block->get_physical_address()+i, PAGE));

			// Setup the write event to read from the right place.
			writeEvent.set_payload((char*)page_data + (block->get_physical_address()+i) * PAGE_SIZE);

			if (controller.issue(writeEvent) == FAILURE)
				printf("Data block copy failed.");

			event.incr_time_taken(writeEvent.get_time_taken() + readEvent.get_time_taken());

			// Update GTD
			long dataPpn = dataBlockAddress.get_linear_address();

			// vpn -> Old ppn to new ppn
			//printf("%li Moving %li to %li\n", reverse_trans_map[block->get_physical_address()+i], block->get_physical_address()+i, dataPpn);
			invalidated_translation[reverse_trans_map[block->get_physical_address()+i]] = dataPpn;

			// Statistics
			controller.stats.numFTLRead++;
			controller.stats.numFTLWrite++;
			controller.stats.numWLRead++;
			controller.stats.numWLWrite++;
			controller.stats.numMemoryRead++; // Block->get_state(i) == VALID
			controller.stats.numMemoryWrite =+ 3; // GTD Update (2) + translation invalidate (1)
		}
	}

	/*
	 * Perform batch update on the marked translation pages
	 * 1. Update GDT and CMT if necessary.
	 * 2. Simulate translation page updates.
	 */

	std::map<long, bool> dirtied_translation_pages;

	for (std::map<long, long>::const_iterator i = invalidated_translation.begin(); i!=invalidated_translation.end(); ++i)
	{
		long real_vpn = (*i).first;
		long newppn = (*i).second;

		// Update translation map ( it also updates the CMT, as it is stored inside the GDT )
		MPage current = trans_map[real_vpn];

		update_translation_map(current, newppn);

		if (current.cached)
			current.modified_ts = event.get_start_time();
		else
		{
			current.modified_ts = event.get_start_time();
			current.create_ts = event.get_start_time();
			current.cached = true;
			cmt++;
		}

		trans_map.replace(trans_map.begin()+real_vpn, current);
	}

}

void FtlImpl_Dftl::print_ftl_statistics()
{
	Block_manager::instance()->print_statistics();
}
