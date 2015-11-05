/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_controller.cpp is part of FlashSim. */

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

/* Controller class
 *
 * Brendan Tauras 2009-11-03
 *
 * The controller accepts read/write requests through its event_arrive method
 * and consults the FTL regarding what to do by calling the FTL's read/write
 * methods.  The FTL returns an event list for the controller through its issue
 * method that the controller buffers in RAM and sends across the bus.  The
 * controller's issue method passes the events from the FTL to the SSD.
 *
 * The controller also provides an interface for the FTL to collect wear
 * information to perform wear-leveling.
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Controller::Controller(Ssd &parent):
	ssd(parent)
{
	switch (FTL_IMPLEMENTATION)
	{
	case 0:
		ftl = new FtlImpl_Page(*this);
		break;
	case 1:
		ftl = new FtlImpl_Bast(*this);
		break;
	case 2:
		ftl = new FtlImpl_Fast(*this);
		break;
	case 3:
		ftl = new FtlImpl_Dftl(*this);
		break;
	case 4:
		ftl = new FtlImpl_BDftl(*this);
		break;
	}
	return;
}

Controller::~Controller(void)
{
	delete ftl;
	return;
}

enum status Controller::event_arrive(Event &event)
{
	if(event.get_event_type() == READ)
		return ftl->read(event);
	else if(event.get_event_type() == WRITE)
		return ftl->write(event);
	else if(event.get_event_type() == TRIM)
		return ftl->trim(event);
	else
		fprintf(stderr, "Controller: %s: Invalid event type\n", __func__);
	return FAILURE;
}

enum status Controller::issue(Event &event_list)
{
	Event *cur;

	/* go through event list and issue each to the hardware
	 * stop processing events and return failure status if any event in the 
	 *    list fails */
	for(cur = &event_list; cur != NULL; cur = cur -> get_next()){
		if(cur -> get_size() != 1){
			fprintf(stderr, "Controller: %s: Received non-single-page-sized event from FTL.\n", __func__);
			return FAILURE;
		}
		else if(cur -> get_event_type() == READ)
		{
			assert(cur -> get_address().valid > NONE);
			if(ssd.bus.lock(cur -> get_address().package, cur -> get_start_time(), BUS_CTRL_DELAY, *cur) == FAILURE
				|| ssd.read(*cur) == FAILURE
				|| ssd.bus.lock(cur -> get_address().package, cur -> get_time_taken(), BUS_CTRL_DELAY + BUS_DATA_DELAY, *cur) == FAILURE
				|| ssd.ram.write(*cur) == FAILURE
				|| ssd.ram.read(*cur) == FAILURE
				|| ssd.replace(*cur) == FAILURE)
				return FAILURE;
		}
		else if(cur -> get_event_type() == WRITE)
		{
			assert(cur -> get_address().valid > NONE);
			if(ssd.bus.lock(cur -> get_address().package, cur -> get_start_time(), BUS_CTRL_DELAY + BUS_DATA_DELAY, *cur) == FAILURE
				|| ssd.ram.write(*cur) == FAILURE
				|| ssd.ram.read(*cur) == FAILURE
				|| ssd.write(*cur) == FAILURE
				|| ssd.replace(*cur) == FAILURE)
				return FAILURE;
		}
		else if(cur -> get_event_type() == ERASE)
		{
			assert(cur -> get_address().valid > NONE);
			if(ssd.bus.lock(cur -> get_address().package, cur -> get_start_time(), BUS_CTRL_DELAY, *cur) == FAILURE
				|| ssd.erase(*cur) == FAILURE)
				return FAILURE;
		}
		else if(cur -> get_event_type() == MERGE)
		{
			assert(cur -> get_address().valid > NONE);
			assert(cur -> get_merge_address().valid > NONE);
			if(ssd.bus.lock(cur -> get_address().package, cur -> get_start_time(), BUS_CTRL_DELAY, *cur) == FAILURE
				|| ssd.merge(*cur) == FAILURE)
				return FAILURE;
		}
		else if(cur -> get_event_type() == TRIM)
		{
			return SUCCESS;
		}
		else
		{
			fprintf(stderr, "Controller: %s: Invalid event type\n", __func__);
			return FAILURE;
		}
	}
	return SUCCESS;
}

void Controller::translate_address(Address &address)
{
	if (PARALLELISM_MODE != 1)
		return;
}

ssd::ulong Controller::get_erases_remaining(const Address &address) const
{
	assert(address.valid > NONE);
	return ssd.get_erases_remaining(address);
}

void Controller::get_least_worn(Address &address) const
{
	assert(address.valid > NONE);
	return ssd.get_least_worn(address);
}

double Controller::get_last_erase_time(const Address &address) const
{
	assert(address.valid > NONE);
	return ssd.get_last_erase_time(address);
}

enum page_state Controller::get_state(const Address &address) const
{
	assert(address.valid > NONE);
	return (ssd.get_state(address));
}

enum block_state Controller::get_block_state(const Address &address) const
{
	assert(address.valid > NONE);
	return (ssd.get_block_state(address));
}

void Controller::get_free_page(Address &address) const
{
	assert(address.valid > NONE);
	ssd.get_free_page(address);
	return;
}

ssd::uint Controller::get_num_free(const Address &address) const
{
	assert(address.valid > NONE);
	return ssd.get_num_free(address);
}

ssd::uint Controller::get_num_valid(const Address &address) const
{
	assert(address.valid > NONE);
	return ssd.get_num_valid(address);
}

ssd::uint Controller::get_num_invalid(const Address &address) const
{
	assert(address.valid > NONE);
	return ssd.get_num_invalid(address);
}

Block *Controller::get_block_pointer(const Address & address)
{
	return ssd.get_block_pointer(address);
}

const FtlParent &Controller::get_ftl(void) const
{
	return (*ftl);
}

void Controller::print_ftl_statistics()
{
	ftl->print_ftl_statistics();
}
