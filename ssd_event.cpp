/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_event.cpp is part of FlashSim. */

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

/* Event class
 * Brendan Tauras 2010-07-16
 *
 * Class to manage I/O requests as events for the SSD.  It was designed to keep
 * track of an I/O request by storing its type, addressing, and timing.  The
 * SSD class creates an instance for each I/O request it receives.
 */

#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

/* see "enum event_type" in ssd.h for details on event types */
Event::Event(enum event_type type, ulong logical_address, uint size, double start_time):
	start_time(start_time),
	time_taken(0.0),
	bus_wait_time(0.0),
	type(type),
	logical_address(logical_address),
	size(size),
	payload(NULL),
	next(NULL),
	noop(false)
{
	assert(start_time >= 0.0);
	return;
}

Event::~Event(void)
{
	return;
}

/* find the last event in the list to finish and use that event's finish time
 * 	to calculate time_taken
 * add bus_wait_time for all events in the list to bus_wait_time
 * all events in the list do not need to start at the same time
 * bus_wait_time can potentially exceed time_taken with long event lists
 * 	because bus_wait_time is a sum while time_taken is a max
 * be careful to only call this method once when the metaevent is finished */
void Event::consolidate_metaevent(Event &list)
{
	Event *cur;
	double max;
	double tmp;

	assert(start_time >= 0);

	/* find max time taken with respect to this event's start_time */
	max = start_time - list.start_time + list.time_taken;
	for(cur = list.next; cur != NULL; cur = cur -> next)
	{
		tmp = start_time - cur -> start_time + cur -> time_taken;
		if(tmp > max)
			max = tmp;
		bus_wait_time += cur -> get_bus_wait_time();
	}
	time_taken = max;

	assert(time_taken >= 0);
	assert(bus_wait_time >= 0);
	return;
}

ssd::ulong Event::get_logical_address(void) const
{
	return logical_address;
}

const Address &Event::get_address(void) const
{
	return address;
}

const Address &Event::get_merge_address(void) const
{
	return merge_address;
}

const Address &Event::get_log_address(void) const
{
	return log_address;
}

const Address &Event::get_replace_address(void) const
{
	return replace_address;
}

void Event::set_log_address(const Address &address)
{
	log_address = address;
}

ssd::uint Event::get_size(void) const
{
	return size;
}

enum event_type Event::get_event_type(void) const
{
	return type;
}

void Event::set_event_type(const enum event_type &type)
{
	this->type = type;
}

double Event::get_start_time(void) const
{
	assert(start_time >= 0.0);
	return start_time;
}

double Event::get_time_taken(void) const
{

	assert(time_taken >= 0.0);
	return time_taken;
}

double Event::get_bus_wait_time(void) const
{
	assert(bus_wait_time >= 0.0);
	return bus_wait_time;
}

bool Event::get_noop(void) const
{
	return noop;
}

Event *Event::get_next(void) const
{
	return next;
}

void Event::set_payload(void *payload)
{
	this->payload = payload;
}

void *Event::get_payload(void) const
{
	return payload;
}

void Event::set_address(const Address &address)
{
	this -> address = address;
	return;
}

void Event::set_merge_address(const Address &address)
{
	merge_address = address;
	return;
}

void Event::set_replace_address(const Address &address)
{
	replace_address = address;
}

void Event::set_noop(bool value)
{
	noop = value;
}

void Event::set_next(Event &next)
{
	this -> next = &next;
	return;
}

double Event::incr_bus_wait_time(double time_incr)
{
	if(time_incr > 0.0)
		bus_wait_time += time_incr;
	return bus_wait_time;
}

double Event::incr_time_taken(double time_incr)
{
  	if(time_incr > 0.0)
		time_taken += time_incr;
	return time_taken;
}

void Event::print(FILE *stream)
{
	if(type == READ)
		fprintf(stream, "Read ");
	else if(type == WRITE)
		fprintf(stream, "Write");
	else if(type == ERASE)
		fprintf(stream, "Erase");
	else if(type == MERGE)
		fprintf(stream, "Merge");
	else
		fprintf(stream, "Unknown event type: ");
	address.print(stream);
	if(type == MERGE)
		merge_address.print(stream);
	fprintf(stream, " Time[%f, %f) Bus_wait: %f\n", start_time, start_time + time_taken, bus_wait_time);
	return;
}

#if 0
/* may be useful for further integration with DiskSim */

/* caution: copies pointers from rhs */
ioreq_event &Event::operator= (const ioreq_event &rhs)
{
	assert(&rhs != NULL);
	if((const ioreq_event *) &rhs == (const ioreq_event *) &(this -> ioreq))
		return *(this -> ioreq);
	ioreq -> time = rhs.time;
	ioreq -> type = rhs.type;
	ioreq -> next = rhs.next;
	ioreq -> prev = rhs.prev;
	ioreq -> bcount = rhs.bcount;
	ioreq -> blkno = rhs.blkno;
	ioreq -> flags = rhs.flags;
	ioreq -> busno = rhs.busno;
	ioreq -> slotno = rhs.slotno;
	ioreq -> devno = rhs.devno;
	ioreq -> opid = rhs.opid;
	ioreq -> buf = rhs.buf;
	ioreq -> cause = rhs.cause;
	ioreq -> tempint1 = rhs.tempint1;
	ioreq -> tempint2 = rhs.tempint2;
	ioreq -> tempptr1 = rhs.tempptr1;
	ioreq -> tempptr2 = rhs.tempptr2;
	ioreq -> mems_sled = rhs.mems_sled;
	ioreq -> mems_reqinfo = rhs.mems_reqinfo;
	ioreq -> start_time = rhs.start_time;
	ioreq -> batchno = rhs.batchno;
	ioreq -> batch_complete = rhs.batch_complete;
	ioreq -> batch_size = rhs.batch_size;
	ioreq -> batch_next = rhs.batch_next;
	ioreq -> batch_prev = rhs.batch_prev;
	return *ioreq;
}
#endif
