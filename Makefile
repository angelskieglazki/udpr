CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread

SRCDIR = src
SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

# File transfer sources
FILE_TRANSFER_SRC = file_storage.cpp file_server.cpp file_client.cpp
FILE_TRANSFER_OBJ = $(FILE_TRANSFER_SRC:.cpp=.o)

TARGETS = file_server file_client ex_client ex_server

.PHONY: all clean

all: $(TARGETS)

# Example server and client
ex_server: ex_server.cpp $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $^
ex_client: ex_client.cpp $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $^

# File transfer server
file_server: fserver.cpp $(FILE_TRANSFER_OBJ) $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# File transfer client
file_client: fclient.cpp $(FILE_TRANSFER_OBJ) $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^



$(SRCDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGETS) $(OBJECTS) $(FILE_TRANSFER_OBJ)

# Зависимости src/
$(SRCDIR)/packet.o: $(SRCDIR)/packet.cpp $(SRCDIR)/packet.h $(SRCDIR)/protocol.h
$(SRCDIR)/fragmenter.o: $(SRCDIR)/fragmenter.cpp $(SRCDIR)/fragmenter.h $(SRCDIR)/packet.h
$(SRCDIR)/assembler.o: $(SRCDIR)/assembler.cpp $(SRCDIR)/assembler.h $(SRCDIR)/packet.h
$(SRCDIR)/retransmission.o: $(SRCDIR)/retransmission.cpp $(SRCDIR)/retransmission.h $(SRCDIR)/packet.h
$(SRCDIR)/epoll_loop.o: $(SRCDIR)/epoll_loop.cpp $(SRCDIR)/epoll_loop.h
$(SRCDIR)/reliable_udp.o: $(SRCDIR)/reliable_udp.cpp $(SRCDIR)/reliable_udp.h $(SRCDIR)/packet.h $(SRCDIR)/fragmenter.h $(SRCDIR)/assembler.h $(SRCDIR)/retransmission.h $(SRCDIR)/epoll_loop.h

# Зависимости file transfer
file_storage.o: file_storage.cpp file_storage.h file_protocol.h
file_server.o: file_server.cpp file_server.h file_protocol.h file_storage.h
file_client.o: file_client.cpp file_client.h file_protocol.h file_storage.h
