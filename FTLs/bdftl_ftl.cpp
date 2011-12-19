/* Copyright 2011 Matias Bjørling */

/* bdftp_ftl.cpp  */

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

/* Implementation of BDFTL. A block-level optimization for DFTL.
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

FtlImpl_BDftl::BPage::BPage()
{
	this->pbn = -1;
	nextPage = 0;
	optimal = true;
}

FtlImpl_BDftl::FtlImpl_BDftl(Controller &controller):
	FtlImpl_DftlParent(controller)
{
	block_map = new BPage[NUMBER_OF_ADDRESSABLE_BLOCKS];
	trim_map = new bool[NUMBER_OF_ADDRESSABLE_BLOCKS*BLOCK_SIZE];

	inuseBlock = NULL;

	printf("Using BDFTL.\n");
}



FtlImpl_BDftl::~FtlImpl_BDftl(void)
{
	delete[] block_map;
	delete[] trim_map;
	return;
}

enum status FtlImpl_BDftl::read(Event &event)
{
	uint dlpn = event.get_logical_address();
	uint dlbn = dlpn / BLOCK_SIZE;

	// Block-level lookup
 	if (block_map[dlbn].optimal)
	{
		uint dppn = block_map[dlbn].pbn + (dlpn % BLOCK_SIZE);

		if (block_map[dlbn].pbn != (uint) -1)
			event.set_address(Address(dppn, PAGE));
		else
		{
			event.set_address(Address(0, PAGE));
			event.set_noop(true);
		}
	} else { // DFTL lookup
		resolve_mapping(event, false);

		MPage current = trans_map[dlpn];

		if (current.ppn != -1)
			event.set_address(Address(current.ppn, PAGE));
		else
		{
			event.set_address(Address(0, PAGE));
			event.set_noop(true);
		}
	}

	event.incr_time_taken(RAM_READ_DELAY*2);
	controller.stats.numMemoryRead += 2; // Block-level lookup + range check
	controller.stats.numFTLRead++; // Page read

	return controller.issue(event);
}


enum status FtlImpl_BDftl::write(Event &event)
{
	uint dlpn = event.get_logical_address();
	uint dlbn = dlpn / BLOCK_SIZE;
	bool handled = false;

	// Update trim map
	trim_map[dlpn] = false;

	// Block-level lookup
	if (block_map[dlbn].optimal)
	{
		// Optimised case for block level lookup

		// Get new block if necessary
		if (block_map[dlbn].pbn == -1u && dlpn % BLOCK_SIZE == 0)
			block_map[dlbn].pbn = Block_manager::instance()->get_free_block(DATA, event).get_linear_address();

		if (block_map[dlbn].pbn != -1u)
		{
			unsigned char dppn = dlpn % BLOCK_SIZE;
			if (block_map[dlbn].nextPage == dppn)
			{
				controller.stats.numMemoryWrite++; // Update next page
				event.incr_time_taken(RAM_WRITE_DELAY);
				event.set_address(Address(block_map[dlbn].pbn + dppn, PAGE));
				block_map[dlbn].nextPage++;
				handled = true;
			} else {
				/*
				 * Transfer the block to DFTL.
				 * 1. Get number of pages to write
				 * 2. Get start address for translation map
				 * 3. Write mappings to trans_map
				 * 4. Make block non-optimal
				 * 5. Add the block to the block queue to be used later
				 */

				// 1-3
				uint numPages = block_map[dlbn].nextPage;
				long startAdr = dlbn * BLOCK_SIZE;

				Block *b = controller.get_block_pointer(Address(startAdr, PAGE));

				for (uint i=0;i<numPages;i++)
				{
					//assert(b->get_state(i) != INVALID);

					if (b->get_state(i) != VALID)
						continue;

					MPage current = trans_map[startAdr + i];

					if (current.ppn != -1)
					{
						update_translation_map(current, block_map[dlbn].pbn+i);
						current.create_ts = event.get_start_time();
						current.modified_ts = event.get_start_time();
						current.cached = true;
						trans_map.replace(trans_map.begin()+startAdr+i, current);

						cmt++;

						event.incr_time_taken(RAM_WRITE_DELAY);
						controller.stats.numMemoryWrite++;
					}

				}

				// 4. Set block to non optimal
				event.incr_time_taken(RAM_WRITE_DELAY);
				controller.stats.numMemoryWrite++;
				block_map[dlbn].optimal = false;

				// 5. Add it to the queue to be used later.
				Block *block = controller.get_block_pointer(Address(block_map[dlbn].pbn, BLOCK));
				if (block->get_pages_valid() != BLOCK_SIZE)
				{
					if (inuseBlock == NULL)
						inuseBlock = block;
					else
						blockQueue.push(block);
				}


				controller.stats.numPageBlockToPageConversion++;
			}
		} else {
			block_map[dlbn].optimal = false;
		}
	}

	if (!handled)
	{
		// Important order. As get_free_data_page might change current.
		long free_page = get_free_biftl_page(event);
		resolve_mapping(event, true);

		MPage current = trans_map[dlpn];

		Address a = Address(current.ppn, PAGE);

		if (current.ppn != -1)
			event.set_replace_address(a);


		update_translation_map(current, free_page);
		trans_map.replace(trans_map.begin()+dlpn, current);

		// Finish DFTL logic
		event.set_address(Address(current.ppn, PAGE));
	}

	controller.stats.numMemoryRead += 3; // Block-level lookup + range check + optimal check
	event.incr_time_taken(RAM_READ_DELAY*3);
	controller.stats.numFTLWrite++; // Page writes

	return controller.issue(event);
}

