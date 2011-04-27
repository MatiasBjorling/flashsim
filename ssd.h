/* Copyright 2009, 2010 Brendan Tauras */

/* ssd.h is part of FlashSim. */

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

/* ssd.h
 * Brendan Tauras 2010-07-16
 * Main SSD header file
 * 	Lists definitions of all classes, structures,
 * 		typedefs, and constants used in ssd namespace
 *		Controls options, such as debug asserts and test code insertions
 */

#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <queue>
#include <map>
 
#ifndef _SSD_H
#define _SSD_H

namespace ssd {

/* define exit codes for errors */
#define MEM_ERR -1
#define FILE_ERR -2

/* Uncomment to disable asserts for production */
#define NDEBUG


/* some obvious typedefs for laziness */
typedef unsigned int uint;
typedef unsigned long ulong;


/* Simulator configuration from ssd_config.cpp */

/* Configuration file parsing for extern config variables defined below */
void load_entry(char *name, double value, uint line_number);
void load_config(void);
void print_config(FILE *stream);

/* Ram class:
 * 	delay to read from and write to the RAM for 1 page of data */
extern const double RAM_READ_DELAY;
extern const double RAM_WRITE_DELAY;

/* Bus class:
 * 	delay to communicate over bus
 * 	max number of connected devices allowed
 * 	flag value to detect free table entry (keep this negative)
 * 	number of time entries bus has to keep track of future schedule usage
 * 	number of simultaneous communication channels - defined by SSD_SIZE */
extern const double BUS_CTRL_DELAY;
extern const double BUS_DATA_DELAY;
extern const uint BUS_MAX_CONNECT;
extern const double BUS_CHANNEL_FREE_FLAG;
extern const uint BUS_TABLE_SIZE;
/* extern const uint BUS_CHANNELS = 4; same as # of Packages, defined by SSD_SIZE */

/* Ssd class:
 * 	number of Packages per Ssd (size) */
extern const uint SSD_SIZE;

/* Package class:
 * 	number of Dies per Package (size) */
extern const uint PACKAGE_SIZE;

/* Die class:
 * 	number of Planes per Die (size) */
extern const uint DIE_SIZE;

/* Plane class:
 * 	number of Blocks per Plane (size)
 * 	delay for reading from plane register
 * 	delay for writing to plane register
 * 	delay for merging is based on read, write, reg_read, reg_write 
 * 		and does not need to be explicitly defined */
extern const uint PLANE_SIZE;
extern const double PLANE_REG_READ_DELAY;
extern const double PLANE_REG_WRITE_DELAY;

/* Block class:
 * 	number of Pages per Block (size)
 * 	number of erases in lifetime of block
 * 	delay for erasing block */
extern const uint BLOCK_SIZE;
extern const uint BLOCK_ERASES;
extern const double BLOCK_ERASE_DELAY;

/* Page class:
 * 	delay for Page reads
 * 	delay for Page writes */
extern const double PAGE_READ_DELAY;
extern const double PAGE_WRITE_DELAY;
extern const uint PAGE_SIZE;
extern const bool PAGE_ENABLE_DATA;
extern const uint PAGE_MAX_LOG;

/*
 * Mapping directory
 */
extern const uint MAP_DIRECTORY_SIZE;

/*
 * FTL Implementation
 */
extern const uint FTL_IMPLEMENTATION;

/*
 * LOG page limit for FAST.
 */
extern const uint LOG_PAGE_LIMIT;

/*
 * Number of blocks allowed to be in DFTL Cached Mapping Table.
 */
extern const uint CACHE_DFTL_LIMIT;

/*
 * Memory area to support pages with data.
 */
extern void *page_data;
extern void *global_buffer;

/* Enumerations to clarify status integers in simulation
 * Do not use typedefs on enums for reader clarity */

/* Page states
 * 	empty   - page ready for writing (and contains no valid data)
 * 	valid   - page has been written to and contains valid data
 * 	invalid - page has been written to and does not contain valid data */
enum page_state{EMPTY, VALID, INVALID};

/* Block states
 * 	free     - all pages in block are empty
 * 	active   - some pages in block are valid, others are empty or invalid
 * 	inactive - all pages in block are invalid */
enum block_state{FREE, ACTIVE, INACTIVE};

/* I/O request event types
 * 	read  - read data from address
 * 	write - write data to address (page state set to valid)
 * 	erase - erase block at address (all pages in block are erased - 
 * 	                                page states set to empty)
 * 	merge - move valid pages from block at address (page state set to invalid)
 * 	           to free pages in block at merge_address */
enum event_type{READ, WRITE, ERASE, MERGE};

/* General return status
 * return status for simulator operations that only need to provide general
 * failure notifications */
enum status{FAILURE, SUCCESS};

/* Address valid status
 * used for the valid field in the address class
 * example: if valid == BLOCK, then
 * 	the package, die, plane, and block fields are valid
 * 	the page field is not valid */
enum address_valid{NONE, PACKAGE, DIE, PLANE, BLOCK, PAGE};

/*
 * Block type status
 * used for the garbage collector specify what pool
 * it should work with.
 * the block types are log, data and map (Directory map usually)
 */
enum block_type {LOG, DATA, LOG_SEQ};

/*
 * Enumeration of the different FTL implementations.
 */
enum ftl_implementation {IMPL_PAGE, IMPL_BAST, IMPL_FAST, IMPL_DFTL, IMPL_BIMODAL};

/* List classes up front for classes that have references to their "parent"
 * (e.g. a Package's parent is a Ssd).
 *
 * The order of definition below follows the order of this list to support
 * cases of agregation where the agregate class should be defined first.
 * Defining the agregate class first enables use of its non-default
 * constructors that accept args
 * (e.g. a Ssd contains a Controller, Ram, Bus, and Packages). */
class Address;
class Stats;
class Event;
class Channel;
class Bus;
class Page;
class Block;
class Plane;
class Die;
class Package;
class Garbage_Collector;
class Wear_Leveler;
class Block_manager;
class FtlParent;
class FtlImpl_Page;
class FtlImpl_Bast;
class FtlImpl_Fast;

class Ram;
class Controller;
class Ssd;

/* Class to manage physical addresses for the SSD.  It was designed to have
 * public members like a struct for quick access but also have checking,
 * printing, and assignment functionality.  An instance is created for each
 * physical address in the Event class. */
class Address
{
public:
	uint package;
	uint die;
	uint plane;
	uint block;
	uint page;
	ulong real_address;
	enum address_valid valid;
	Address(void);
	Address(const Address &address);
	Address(const Address *address);
	Address(uint package, uint die, uint plane, uint block, uint page, enum address_valid valid);
	Address(uint address, enum address_valid valid);
	~Address();
	enum address_valid check_valid(uint ssd_size = SSD_SIZE, uint package_size = PACKAGE_SIZE, uint die_size = DIE_SIZE, uint plane_size = PLANE_SIZE, uint block_size = BLOCK_SIZE);
	enum address_valid compare(const Address &address) const;
	void print(FILE *stream = stdout);

