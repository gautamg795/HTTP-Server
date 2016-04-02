CXXFLAGS = -O3 -std=c++11 -Wall -Wextra
LDFLAGS = -lpthread

SRCDIR = ./src
OBJDIR = ./build
OBJS = $(addprefix $(OBJDIR)/,HTTPRequest.o HTTPResponse.o)
all: web-server web-client

debug: CXXFLAGS = -O0 -std=c++11 -Wall -Wextra -D_DEBUG
debug: all

web-client: $(OBJS) $(SRCDIR)/web-client.cpp
	$(CXX) -o $@ $(CXXFLAGS) $^ $(LDFLAGS)

web-server: $(OBJS) $(OBJDIR)/HTTPServer.o $(SRCDIR)/web-server.cpp
	$(CXX) -o $@ $(CXXFLAGS) $^ $(LDFLAGS)

$(OBJDIR)/HTTPRequest.o: $(SRCDIR)/HTTPRequest.cpp $(SRCDIR)/HTTPRequest.h $(SRCDIR)/logging.h
	$(CXX) -c -o $@ $(CXXFLAGS) $(SRCDIR)/HTTPRequest.cpp

$(OBJDIR)/HTTPResponse.o: $(SRCDIR)/HTTPResponse.cpp $(SRCDIR)/HTTPResponse.h $(SRCDIR)/logging.h
	$(CXX) -c -o $@ $(CXXFLAGS) $(SRCDIR)/HTTPResponse.cpp

$(OBJDIR)/HTTPServer.o: $(SRCDIR)/HTTPServer.cpp $(SRCDIR)/HTTPServer.h $(SRCDIR)/logging.h $(OBJS)
	$(CXX) -c -o $@ $(CXXFLAGS) $(SRCDIR)/HTTPServer.cpp

$(OBJS): | $(OBJDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR)
	rm -f web-server-client.tar.gz
	rm -rf *.dSYM/
	rm -f web-server web-client

dist: clean
	tar czf web-server-client.tar.gz ./*

.PHONY: all clean debug dist
