TARGET = sender
LIBS =

CXX = g++
CXXFLAGS = -std=c++11

.PHONY: default all clean

PROJ_ROOT = .

# Where to find user code.
SRC_DIR = $(PROJ_ROOT)/src
TEST_DIR = test
TEST_SRC = $(TEST_DIR)/src

SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS = $(patsubst %.cpp, %.o, $(SOURCES))
TEST_SOURCES = $(wildcard $(TEST_SRC)/*.cpp)
TEST_OBJECTS = $(patsubst %.cpp, %.o, $(SOURCES))
HEADERS = $(wildcard $(SRC_DIR)/*.h)

# Please tweak the following variable definitions as needed by your
# project, except GTEST_HEADERS, which you can use in your own targets
# but shouldn't modify.

# Points to the root of Google Test, relative to where this file is.
# Remember to tweak this if you move this file.
GTEST_DIR = gtest/googletest/googletest

# Flags passed to the preprocessor.
# Set Google Test's header directory as a system directory, such that
# the compiler doesn't generate warnings in Google Test headers.
CPPFLAGS += -isystem $(GTEST_DIR)/include

# Gtest-needed flags passed to the C++ compiler.
CXXFLAGS += -g -Wall -Wextra -pthread

# All tests produced by this Makefile.  Remember to add new tests you
# created to the list.
TESTS = sample1_unittest

# All Google Test headers.  Usually you shouldn't change this
# definition.
GTEST_HEADERS = $(GTEST_DIR)/include/gtest/*.h \
                $(GTEST_DIR)/include/gtest/internal/*.h

default: $(TARGET)
all: $(TARGET) $(TESTS)


# Dependency generation

%.d: %.cpp
	set -e; rm -f $@; \
         $(CXX) -MM $(CXXFLAGS) -I$(GTEST_DIR)/include -I$(SRC_DIR) $< > $@.$$$$; \
         sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

include $(SOURCES:.cpp=.d)
include $(TEST_SOURCES:.cpp=.d)

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	-rm -f $(SRC_DIR)/*.o $(SRC_DIR)/*.d $(TEST_SRC)/*.o $(TEST_SRC)/*.d
	-rm -f $(TARGET) $(TESTS) gtest.a gtest_main.a

# Builds gtest.a and gtest_main.a.

# Usually you shouldn't tweak such internal variables, indicated by a
# trailing _.
GTEST_SRCS_ = $(GTEST_DIR)/src/*.cc $(GTEST_DIR)/src/*.h $(GTEST_HEADERS)

# For simplicity and to avoid depending on Google Test's
# implementation details, the dependencies specified below are
# conservative and not optimized.  This is fine as Google Test
# compiles fast and for ordinary users its source rarely changes.
gtest-all.o: $(GTEST_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) $(CXXFLAGS) -c \
            $(GTEST_DIR)/src/gtest-all.cc

gtest_main.o: $(GTEST_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) $(CXXFLAGS) -c \
            $(GTEST_DIR)/src/gtest_main.cc

gtest.a: gtest-all.o
	$(AR) $(ARFLAGS) $@ $^

gtest_main.a: gtest-all.o gtest_main.o
	$(AR) $(ARFLAGS) $@ $^

# Builds a sample test.  A test should link with either gtest.a or
# gtest_main.a, depending on whether it defines its own main()
# function.

test_util.o: $(TEST_SRC)/test_util.cpp \
                     $(SRC_DIR)/util.h $(GTEST_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -isystem $(GTEST_DIR)/include -c $(TEST_SRC)/test_util.cpp

test_util: util.o test_util.o gtest_main.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -lpthread $^ -o $@
