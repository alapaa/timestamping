TARGET = sender
LIBS =

CXX = g++
CXXFLAGS = -g -Wall -std=c++11

.PHONY: default all clean

default: $(TARGET)
all: default

SOURCES = $(wildcard *.cpp)
OBJECTS = $(patsubst %.cpp, %.o, $(SOURCES))
HEADERS = $(wildcard *.h)
# Dependency generation

%.d: %.cpp
	@set -e; rm -f $@; \
         $(CXX) -MM $(CXXFLAGS) $< > $@.$$$$; \
         sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

include $(SOURCES:.cpp=.d)

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	-rm -f *.o *.d
	-rm -f $(TARGET)
