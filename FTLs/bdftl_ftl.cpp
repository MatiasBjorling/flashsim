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
	uint ssdBlockSize = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE;
	block_map = new BPage[ssdBlockSize];
	trim_map = new bool[ssdBlockSize*BLOCK_SIZE];

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

		if (block_map[dlbn].pbn != -1)
			event.set_address(Address(dppn, PAGE));
		else
		{
			event.set_address(Address(0, PAGE));
			event.set_noop(true);
		}
	} else { // DFTL lookup
		resolve_mapping(event, false);

		if (trans_map[dlpn].ppn != -1)
			event.set_address(Address(trans_map[dlpn].ppn, PAGE));
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
			block_map[dlbn].pbn = manager.get_free_block(DATA).get_linear_address();

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

				for (uint i=0;i<numPages;i++)
				{
					update_translation_map(startAdr+i, block_map[dlbn].pbn+i); //trans_map[startAdr+i].ppn = block_map[dlbn].pbn+i;
					trans_map[startAdr+i].create_ts = event.get_start_time();
					trans_map[startAdr+i].modified_ts = event.get_start_time();
					cmt[startAdr+i] = true;

					event.incr_time_taken(RAM_WRITE_DELAY);
					controller.stats.numMemoryWrite++;
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
		// Get next available data page
		if (inuseBlock == NULL)
		{
			// DFTL lookup
			resolve_mapping(event, true);
			update_translation_map(dlpn, get_free_data_page()); //trans_map[dlpn].ppn = get_free_data_page();
		} else {
			Address address;
			if (inuseBlock->get_next_page(address) == SUCCESS)
			{
				update_translation_map(dlpn, address.get_linear_address());
				//trans_map[dlpn].ppn = address.get_linear_address();
			} else {

					if (blockQueue.size() != 0)
					{
						inuseBlock = blockQueue.front();
						blockQueue.pop();
						if (inuseBlock->get_next_page(address) == SUCCESS)
						{
							update_translation_map(dlpn, address.get_linear_address());
							//trans_map[dlpn].ppn = address.get_linear_address();
						}
						else
						{
							printf("wee\n");
						}
					} else {
						inuseBlock = NULL;
						// DFTL lookup
						resolve_mapping(event, true);
						update_translation_map(dlpn, get_free_data_page()); //trans_map[dlpn].ppn = get_free_data_page();

					}


			}
		}

		// Finish DFTL logic
		event.set_address(Address(trans_map[dlpn].ppn, PAGE));
	}

	controller.stats.numMemoryRead += 3; // Block-level lookup + range check + optimal check
	event.incr_time_taken(RAM_READ_DELAY*3);
	controller.stats.numFTLWrite++; // Page reads

	// Insert garbage collection
	manager.insert_events(event);

	return controller.issue(event);
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
		uint dppn = block_map[dlbn].pbn + (dlpn % BLOCK_SIZE);
		Address address = Address(block_map[dlbn].pbn, PAGE);
		Block *block = controller.get_block_pointer(address);
		block->invalidate_page(address.page);

		if (block->get_state() == INACTIVE) // All pages invalid, force an erase. PTRIM style.
		{
			block_map[dlbn].pbn = -1;
			block_map[dlbn].nextPage = 0;
			manager.erase_and_invalidate(event, address, DATA);
		}
	} else { // DFTL lookup

		resolve_mapping(event, false);

		if (trans_map[dlpn].ppn != -1)
		{
			Address address = Address(trans_map[dlpn].ppn, PAGE);
			Block *block = controller.get_block_pointer(address);
			block->invalidate_page(address.page);

			// Update translation map to default values.
			update_translation_map(dlpn, -1); // trans_map[dlpn].ppn = -1;
			trans_map[dlpn].modified_ts = -1;
			trans_map[dlpn].modified_ts = -1;

			// Remove it from cache too.
			cmt.erase(dlpn);

			event.incr_time_taken(RAM_READ_DELAY);
			event.incr_time_taken(RAM_WRITE_DELAY);
			controller.stats.numMemoryRead++;
			controller.stats.numMemoryWrite++;

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
	uint cnt=0;
	for (uint i=0;i<BLOCK_SIZE;i++)
	{
		// When valid, two events are create, one for read and one for write. They are chained and the controller are
		// called to execute them. The execution time is then added to the real event.
		if (block->get_state(i) == VALID)
		{
			// Set up events.
			Event readEvent = Event(READ, event.get_logical_address(), 1, event.get_start_time());
			readEvent.set_address(Address(block->get_physical_address()+i, PAGE));

			if (controller.issue(readEvent) == FAILURE) printf("Data block copy failed.");
			//event.consolidate_metaevent(readEvent);

			// Setup the write event to read from the right place.
			Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time()+readEvent.get_time_taken());
			writeEvent.set_payload((char*)page_data + (block->get_physical_address()+i) * PAGE_SIZE);
			// Get new address to write to and invalidate previous
			Address dataBlockAddress = Address(get_free_data_page(), PAGE);
			writeEvent.set_address(dataBlockAddress);
			writeEvent.set_replace_address(Address(block->get_physical_address()+i, PAGE));

			// Execute
			if (controller.issue(writeEvent) == FAILURE) printf("Data block copy failed.");
			//event.consolidate_metaevent(writeEvent);
			event.incr_time_taken(writeEvent.get_time_taken() + readEvent.get_time_taken());

			// Update GTD (A reverse map is much better. But not implemented at this moment. Maybe I do it later.
			long dataPpn = dataBlockAddress.get_linear_address();
			invalidated_translation[reverse_trans_map[dataPpn]] = dataPpn;
//			for (uint j=0;j<SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE;j++)
//			{
//				if (trans_map[j].ppn == dataPpn)
//					invalidated_translation[trans_map[j].vpn] = dataPpn;
//			}

			// Statistics
			controller.stats.numGCRead++;
			controller.stats.numGCWrite++;
			controller.stats.numMemoryRead++; // Block->get_state(i) == VALID
			controller.stats.numMemoryWrite =+ 3; // GTD Update (2) + translation invalidate (1)

			cnt++;
		}
	}

	printf("GCed %u valid data pages.\n", cnt);

	/*
	 * Perform batch update on the marked translation pages
	 * 1. Update GDT and CMT if necessary.
	 * 2. Simulate translation page updates.
	 */

	std::map<long, bool> dirtied_translation_pages;

	for (std::map<long, long>::const_iterator i = invalidated_translation.begin(); i!=invalidated_translation.end(); ++i)
	{
		long ppn = (*i).first;
		long vpn = (*i).second;

		// Update translation map ( it also updates the CMT, as it is stored inside the GDT )
		update_translation_map(vpn, ppn); //trans_map[vpn].ppn = ppn;
		trans_map[vpn].modified_ts = event.get_start_time();

		dirtied_translation_pages[vpn/addressPerPage] = true;
	}

	for (std::map<long, bool>::const_iterator i = dirtied_translation_pages.begin(); i!=dirtied_translation_pages.end(); ++i)
	{
		Event readEvent = Event(READ, event.get_logical_address(), 1, event.get_start_time());

		// Set up events.
		readEvent.set_address(Address(0, PAGE));
		readEvent.set_noop(true);
		if (controller.issue(readEvent) == FAILURE) printf("Translation simulation block copy failed.");
		//event.consolidate_metaevent(readEvent);

		// Simulate the write.
		Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time()+readEvent.get_time_taken());
		writeEvent.set_address(Address(0, PAGE));
		writeEvent.set_noop(true);

		// Execute
		if (controller.issue(writeEvent) == FAILURE) printf("Translation simulation block copy failed.");
		//event.consolidate_metaevent(writeEvent);

		event.incr_time_taken(writeEvent.get_time_taken() + readEvent.get_time_taken());
	}
}

// Returns true if the next page is in a new block
bool FtlImpl_BDftl::block_next_new()
{
	return (currentDataPage == -1 || currentDataPage % BLOCK_SIZE == BLOCK_SIZE -1);
}