long FtlImpl_BDftl::get_free_biftl_page(Event &event)
{
	// Important order. As get_free_data_page might change current.
	long free_page = -1;

	// Get next available data page
	if (inuseBlock == NULL)
	{
		// DFTL way
		free_page = get_free_data_page(event);
	} else {
		Address address;
		if (inuseBlock->get_next_page(address) == SUCCESS)
		{
			// Get page from biftl block space
			free_page = address.get_linear_address();
		}
		else if (blockQueue.size() != 0)
		{
			inuseBlock = blockQueue.front();
			blockQueue.pop();
			if (inuseBlock->get_next_page(address) == SUCCESS)
			{
				// Get page from the next block in the biftl block space
				free_page = address.get_linear_address();
			}
			else
			{
				assert(false);
			}
		} else {
			inuseBlock = NULL;
			// DFTL way
			free_page = get_free_data_page(event);
		}
	}

	assert(free_page != -1);

	return free_page;

}

enum status FtlImpl_BDftl::trim(Event &event)
{
	uint dlpn = event.get_logical_address();
	uint dlbn = dlpn / BLOCK_SIZE;

	// Update trim map
	trim_map[dlpn] = true;

	// Block-level lookup
	if (block_map[dlbn].optimal)
	{
		Address address = Address(block_map[dlbn].pbn+event.get_logical_address()%BLOCK_SIZE, PAGE);
		Block *block = controller.get_block_pointer(address);
		block->invalidate_page(address.page);

		if (block->get_state() == INACTIVE) // All pages invalid, force an erase. PTRIM style.
		{
			block_map[dlbn].pbn = -1;
			block_map[dlbn].nextPage = 0;
			Block_manager::instance()->erase_and_invalidate(event, address, DATA);
		}
	} else { // DFTL lookup

		MPage current = trans_map[dlpn];
		if (current.ppn != -1)
		{
			Address address = Address(current.ppn, PAGE);
			Block *block = controller.get_block_pointer(address);
			block->invalidate_page(address.page);

			evict_specific_page_from_cache(event, dlpn);

			// Update translation map to default values.
			update_translation_map(current, -1);
			trans_map.replace(trans_map.begin()+dlpn, current);

			event.incr_time_taken(RAM_READ_DELAY);
			event.incr_time_taken(RAM_WRITE_DELAY);
			controller.stats.numMemoryRead++;
			controller.stats.numMemoryWrite++;
		}

		// Update trim map and update block map if all pages are trimmed. i.e. the state are reseted to optimal.
		long addressStart = dlpn - dlpn % BLOCK_SIZE;
		bool allTrimmed = true;
		for (uint i=addressStart;i<addressStart+BLOCK_SIZE;i++)
		{
			if (!trim_map[i])
				allTrimmed = false;
		}

		controller.stats.numMemoryRead++; // Trim map looping

		if (allTrimmed)
		{
			block_map[dlbn].pbn = -1;
			block_map[dlbn].nextPage = 0;
			block_map[dlbn].optimal = true;
			controller.stats.numMemoryWrite++; // Update block_map.
		}
	}

	event.set_address(Address(0, PAGE));
	event.set_noop(true);

	event.incr_time_taken(RAM_READ_DELAY*2);
	controller.stats.numMemoryRead += 2; // Block-level lookup + range check
	controller.stats.numFTLTrim++; // Page trim

	return controller.issue(event);
}

void FtlImpl_BDftl::cleanup_block(Event &event, Block *block)
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

// Returns true if the next page is in a new block
bool FtlImpl_BDftl::block_next_new()
{
	return (currentDataPage == -1 || currentDataPage % BLOCK_SIZE == BLOCK_SIZE -1);
}

void FtlImpl_BDftl::print_ftl_statistics()
{
	printf("FTL Stats:\n");
	printf(" Blocks total: %i\n", NUMBER_OF_ADDRESSABLE_BLOCKS);

	int numOptimal = 0;
	for (uint i=0;i<NUMBER_OF_ADDRESSABLE_BLOCKS;i++)
	{
		BPage bp = block_map[i];
		if (bp.optimal)
		{
			printf("Optimal: %i\n", i);
			numOptimal++;
		}

	}

	printf(" Blocks optimal: %i\n", numOptimal);
	Block_manager::instance()->print_statistics();
}

