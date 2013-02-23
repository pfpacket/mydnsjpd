CXX        = g++
CXXFLAGS   = -Wall -std=c++0x -O2
LDFLAGS    =
BOOST_ROOT = /usr
INCLUDES   = -I $(BOOST_ROOT)/include
LIBS       = -L $(BOOST_ROOT)/lib -lboost_system -lpthread
OBJS       = src/mydns_updater.o
TARGET     = mydns_update

all: $(TARGET)
rebuild:  clean all

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

.cpp.o: 
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
