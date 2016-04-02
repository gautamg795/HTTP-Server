CXXFLAGS = -Ofast -std=c++11 -Wall -Wextra -g
LDFLAGS = -lpthread

# The directory for the build files, may be overridden on make command line.
builddir = .

all: $(builddir)/web-server $(builddir)/web-client

debug: CXXFLAGS = -O0 -g -std=c++11 -Wall -Wextra -D_DEBUG -lpthread
debug: all

$(builddir)/web-client: $(builddir)/HTTPRequest.o $(builddir)/HTTPResponse.o web-client.cpp
	$(CXX) -o $@ $(CXXFLAGS) $^ $(LDFLAGS) 

$(builddir)/web-server: $(builddir)/HTTPRequest.o $(builddir)/HTTPResponse.o $(builddir)/HTTPServer.o $(builddir)/RequestProcessor.o web-server.cpp
	$(CXX) -o $@ $(CXXFLAGS) $^ $(LDFLAGS) 

$(builddir)/HTTPRequest.o: HTTPRequest.cpp HTTPRequest.h
	$(CXX) -c -o $@ $(CXXFLAGS) HTTPRequest.cpp

$(builddir)/HTTPResponse.o: HTTPResponse.cpp HTTPResponse.h
	$(CXX) -c -o $@ $(CXXFLAGS) HTTPResponse.cpp

$(builddir)/HTTPServer.o: HTTPServer.cpp HTTPServer.h
	$(CXX) -c -o $@ $(CXXFLAGS) HTTPServer.cpp

$(builddir)/RequestProcessor.o: RequestProcessor.cpp RequestProcessor.h
	$(CXX) -c -o $@ $(CXXFLAGS) RequestProcessor.cpp

clean:
	rm -f *.o *.d
	rm -f web-server-client.tar.gz
	rm -rf *.dSYM/
	rm -f $(builddir)/web-server $(builddir)/web-client

dist: clean
	tar czf web-server-client.tar.gz ./*

.PHONY: all clean debug dist
