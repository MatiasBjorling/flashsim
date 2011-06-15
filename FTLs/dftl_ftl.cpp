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
	uint ssdSize = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE;

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

	if (trans_map[dlpn].ppn == -1)
	{
		event.set_address(Address(0, PAGE));
		event.set_noop(true);
	}
	else
		event.set_address(Address(trans_map[dlpn].ppn, PAGE));


	controller.stats.numFTLRead++;

	// Insert garbage collection
	manager.insert_events(event);

	return controller.issue(event);
}

enum status FtlImpl_Dftl::write(Event &event)
{
	uint dlpn = event.get_logical_address();

	resolve_mapping(event, true);

	// Get next available data page
	long replace_page = trans_map[dlpn].ppn;
	trans_map[dlpn].ppn = get_free_data_page();

	event.set_address(Address(trans_map[dlpn].ppn, PAGE));

	if (replace_page != -1)
		event.set_replace_address(Address(replace_page, PAGE));

	controller.stats.numFTLWrite++;

	// Insert garbage collection
	manager.insert_events(event);

	return controller.issue(event);
}

enum status FtlImpl_Dftl::trim(Event &event)
{
	uint dlpn = event.get_logical_address();

	resolve_mapping(event, false);

	event.set_address(Address(0, PAGE));

	if (trans_map[dlpn].ppn != -1)
	{
		Address address = Address(trans_map[dlpn].ppn, PAGE);
		Block *block = controller.get_block_pointer(address);
		block->invalidate_page(address.page);
		trans_map[dlpn].ppn = -1;
		trans_map[dlpn].modified_ts = -1;
		trans_map[dlpn].modified_ts = -1;
	}

	controller.stats.numFTLTrim++;

	// Insert garbage collection
	manager.insert_events(event);

	return controller.issue(event);
}

void FtlImpl_Dftl::cleanup_block(Event &event, Block *block)
{
	if (block->get_block_type() == LOG)
	{
		/*
		 * Copy only valid pages in the victim block to the Current Translation Block
		 * Invalidate old pages
		 * Update GTD
		 */
		for (uint i=0;i<BLOCK_SIZE;i++)
		{
			if (block->get_state(i) == VALID)
			{

			}
		}

	}
	else // Data block
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
				Event readEvent = Event(READ, event.get_logical_address(), 1, event.get_start_time());
				Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time()+event.get_time_taken());

				// Set up events.
				readEvent.set_address(Address(block->get_physical_address()+i, PAGE));
				readEvent.set_next(writeEvent);

				// Setup the write event to read from the right place.
				writeEvent.set_payload((char*)page_data + (block->get_physical_address()+i) * PAGE_SIZE);

				// Get new address to write to and invalidate previous
				Address dataBlockAddress = Address(get_free_data_page(), PAGE);
				writeEvent.set_address(dataBlockAddress);
				writeEvent.set_replace_address(Address(block->get_physical_address()+i, PAGE));

				//printf("Write address %i to %i\n", readEvent.get_address().get_linear_address(), writeEvent.get_address().get_linear_address());

				// Update GTD (A reverse map is much better. But not implemented at this moment. Maybe I do it later.
				long dataPpn = dataBlockAddress.get_linear_address();
				for (uint j=0;j<SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE;j++)
				{
					if (trans_map[j].ppn == dataPpn)
						invalidated_translation[trans_map[j].vpn] = dataPpn;
				}

				// Execute
				if (controller.issue(readEvent) == FAILURE)
					printf("Data block copy failed.");

				event.consolidate_metaevent(readEvent);

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
			trans_map[vpn].ppn = ppn;
			trans_map[vpn].modified_ts = event.get_start_time();

			dirtied_translation_pages[vpn/addressPerPage] = true;
		}

		for (std::map<long, bool>::const_iterator i = dirtied_translation_pages.begin(); i!=dirtied_translation_pages.end(); ++i)
		{
			Event readEvent = Event(READ, event.get_logical_address(), 1, event.get_start_time());
			Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time());

			// Set up events.
			readEvent.set_address(Address(0, PAGE));
			readEvent.set_noop(true);
			readEvent.set_next(writeEvent);

			// Simulate the write.
			writeEvent.set_address(Address(0, PAGE));
			writeEvent.set_noop(true);

			// Execute
			if (controller.issue(readEvent) == FAILURE)
				printf("Translation simulation block copy failed.");

			event.consolidate_metaevent(readEvent);
		}
	}

	return;
}