	void operator+(int);
	void operator+(uint);
	Address &operator+=(const uint rhs);
	Address &operator=(const Address &rhs);

	void set_linear_address(ulong address, enum address_valid valid);
	void set_linear_address(ulong address);
	ulong get_linear_address() const;
};

class Stats
{
public:
	// Flash Translation Layer
	long numFTLRead;
	long numFTLWrite;
	long numFTLErase;

	// Garbage Collection
	long numGCRead;
	long numGCWrite;
	long numGCErase;

	// Wearleveling
	long numWLRead;
	long numWLWrite;
	long numWLErase;

	// Log based FTL's
	long numLogMergeSwitch;
	long numLogMergePartial;
	long numLogMergeFull;

	// Cache based FTL's
	long numCacheHits;
	long numCacheFaults;

	// Memory consumptions (Bytes)
	long numMemoryTranslation;
	long numMemoryCache;

	long numMemoryRead;
	long numMemoryWrite;

	// Advance statictics
	double translation_overhead() const;
	double variance_of_io() const;
	double cache_hit_ratio() const;

	// Constructors, maintainance, output, etc.
	Stats(void);
	void print_statistics();
};

/* Class to emulate a log block with page-level mapping. */
class LogPageBlock
{
public:
	LogPageBlock(void);
	~LogPageBlock(void);

	int *pages;
	ulong *aPages;
	Address address;
	int numPages;

	LogPageBlock *next;

	bool operator() (const ssd::LogPageBlock& lhs, const ssd::LogPageBlock& rhs) const;
	bool operator() (const ssd::LogPageBlock*& lhs, const ssd::LogPageBlock*& rhs) const;
};


/* Class to manage I/O requests as events for the SSD.  It was designed to keep
 * track of an I/O request by storing its type, addressing, and timing.  The
 * SSD class creates an instance for each I/O request it receives. */
class Event 
{
public:
	Event(enum event_type type, ulong logical_address, uint size, double start_time);
	~Event(void);
	void consolidate_metaevent(Event &list);
	ulong get_logical_address(void) const;
	const Address &get_address(void) const;
	const Address &get_merge_address(void) const;
	const Address &get_log_address(void) const;
	const Address &get_replace_address(void) const;
	uint get_size(void) const;
	enum event_type get_event_type(void) const;
	double get_start_time(void) const;
	double get_time_taken(void) const;
	double get_bus_wait_time(void) const;
	bool get_noop(void) const;
	Event *get_next(void) const;
	void set_address(const Address &address);
	void set_merge_address(const Address &address);
	void set_log_address(const Address &address);
	void set_replace_address(const Address &address);
	void set_next(Event &next);
	void set_payload(void *payload);
	void set_event_type(const enum event_type &type);
	void set_noop(bool value);
	void *get_payload(void) const;
	double incr_bus_wait_time(double time);
	double incr_time_taken(double time_incr);
	void print(FILE *stream = stdout);
	Event *get_last_event(Event &event) const;
private:
	double start_time;
	double time_taken;
	double bus_wait_time;
	enum event_type type;
	bool noop;
	ulong logical_address;
	Address address;
	Address merge_address;
	Address log_address;
	Address replace_address;
	uint size;
	void *payload;
	Event *next;
};

/* Single bus channel
 * Simulate multiple devices on 1 bus channel with variable bus transmission
 * durations for data and control delays with the Channel class.  Provide the 
 * delay times to send a control signal or 1 page of data across the bus
 * channel, the bus table size for the maximum number channel transmissions that
 * can be queued, and the maximum number of devices that can connect to the bus.
 * To elaborate, the table size is the size of the channel scheduling table that
 * holds start and finish times of events that have not yet completed in order
 * to determine where the next event can be scheduled for bus utilization. */
class Channel
{
public:
	Channel(double ctrl_delay = BUS_CTRL_DELAY, double data_delay = BUS_DATA_DELAY, uint table_size = BUS_TABLE_SIZE, uint max_connections = BUS_MAX_CONNECT);
	~Channel(void);
	enum status lock(double start_time, double duration, Event &event);
	enum status connect(void);
	enum status disconnect(void);
private:
	void unlock(double current_time);
	uint table_size;
	double * const lock_time;
	double * const unlock_time;
	uint table_entries;
	uint selected_entry;
	uint num_connected;
	uint max_connections;
	double ctrl_delay;
	double data_delay;
};

/* Multi-channel bus comprised of Channel class objects
 * Simulates control and data delays by allowing variable channel lock
 * durations.  The sender (controller class) should specify the delay (control,
 * data, or both) for events (i.e. read = ctrl, ctrl+data; write = ctrl+data;
 * erase or merge = ctrl).  The hardware enable signals are implicitly
 * simulated by the sender locking the appropriate bus channel through the lock
 * method, then sending to multiple devices by calling the appropriate method
 * in the Package class. */
class Bus
{
public:
	Bus(uint num_channels = SSD_SIZE, double ctrl_delay = BUS_CTRL_DELAY, double data_delay = BUS_DATA_DELAY, uint table_size = BUS_TABLE_SIZE, uint max_connections = BUS_MAX_CONNECT);
	~Bus(void);
	enum status lock(uint channel, double start_time, double duration, Event &event);
	enum status connect(uint channel);
	enum status disconnect(uint channel);
	Channel &get_channel(uint channel);
private:
	uint num_channels;
	Channel * const channels;
};



/* The page is the lowest level data storage unit that is the size unit of
 * requests (events).  Pages maintain their state as events modify them. */
class Page 
{
public:
	Page(const Block &parent, double read_delay = PAGE_READ_DELAY, double write_delay = PAGE_WRITE_DELAY);
	~Page(void);
	enum status _read(Event &event);
	enum status _write(Event &event);
	const Block &get_parent(void) const;
	enum page_state get_state(void) const;
	void set_state(enum page_state state);
private:
	enum page_state state;
	const Block &parent;
	double read_delay;
	double write_delay;
};

/* The block is the data storage hardware unit where erases are implemented.
 * Blocks maintain wear statistics for the FTL. */
class Block 
{
public:
	Block(const Plane &parent, uint size = BLOCK_SIZE, ulong erases_remaining = BLOCK_ERASES, double erase_delay = BLOCK_ERASE_DELAY);
	~Block(void);
	enum status read(Event &event);
	enum status write(Event &event);
	enum status replace(Event &event);
	enum status _erase(Event &event);
	const Plane &get_parent(void) const;
	uint get_pages_valid(void) const;
	uint get_pages_invalid(void) const;
	enum block_state get_state(void) const;
	enum page_state get_state(uint page) const;
	enum page_state get_state(const Address &address) const;
	double get_last_erase_time(void) const;
	ulong get_erases_remaining(void) const;
	uint get_size(void) const;
	enum status get_next_page(Address &address) const;
	void invalidate_page(uint page);
private:
	uint size;
	Page * const data;
	const Plane &parent;
	uint pages_valid;
	uint pages_invalid;
	enum block_state state;
	ulong erases_remaining;
	double last_erase_time;
	double erase_delay;
};

/* The plane is the data storage hardware unit that contains blocks.
 * Plane-level merges are implemented in the plane.  Planes maintain wear
 * statistics for the FTL. */
class Plane 
{
public:
	Plane(const Die &parent, uint plane_size = PLANE_SIZE, double reg_read_delay = PLANE_REG_READ_DELAY, double reg_write_delay = PLANE_REG_WRITE_DELAY);
	~Plane(void);
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status replace(Event &event);
	enum status _merge(Event &event);
	const Die &get_parent(void) const;
	double get_last_erase_time(const Address &address) const;
	ulong get_erases_remaining(const Address &address) const;
	void get_least_worn(Address &address) const;
	uint get_size(void) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
private:
	void update_wear_stats(void);
	enum status get_next_page(void);
	uint size;
	Block * const data;
	const Die &parent;
	uint least_worn;
	ulong erases_remaining;
	double last_erase_time;
	double reg_read_delay;
	double reg_write_delay;
	Address next_page;
	uint free_blocks;
};

/* The die is the data storage hardware unit that contains planes and is a flash
 * chip.  Dies maintain wear statistics for the FTL. */
class Die 
{
public:
	Die(const Package &parent, Channel &channel, uint die_size = DIE_SIZE);
	~Die(void);
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status replace(Event &event);
	enum status merge(Event &event);
	enum status _merge(Event &event);
	const Package &get_parent(void) const;
	double get_last_erase_time(const Address &address) const;
	ulong get_erases_remaining(const Address &address) const;
	void get_least_worn(Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
private:
	void update_wear_stats(const Address &address);
	uint size;
	Plane * const data;
	const Package &parent;
	Channel &channel;
	uint least_worn;
	ulong erases_remaining;
	double last_erase_time;
};

/* The package is the highest level data storage hardware unit.  While the
 * package is a virtual component, events are passed through the package for
 * organizational reasons, including helping to simplify maintaining wear
 * statistics for the FTL. */
class Package 
{
public:
	Package (const Ssd &parent, Channel &channel, uint package_size = PACKAGE_SIZE);
	~Package ();
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status replace(Event &event);
	enum status merge(Event &event);
	const Ssd &get_parent(void);
	double get_last_erase_time (const Address &address) const;
	ulong get_erases_remaining (const Address &address) const;
	void get_least_worn (Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
private:
	void update_wear_stats (const Address &address);
	uint size;
	Die * const data;
	const Ssd &parent;
	uint least_worn;
	ulong erases_remaining;
	double last_erase_time;
};

/* place-holder definitions for GC, WL, FTL, RAM, Controller 
 * please make sure to keep this order when you replace with your definitions */
class Garbage_collector 
{
public:
	Garbage_collector(FtlParent &ftl);
	~Garbage_collector(void);
private:
	void clean(Address &address);
};

class Wear_leveler 
{
public:
	Wear_leveler(FtlParent &FTL);
	~Wear_leveler(void);
	enum status insert(const Address &address);
};

class Block_manager
{
public:
	Block_manager(FtlParent &ftl);
	~Block_manager(void);

