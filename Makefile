USERID=
CXXFLAGS = -O3 -std=c++11 -Wall -Wextra -g
LDFLAGS = -lpthread

SRCDIR = ./src
OBJDIR = ./build
OBJS = $(addprefix $(OBJDIR)/,HTTPRequest.o HTTPResponse.o)
all: web-server web-client web-server-async

debug: CXXFLAGS = -O0 -std=c++11 -Wall -Wextra -D_DEBUG -g
debug: all

# Executables
web-client: $(OBJS) $(SRCDIR)/web-client.cpp
	$(CXX) -o $@ $(CXXFLAGS) $^ $(LDFLAGS)

web-server: $(OBJS) $(OBJDIR)/HTTPServer.o $(SRCDIR)/web-server.cpp
	$(CXX) -o $@ $(CXXFLAGS) $^ $(LDFLAGS)

web-server-async: $(OBJS) $(OBJDIR)/HTTPServer.o $(SRCDIR)/web-server-async.cpp
	$(CXX) -o $@ $(CXXFLAGS) $^ $(LDFLAGS)

# Object files
$(OBJDIR)/HTTPRequest.o: $(SRCDIR)/HTTPRequest.cpp $(SRCDIR)/HTTPRequest.h $(SRCDIR)/logging.h
	$(CXX) -c -o $@ $(CXXFLAGS) $(SRCDIR)/HTTPRequest.cpp

$(OBJDIR)/HTTPResponse.o: $(SRCDIR)/HTTPResponse.cpp $(SRCDIR)/HTTPResponse.h $(SRCDIR)/logging.h
	$(CXX) -c -o $@ $(CXXFLAGS) $(SRCDIR)/HTTPResponse.cpp

$(OBJDIR)/HTTPServer.o: $(SRCDIR)/HTTPServer.cpp $(SRCDIR)/HTTPServer.h $(SRCDIR)/logging.h $(OBJS)
	$(CXX) -c -o $@ $(CXXFLAGS) $(SRCDIR)/HTTPServer.cpp

# Ensure $(OBJDIR) exists
$(OBJS): | $(OBJDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR)
	rm -f web-server-client.tar.gz
	rm -rf *.dSYM/
	rm -f web-server web-client web-server-async

tarball: req-user-id clean
	tar czf $(USERID).tar.gz ./*

req-user-id:
ifndef USERID
	$(error Run `make tarball USERID=xxx`)
endif

.PHONY: all clean debug tarball req-user-id
