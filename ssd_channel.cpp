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
	table_size(table_size),

	/* use a const pointer (double * const) for the scheduling table arrays
	 * like a reference, we cannot reseat the pointer */
	lock_time(new double[table_size]),
	unlock_time(new double[table_size]),

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

	uint i;

	/* initialize scheduling tables
	 * arrays allocated in initializer list */
	if(lock_time == NULL || unlock_time == NULL)
	{
		fprintf(stderr, "Bus channel error: %s: constructor unable to allocate channel scheduling tables\n", __func__);
		exit(MEM_ERR);
	}
	for(i = 0; i < table_size; i++)
	{
		lock_time[i] = BUS_CHANNEL_FREE_FLAG;
		unlock_time[i] = BUS_CHANNEL_FREE_FLAG;
	}

	return;
}

/* free allocated bus channel state space */
Channel::~Channel(void)
{
	assert(lock_time != NULL && unlock_time != NULL);
	delete[] lock_time;
	delete[] unlock_time;
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
/* TODO: Recombine assert statements */
	assert(lock_time != NULL && unlock_time != NULL);assert(num_connected <= max_connections);assert(ctrl_delay >= 0.0);assert(data_delay >= 0.0);assert(start_time >= 0.0);assert(duration >= 0.0);

	#ifndef NDEBUG
		uint j;
		printf("Table entries before unlock()\n");
		for(j = 0; j < table_size; j++)
			printf("%lf, %lf\n", lock_time[j], unlock_time[j]);
		printf("Press ENTER to continue...");
		fflush(stdout);
		getchar();
	#endif

	/* free up any table slots and sort existing ones */
	unlock(start_time);

	#ifndef NDEBUG
		printf("Table entries after unlock()\n");
		for(j = 0; j < table_size; j++)
			printf("%lf, %lf\n", lock_time[j], unlock_time[j]);
		printf("Press ENTER to continue...");
		fflush(stdout);
		getchar();
	#endif

	/* give up if no free table slots */
	if(table_entries >= table_size)
		return FAILURE;
	
	uint i = 0;
	double sched_time = BUS_CHANNEL_FREE_FLAG;

	/* just schedule if table is empty */
	if(table_entries == 0)
		sched_time = start_time;

	/* check if can schedule before or in between before just scheduling
	 * after all other events */
	else
	{
		/* skip over empty table entries
		 * empty table entries will be first from sorting (in unlock method)
		 * because the flag is a negative value */
		while(lock_time[i] == BUS_CHANNEL_FREE_FLAG && i < table_size)
			i++;
	
		/* schedule before first event in table */
		if(lock_time[i] > start_time && lock_time[i] - start_time >= duration)
			sched_time = start_time;
	
		/* schedule in between other events in table */
		if(sched_time == BUS_CHANNEL_FREE_FLAG)
		{
			for(; i < table_size - 2; i++)
			{
				/* enough time to schedule in between next two events */
				if(unlock_time[i] >= start_time  && lock_time[i + 1] - unlock_time[i] >= duration)
				{
					sched_time = unlock_time[i];
					break;
				}
			}
		}
	
		/* schedule after all events in table */
		if(sched_time == BUS_CHANNEL_FREE_FLAG)
			sched_time = unlock_time[table_size - 1];
	}

	/* write scheduling info in free table slot */
	lock_time[0] = sched_time;
	unlock_time[0] = sched_time + duration;
	table_entries++;

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
	uint i;

	/* remove expired channel lock entries */
	for(i = 0; i < table_size; i++)
	{
		if(unlock_time[i] != BUS_CHANNEL_FREE_FLAG)
		{
			if(unlock_time[i] <= start_time)
			{
				lock_time[i] = unlock_time[i] = BUS_CHANNEL_FREE_FLAG;
				table_entries--;
			}
		}
	}

	/* sort both arrays together - e.g. sort by first array but perform same
	 * move operation on both arrays */
	quicksort(lock_time, unlock_time, 0, table_size - 1);
	return;
}
