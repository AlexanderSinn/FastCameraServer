CXX=/usr/local/cuda/bin/nvcc
SOURCES=$(wildcard *.cpp)
OBJECTS=$(SOURCES:.cpp=.o)
PROGRAM=FCS
FLAGS=-forward-unknown-to-host-compiler -O3 -march=native -std=c++17 -arch=sm_87

all: $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CXX) -g $(OBJECTS) -o $@ $(FLAGS) -lm3api -lnvToolsExt -ljetgpio

.cpp.o:
	$(CXX) -g -c $< -o $@ $(FLAGS) -x cu

clean:
	rm -f $(PROGRAM) $(OBJECTS) report*

nothing:
	@:
