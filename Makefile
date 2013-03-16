CXX        = g++
CXXFLAGS   = -Wall -std=c++0x -O2
LDFLAGS    =
BOOST_ROOT = /usr
INCLUDES   = -I $(BOOST_ROOT)/include
LIBS       = -L $(BOOST_ROOT)/lib -lboost_system -lboost_filesystem -lpthread
OBJS       = src/mydnsjpd.o
TARGET     = mydnsjpd

all: $(TARGET)
rebuild:  clean all

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

.cpp.o: 
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
