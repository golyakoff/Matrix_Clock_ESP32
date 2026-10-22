# Host unit tests of hardware independent code.
# Run: make -C test/host

CXX ?= g++
MAIN := ../../main
CXXFLAGS := -Wall -Wextra -std=gnu++17 -Istub -I$(MAIN)/include

TESTS := build/test_time_helper

.PHONY: all clean
all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

build:
	mkdir -p build

build/test_time_helper: test_time_helper.cpp $(MAIN)/time_helper.cpp $(MAIN)/include/time_helper.h | build
	$(CXX) $(CXXFLAGS) -o $@ test_time_helper.cpp $(MAIN)/time_helper.cpp

clean:
	rm -rf build