	// Usual suspects
	Address get_free_block();
	Address get_free_block(block_type btype);
	void invalidate(Address &address, block_type btype);
	void print_statistics();
	void insert_events(Event &event);
	void promote_block(block_type to_type);
	bool is_log_full();

	// Map directory
	void simulate_map_write(Event &events);
	void simulate_map_read(Event &events);
private:
	void get_page_block(Address &address);

	FtlParent &ftl;

	ulong data_active;
	ulong log_active;
	ulong logseq_active;

	ulong max_log_blocks;
	ulong max_blocks;

	ulong max_map_pages;
	ulong map_offset;
	ulong map_space_capacity;

	std::vector<ulong> free_list;
	std::vector<ulong> invalid_list;

	// Until all pages have been requested, we serve them from a linear
	// address space.
	ulong simpleCurrentFree;

	// Counter for returning the next free page.
	ulong directoryCurrentPage;
	// Address on the current cached page in SRAM.
	ulong directoryCachedPage;
};


class FtlParent
{
public:
	FtlParent(Controller &controller);

	virtual enum status read(Event &event) = 0;
	virtual enum status write(Event &event) = 0;
	friend class Block_manager;

	ulong get_erases_remaining(const Address &address) const;
	void get_least_worn(Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;

	Address resolve_logical_address(unsigned int logicalAddress);

protected:
	Controller &controller;
	Block_manager manager;
};

class FtlImpl_Page : public FtlParent
{
public:
	FtlImpl_Page(Controller &controller);
	~FtlImpl_Page();
	enum status read(Event &event);
	enum status write(Event &event);
private:
	Address resolve_logical_address(uint logicalAddress);

