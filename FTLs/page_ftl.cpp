/* Copyright 2011 Matias Bjørling */

/* page_ftl.cpp  */

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

/* Implements a very simple page-level FTL without merge */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../ssd.h"

using namespace ssd;

FtlImpl_Page::FtlImpl_Page(Controller &controller):
	FtlParent(controller)
{
	trim_map = new bool[NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE];

	numPagesActive = 0;

	return;
}

FtlImpl_Page::~FtlImpl_Page(void)
{

	return;
}

enum status FtlImpl_Page::read(Event &event)
{
	event.set_address(Address(0, PAGE));
	event.set_noop(true);

	controller.stats.numFTLRead++;

	return controller.issue(event);
}

enum status FtlImpl_Page::write(Event &event)
{
	event.set_address(Address(1, PAGE));
	event.set_noop(true);

	controller.stats.numFTLWrite++;

	if (numPagesActive == NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE)
	{
		numPagesActive -= BLOCK_SIZE;

		Event eraseEvent = Event(ERASE, event.get_logical_address(), 1, event.get_start_time());
		eraseEvent.set_address(Address(0, PAGE));

		if (controller.issue(eraseEvent) == FAILURE) printf("Erase failed");

		event.incr_time_taken(eraseEvent.get_time_taken());

		controller.stats.numFTLErase++;
	}

	numPagesActive++;


	return controller.issue(event);
}

enum status FtlImpl_Page::trim(Event &event)
{
	controller.stats.numFTLTrim++;

	uint dlpn = event.get_logical_address();

	if (!trim_map[event.get_logical_address()])
		trim_map[event.get_logical_address()] = true;

	// Update trim map and update block map if all pages are trimmed. i.e. the state are reseted to optimal.
	long addressStart = dlpn - dlpn % BLOCK_SIZE;
	bool allTrimmed = true;
	for (uint i=addressStart;i<addressStart+BLOCK_SIZE;i++)
	{
		if (!trim_map[i])
			allTrimmed = false;
	}

	if (allTrimmed)
	{
		Event eraseEvent = Event(ERASE, event.get_logical_address(), 1, event.get_start_time());
		eraseEvent.set_address(Address(0, PAGE));

		if (controller.issue(eraseEvent) == FAILURE) printf("Erase failed");

		event.incr_time_taken(eraseEvent.get_time_taken());

		for (uint i=addressStart;i<addressStart+BLOCK_SIZE;i++)
			trim_map[i] = false;

		controller.stats.numFTLErase++;

		numPagesActive -= BLOCK_SIZE;
	}

	return SUCCESS;
}
