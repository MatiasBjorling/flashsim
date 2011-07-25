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

FtlImpl_DftlParent::MPage::MPage()
{
	this->vpn = -1;
	this->ppn = -1;
	this->create_ts = -1;
	this->modified_ts = -1;
}


FtlImpl_DftlParent::FtlImpl_DftlParent(Controller &controller):
	FtlParent(controller)
{
	addressPerPage = 0;

	currentDataPage = -1;
	currentTranslationPage = -1;

	// Detect required number of bits for logical address size
	addressSize = log(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE)/log(2);

	// Find required number of bits for block size
	addressPerPage = (PAGE_SIZE/ceil(addressSize / 8.0)); // 8 bits per byte

	printf("Total required bits for representation: Address size: %i Total per page: %i \n", addressSize, addressPerPage);

	totalCMTentries = CACHE_DFTL_LIMIT * addressPerPage;
	printf("Number of elements in Cached Mapping Table (CMT): %i\n", totalCMTentries);

	// Initialise block mapping table.
	uint ssdSize = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE;

	trans_map = new MPage[ssdSize];

	for (uint i=0;i<ssdSize;i++)
		trans_map[i].vpn = i;

	reverse_trans_map = new long[ssdSize];

	return;
}


void FtlImpl_DftlParent::select_victim_entry(FtlImpl_DftlParent::MPage &mpage)
{
	double evictPageTime = trans_map[cmt.begin()->first].modified_ts;
	long evictPage = cmt.begin()->first;

	// Retrieves the LRU CMT object
	std::map<long, bool>::iterator i = cmt.begin();
	while (i != cmt.end())
	{
		mpage = trans_map[i->first];
		if (mpage.ppn != -1 && evictPageTime >= mpage.modified_ts)
			evictPage = i->first;

		++i;
	}

	printf("done %li\n",evictPage);

	mpage = trans_map[evictPage];
}

void FtlImpl_DftlParent::remove_victims(FtlImpl_DftlParent::MPage &mpage)
{

}

void FtlImpl_DftlParent::consult_GTD(long dlpn, Event &event)
{
	// Simulate that we goto translation map and read the mapping page.
	Event readEvent = Event(READ, event.get_logical_address(), 1, event.get_start_time());
	readEvent.set_address(Address(1, PAGE));
	readEvent.set_noop(true);

	controller.issue(readEvent);
	//event.consolidate_metaevent(readEvent);
	event.incr_time_taken(readEvent.get_time_taken());
	controller.stats.numFTLRead++;
}

void FtlImpl_DftlParent::reset_MPage(FtlImpl_DftlParent::MPage &mpage)
{
	mpage.create_ts = -1;
	mpage.modified_ts = -1;
}

bool FtlImpl_DftlParent::lookup_CMT(long dlpn, Event &event)
{
	if (cmt.find(dlpn) == cmt.end())
		return false;

	event.incr_time_taken(RAM_READ_DELAY);
	controller.stats.numMemoryRead++;

	return true;
}

long FtlImpl_DftlParent::get_free_translation_page()
{
	if (currentTranslationPage == -1 || currentTranslationPage % BLOCK_SIZE == BLOCK_SIZE -1)
		currentTranslationPage = manager.get_free_block(LOG).get_linear_address();
	else
		currentTranslationPage++;

	return currentTranslationPage;
}

long FtlImpl_DftlParent::get_free_data_page()
{
	if (currentDataPage == -1 || currentDataPage % BLOCK_SIZE == BLOCK_SIZE -1)
		currentDataPage = manager.get_free_block(DATA).get_linear_address();
	else
		currentDataPage++;

	return currentDataPage;
}

FtlImpl_DftlParent::~FtlImpl_DftlParent(void)
{
	delete[] trans_map;
	delete[] reverse_trans_map;
}

void FtlImpl_DftlParent::resolve_mapping(Event &event, bool isWrite)
{
	uint dlpn = event.get_logical_address();
	/* 1. Lookup in CMT if the mapping exist
	 * 2. If, then serve
	 * 3. If not, then goto GDT, lookup page
	 * 4. If CMT full, evict a page
	 * 5. Add mapping to CMT
	 */
	if (lookup_CMT(event.get_logical_address(), event))
	{
		controller.stats.numCacheHits++;

		if (isWrite)
		{
			trans_map[dlpn].modified_ts = event.get_start_time();

			// Inform the ssd model that it should invalidate the previous page.
			Address killAddress = Address(trans_map[dlpn].ppn, PAGE);
			event.set_replace_address(killAddress);
		}
	} else {
		controller.stats.numCacheFaults++;
		consult_GTD(dlpn, event);

		if (isWrite)
		{
			trans_map[dlpn].create_ts = event.get_start_time();
			trans_map[dlpn].modified_ts = event.get_start_time();
		}

		if (cmt.size() == totalCMTentries)
		{
			// Find page to evict
			MPage evictPage;
			select_victim_entry(evictPage);

			if (evictPage.create_ts != evictPage.modified_ts)
			{
				// Evict page
				// Inform the ssd model that it should invalidate the previous page.
				// Calculate the start address of the translation page.
				int vpnBase = evictPage.vpn - evictPage.vpn % addressPerPage;

				for (int i=vpnBase;i<vpnBase+addressPerPage;i++)
						trans_map[i].create_ts = trans_map[i].modified_ts;

				// Simulate the write to translate page
				Event write_event = Event(WRITE, event.get_logical_address(), 1, event.get_start_time());
				write_event.set_address(Address(0, PAGE));
				write_event.set_noop(true);

				controller.issue(write_event);
				event.incr_time_taken(write_event.get_time_taken());
				//event.consolidate_metaevent(write_event);
			}

			// Remove page from cache.
			reset_MPage(evictPage);
			cmt.erase(evictPage.vpn);
		}

		cmt[dlpn] = true;
	}
}

void FtlImpl_DftlParent::update_translation_map(long lpn, long ppn)
{
	trans_map[lpn].ppn = ppn;
	reverse_trans_map[ppn] = lpn;
}
