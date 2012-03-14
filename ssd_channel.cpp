/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_channel.cpp is part of FlashSim. */

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

/* Channel class
 * Brendan Tauras 2010-08-09
 *
 * Single bus channel
 * Simulate multiple devices on 1 bus channel with variable bus transmission
 * durations for data and control delays with the Channel class.  Provide the 
 * delay times to send a control signal or 1 page of data across the bus
 * channel, the bus table size for the maximum number channel transmissions that
 * can be queued, and the maximum number of devices that can connect to the bus.
 * To elaborate, the table size is the size of the channel scheduling table that
 * holds start and finish times of events that have not yet completed in order
 * to determine where the next event can be scheduled for bus utilization.
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <algorithm>
#include <stdexcept>
#include "ssd.h"

using namespace ssd;

/* a single channel bus: all connected devices share the same channel
 * simulates control and data
 * enable signals are implicitly simulated by the sender locking the bus
 * 	then sending to multiple devices
 * the table size is synonymous to the queue size for the channel
 * it is not necessary to use the max connections properly, but it is provided
 * 	to help ensure correctness */
Channel::Channel(double ctrl_delay, double data_delay, uint table_size, uint max_connections):
	//table_size(table_size),

	/* use a const pointer (double * const) for the scheduling table arrays
	 * like a reference, we cannot reseat the pointer */
	table_entries(0),
	selected_entry(0),
	num_connected(0),
	max_connections(max_connections),
	ctrl_delay(ctrl_delay),
	data_delay(data_delay)
{
	if(ctrl_delay < 0.0){
		fprintf(stderr, "Bus channel warning: %s: constructor received negative control delay value\n\tsetting control delay to 0.0\n", __func__);
		ctrl_delay = 0.0;
	}
	if(data_delay < 0.0){
		fprintf(stderr, "Bus channel warning: %s: constructor received negative data delay value\n\tsetting data delay to 0.0\n", __func__);
		data_delay = 0.0;
	}

	timings.reserve(4096);

	ready_at = -1;
}

/* free allocated bus channel state space */
Channel::~Channel(void)
{
	if(num_connected > 0)
		fprintf(stderr, "Bus channel warning: %s: %d connected devices when bus channel terminated\n", __func__, num_connected);
	return;
}

/* not required before calling lock()
 * but should be used to help ensure correctness
 * controller that talks on all channels should not connect/disconnect
 * 	only components that receive a single channel should connect */
enum status Channel::connect(void)
{
	if(num_connected < max_connections)
	{
		num_connected++;
		return SUCCESS;
	}
	else
	{
		fprintf(stderr, "Bus channel error: %s: device attempting to connect to channel when %d max devices already connected\n", __func__, max_connections);
		return FAILURE;
	}
}

/* not required when finished
 * but should be used to help ensure correctness
 * controller that talks on all channels should not connect/disconnect
 * 	only components that receive a single channel should connect */
enum status Channel::disconnect(void)
{
	if(num_connected > 0)
	{
		num_connected--;
		return SUCCESS;
	}
	fprintf(stderr, "Bus channel error: %s: device attempting to disconnect from bus channel when no devices connected\n", __func__);
	return FAILURE;
}

/* lock bus channel for event
 * updates event with bus delay and bus wait time if there is wait time
 * bus will automatically unlock after event is finished using bus
 * event is sent across bus as soon as bus channel is available
 * event may fail if bus channel is saturated so check return value
 */
enum status Channel::lock(double start_time, double duration, Event &event)
{
	assert(num_connected <= max_connections);
	assert(ctrl_delay >= 0.0);
	assert(data_delay >= 0.0);
	assert(start_time >= 0.0);
	assert(duration >= 0.0);

	/* free up any table slots and sort existing ones */
	unlock(start_time);

	double sched_time = BUS_CHANNEL_FREE_FLAG;

	/* just schedule if table is empty */
	if(timings.size() == 0)
		sched_time = start_time;

	/* check if can schedule before or in between before just scheduling
	 * after all other events */
	else
	{
		/* skip over empty table entries
		 * empty table entries will be first from sorting (in unlock method)
		 * because the flag is a negative value */
		std::vector<lock_times>::iterator it = timings.begin();

		/* schedule before first event in table */
		if((*it).lock_time > start_time && (*it).lock_time - start_time >= duration)
			sched_time = start_time;

		/* schedule in between other events in table */
		if(sched_time == BUS_CHANNEL_FREE_FLAG)
		{
			for(; it < timings.end(); it++)
			{
				if (it + 1 != timings.end())
				{
					/* enough time to schedule in between next two events */
					if((*it).unlock_time >= start_time  && (*(it+1)).lock_time - (*it).unlock_time >= duration)
					{
						sched_time = (*it).unlock_time;
						break;
					}
				}

			}
		}

		/* schedule after all events in table */
		if(sched_time == BUS_CHANNEL_FREE_FLAG)
			sched_time = timings.back().unlock_time;
	}

	/* write scheduling info in free table slot */
	lock_times lt;
	lt.lock_time = sched_time;
	lt.unlock_time = sched_time + duration;
	timings.push_back(lt);

	if (lt.unlock_time > ready_at)
		ready_at = lt.unlock_time;

	/* update event times for bus wait and time taken */
	event.incr_bus_wait_time(sched_time - start_time);
	event.incr_time_taken(sched_time - start_time + duration);

	return SUCCESS;
}

/* remove all expired entries (finish time is less than provided time)
 * update current number of table entries used
 * sort table by finish times (2nd row) */
void Channel::unlock(double start_time)
{
	/* remove expired channel lock entries */
	std::vector<lock_times>::iterator it;
	for ( it = timings.begin(); it < timings.end(); it++)
	{
		if((*it).unlock_time <= start_time)
			timings.erase(it);
		else
			break;
	}
	std::sort(timings.begin(), timings.end(), &timings_sorter);
}

bool Channel::timings_sorter(lock_times const& lhs, lock_times const& rhs) {
    return lhs.lock_time < rhs.lock_time;
}

double Channel::ready_time(void)
{
	return ready_at;
}