	ulong currentPage;
	long *map;
};

class FtlImpl_Bast : public FtlParent
{
public:
	FtlImpl_Bast(Controller &controller);
	~FtlImpl_Bast();
	enum status read(Event &event);
	enum status write(Event &event);
private:
	std::map<long, LogPageBlock*> log_map;

	long *data_list;

	void dispose_logblock(LogPageBlock *logBlock, long logicalBlockAddress);
	void allocate_new_logblock(LogPageBlock *logBlock, long logicalBlockAddress, Event &event);

	bool is_sequential(LogPageBlock* logBlock, long logicalBlockaddress, Event &event);
	bool random_merge(LogPageBlock *logBlock, long logicalBlockAddress, Event &event);

	int addressShift;
	int addressSize;
};

class FtlImpl_Fast : public FtlParent
{
public:
	FtlImpl_Fast(Controller &controller);
	~FtlImpl_Fast();
	enum status read(Event &event);
	enum status write(Event &event);
private:
	std::map<long, LogPageBlock*> log_map;

	long *data_list;

	void dispose_logblock(LogPageBlock *logBlock, long logicalBlockAddress);
	void allocate_new_logblock(LogPageBlock *logBlock, long logicalBlockAddress, Event &event);

	bool write_to_log_block(Event &event, long logicalBlockAddress);

	void switch_sequential(Event &event);
	void merge_sequential(Event &event);
	bool random_merge(LogPageBlock *logBlock, Event &event);

	long sequential_logicalblock_address;
	Address sequential_address;
	uint sequential_offset;

	uint log_page_next;
	LogPageBlock *log_pages;

	int addressShift;
	int addressSize;
};

class FtlImpl_Dftl : public FtlParent
{
public:
	FtlImpl_Dftl(Controller &controller);
	~FtlImpl_Dftl();
	enum status read(Event &event);
	enum status write(Event &event);
private:

	struct MPage {
		long vpn;
		long ppn;
		double create_ts;
		double modified_ts;

		MPage();
	};

	std::map<long, bool> cmt;
	MPage *trans_map;

	void select_victim_entry(FtlImpl_Dftl::MPage &mpage);
	void consult_GTD(long dppn, Event &event);
	void reset_MPage(FtlImpl_Dftl::MPage &mpage);
	void remove_victims(FtlImpl_Dftl::MPage &mpage);

	void resolve_mapping(Event &event, bool isWrite);

	bool lookup_CMT(long dlpn, Event &event);

	long get_free_translation_page();
	long get_free_data_page();


	// Mapping information
	int addressPerPage;
	int addressSize;
	uint totalCMTentries;

	// Current storage
	long currentDataPage;
	long currentTranslationPage;
};

class FtlImpl_BDftl : public FtlParent
{
public:
	FtlImpl_BDftl(Controller &controller);
	~FtlImpl_BDftl();
	enum status read(Event &event);
	enum status write(Event &event);
private:

	struct MPage {
		long vpn;
		long ppn;
		double create_ts;
		double modified_ts;

		MPage();
	};

	std::map<long, bool> cmt;
	MPage *trans_map;

	void select_victim_entry(FtlImpl_BDftl::MPage &mpage);
	void consult_GTD(long dppn, Event &event);
	void reset_MPage(FtlImpl_BDftl::MPage &mpage);
	void remove_victims(FtlImpl_BDftl::MPage &mpage);

	void resolve_mapping(Event &event, bool isWrite);

	bool lookup_CMT(long dlpn, Event &event);

	long get_free_translation_page();
	long get_free_data_page();


	// Mapping information
	int addressPerPage;
	int addressSize;
	uint totalCMTentries;

	// Current storage
	long currentDataPage;
	long currentTranslationPage;
};


/* This is a basic implementation that only provides delay updates to events
 * based on a delay value multiplied by the size (number of pages) needed to
 * be written. */
class Ram 
{
public:
	Ram(double read_delay = RAM_READ_DELAY, double write_delay = RAM_WRITE_DELAY);
	~Ram(void);
	enum status read(Event &event);
	enum status write(Event &event);
private:
	double read_delay;
	double write_delay;
};

/* The controller accepts read/write requests through its event_arrive method
 * and consults the FTL regarding what to do by calling the FTL's read/write
 * methods.  The FTL returns an event list for the controller through its issue
 * method that the controller buffers in RAM and sends across the bus.  The
 * controller's issue method passes the events from the FTL to the SSD.
 *
 * The controller also provides an interface for the FTL to collect wear
 * information to perform wear-leveling.  */
class Controller 
{
public:
	Controller(Ssd &parent);
	~Controller(void);
	enum status event_arrive(Event &event);
	friend class FtlParent;
	friend class FtlImpl_Page;
	friend class FtlImpl_Bast;
	friend class FtlImpl_Fast;
	friend class FtlImpl_Dftl;
	friend class FtlImpl_BDftl;
	Stats stats;
private:
	enum status issue(Event &event_list);
	ssd::ulong get_erases_remaining(const Address &address) const;
	void get_least_worn(Address &address) const;
	double get_last_erase_time(const Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
	Ssd &ssd;
	FtlParent *ftl;
};

/* The SSD is the single main object that will be created to simulate a real
 * SSD.  Creating a SSD causes all other objects in the SSD to be created.  The
 * event_arrive method is where events will arrive from DiskSim. */
class Ssd 
{
public:
	Ssd (uint ssd_size = SSD_SIZE);
	~Ssd(void);
	double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time);
	double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, void *buffer);
	void *get_result_buffer();
	friend class Controller;
	void print_statistics();
private:
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status merge(Event &event);
	enum status replace(Event &event);
	enum status merge_replacement_block(Event &event);
	ulong get_erases_remaining(const Address &address) const;
	void update_wear_stats(const Address &address);
	void get_least_worn(Address &address) const;
	double get_last_erase_time(const Address &address) const;	
	Package &get_data(void);
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;

	uint size;
	Controller controller;
	Ram ram;
	Bus bus;
	Package * const data;
	ulong erases_remaining;
	ulong least_worn;
	double last_erase_time;
};

} /* end namespace ssd */

#endif
