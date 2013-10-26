CXX=g++
CXXFLAGS=-Wall -c
LDFLAGS=
HEADERS=ssd.h
SOURCES=ssd_address.cpp ssd_block.cpp ssd_bm.cpp ssd_bus.cpp \
		ssd_channel.cpp ssd_controller.cpp ssd_die.cpp \
		ssd_event.cpp ssd_config.cpp ssd_ftlparent.cpp ssd_gc.cpp \
		ssd_package.cpp ssd_page.cpp ssd_plane.cpp ssd_ram.cpp \
		ssd_stats.cpp ssd_ssd.cpp ssd_wl.cpp \
		FTLs/dftl_parent.cpp FTLs/dftl_ftl.cpp FTLs/bdftl_ftl.cpp \
		FTLs/fast_ftl.cpp FTLs/page_ftl.cpp FTLs/bast_ftl.cpp \
		run_test.cpp
OBJECTS=$(SOURCES:.cpp=.o)

all: $(SOURCES) dftl

dftl: $(HEADERS) $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) -o $@

.cpp.o: $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	-rm -rf *.o flashsim_dftl
