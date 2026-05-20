# CXX=/usr/local/cuda/bin/nvcc
CXX=g++
SOURCES=$(wildcard *.cpp)
OBJECTS=$(SOURCES:.cpp=.o)
PROGRAM=FCS
# FLAGS=-forward-unknown-to-host-compiler -O3 -march=native -std=c++17 -arch=sm_87 --extended-lambda --expt-relaxed-constexpr
FLAGS=-O3 -march=native -std=c++17

all: $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CXX) -g $(OBJECTS) -o $@ $(FLAGS) -ljetgpio -lpthread

.cpp.o:
	$(CXX) -g -c $< -o $@ $(FLAGS)

clean:
	rm -f $(PROGRAM) $(OBJECTS) report*

nothing:
	@:
