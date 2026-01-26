CXX=g++
SOURCES=main.cpp
OBJECTS=$(SOURCES:.cpp=.o)
PROGRAM=FCS

all: $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CXX) -g $(OBJECTS) -o $@ -lm3api -O3 -march=native -fopenmp -flto

.cpp.o: $(patsubst %.cpp,%.o,$(wildcard *.cpp))
	$(CXX) -g -c $< -o $@ -O3 -march=native -fopenmp -flto

clean:
	rm -f $(PROGRAM) $(OBJECTS)

install:
	cp $(PROGRAM) ../../bin

nothing:
	@:
