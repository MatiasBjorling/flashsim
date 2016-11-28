CXX=g++
CXXFLAGS=-Wall -c -std=c++11 -g
LDFLAGS=
HEADERS=ssd.h
SOURCES_SSDLIB = $(filter-out ssd_ftl.cpp, $(wildcard ssd_*.cpp))  \
                 $(wildcard FTLs/*.cpp)                            \
                 SSDSim.cpp
OBJECTS_SSDLIB=$(patsubst %.cpp,%.o,$(SOURCES_SSDLIB))
SOURCES_RUNS = $(wildcard run_*.cpp)
PROGRAMS = $(patsubst run_%.cpp,%,$(SOURCES_RUNS))


all: $(PROGRAMS)

.cpp.o: $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@

define PROGRAM_TEMPLATE
  $1 : run_$1.o $$(OBJECTS_SSDLIB)
	$$(CXX) $$(LDFLAGS) $$< $$(OBJECTS_SSDLIB) -o $$@
endef

$(foreach prog,$(PROGRAMS),$(eval $(call PROGRAM_TEMPLATE,$(prog))))

clean:
	-rm -rf *.o FTLs/*.o $(PROGRAMS)

.PHONY: files
files:
	@echo $(SOURCES_SSDLIB) $(SOURCES_RUNS) $(HEADERS) | tr ' ' '\n'
