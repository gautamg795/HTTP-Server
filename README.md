# HTTP Server and Client
### CS118 Project 1 Spring '16
##### Gautam Gupta, Kelly Hosokawa, and David Pu

## Building
The Vagrantfile has been modified to install `gcc-5` and to increase available memory to 1536MB
(to account for larger file downloads on the client, which temporarily loads responses into memory).
The new version of `gcc` is set as default, so the Makefile's default option for `CXX` picks the new `gcc`
The following commands were added to the shell provisioner:
```
sudo add-apt-repository ppa:ubuntu-toolchain-r/test # add the ubuntu toolchain ppa
sudo apt-get update
sudo apt-get install -y git build-essential gcc-5 g++-5 gdb # install newer versions of gcc
sudo apt-get upgrade -y
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-5 60 --slave /usr/bin/g++ g++ /usr/bin/g++-5
```
Once in the Vagrant box, `cd` to `/vagrant` to access the project directory.

The full client, server, and async-server can be 
built by running `make` to produce three executables, `web-client`, `web-server`, and `web-server-async`.
(Intermediate object files will be put in a `build` subdirectory to allow for reuse during linking, and all source files are in
the `src` subdirectory)

Additionally, a (very) verbose debug build can be created by issuing the `make debug` command.

## Extra Credit Attempted
* Timeout: Both client and server handle request/response timeout, with values set at 10 seconds on both. 
* Asynchronous and synchronous server: Two server executables are created, `web-server` and `web-server-async`.
The latter is implemented using `poll`, and the former simply spawns a new `std::thread` for each new request.
* HTTP/1.1: Both client and server support HTTP/1.1 persistent connections, and the server fully supports pipelined requests,
and the client uses persistent connections for all URLs given on the same host, falling back to non-persistent as necessary.

## Design/Architecture
### HTTP Request/Response
HTTP Response and Request classes were created to encapsulate the various fields of requests and responses used by both client and server.
Both `HTTPRequest` and `HTTPResponse` classes have two constructors, a default-initializing one, and a constructor taking a 
`std::string` representing the raw wire-encoded request/response text. The latter constructor takes an optional `std::string* remainder`
parameter for returning excess characters from the request/response to the caller. 

The request and response have hardcoded `std::string` fields for the version, verb, and status code parameters, and then a 
`std::unordered_map<std::string, std::string>` container is used for storing HTTP headers and their associated values. 

### Server
The server was designed as a class, `HTTPServer`, that can be instantiated with three parameters: hostname, port, and serving directory.

It has three public methods:
* `void install_signal_handler()`: Used to install a signal handler for `SIGINT`, `SIGTERM`, `SIGCHLD`, and `SIGPIPE` to either ignore, or end the run loop, as necessary. 
* `void run()`: Used to run the server synchronously as configured by the constructor.
* `void run_async()` Used to run the server asynrchonously as configured by the constructor.

The constructor handles parsing of the initial parameters. The `directory` parameter is tilde and glob-expanded, then `chdir()` is run
to set the server's current workign directory. The `hostname` and `port` parameters are used with `getaddrinfo()` to create and bind
to the appropriate TCP socket.
##### Synchronous Server
##### Asynchronous Server
### Client
Client architecture here
