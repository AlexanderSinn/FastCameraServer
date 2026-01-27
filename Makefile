CXX=/usr/local/cuda/bin/nvcc
SOURCES=main.cpp
OBJECTS=$(SOURCES:.cpp=.o)
PROGRAM=FCS

all: $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CXX) -g $(OBJECTS) -o $@ -lm3api -O3 -march=native -forward-unknown-to-host-compiler -std=c++17 -lnvToolsExt

.cpp.o: $(patsubst %.cpp,%.o,$(wildcard *.cpp))
	$(CXX) -g -c $< -o $@ -O3 -march=native -forward-unknown-to-host-compiler -x cu -std=c++17

clean:
	rm -f $(PROGRAM) $(OBJECTS) report*

install:
	cp $(PROGRAM) ../../bin

nothing:
	@:
