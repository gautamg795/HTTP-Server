# HTTP Server and Client
### CS118 Project 1 Spring '16
##### Gautam Gupta, Kelly Hosokawa, and David Pu

## Compilation
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

# Design/Architecture
## HTTP Request/Response
HTTP Response and Request classes were created to encapsulate the various fields of requests and responses used by both client and server.
Both `HTTPRequest` and `HTTPResponse` classes have two constructors, a default-initializing one, and a constructor taking a
`std::string` representing the raw wire-encoded request/response text. The latter constructor takes an optional `std::string* remainder`
parameter for returning excess characters from the request/response to the caller.

The request and response have hardcoded `std::string` fields for the version, verb, and status code parameters, and then a
`std::unordered_map<std::string, std::string>` container is used for storing HTTP headers and their associated values.
## Server
The server was designed as a class, `HTTPServer`, that can be instantiated with three parameters: hostname, port, and serving directory.

It has three public methods:
* `void install_signal_handler()`: Used to install a signal handler for `SIGINT`, `SIGTERM`, `SIGCHLD`, and `SIGPIPE` to either ignore, or end the run loop, as necessary.
* `void run()`: Used to run the server synchronously as configured by the constructor.
* `void run_async()` Used to run the server asynchronously as configured by the constructor.

The constructor handles parsing of the initial parameters. The `directory` parameter is tilde and glob-expanded, then `chdir()` is run
to set the server's current working directory. The `hostname` and `port` parameters are used with `getaddrinfo()` to create and bind
to the appropriate TCP socket.
### Synchronous Server
The `run()` method starts the synchronous server. It begins listening on socket created in the constructor, and enters a run loop which can be canceled through the aforementioned signal handler. Whenever a new connection is `accept()`'d by the server, a new `std::thread` is spawned running the `process_request()` private function.

This function `recv()`'s from the socket, creates a `HTTPRequest` object, and if valid, attempts to open the
referenced file. If the file is not found or an error is encountered, a HTTP 404 response is sent back. Otherwise, the file is opened for reading, and a HTTP 200 response is sent back, followed by the file's data
via `sendfile()`.

As part of processing the incoming request, the HTTP Version and `Connection` header are examined to determine
if persistent connections should be used, defaulting to persistent for HTTP/1.1 and non-persistent for 1.0, unless otherwise specified. The server always sends HTTP/1.1 responses.

After sending the response, the thread either closes the connection and dies (for non-persistent connections), or loops back to the beginning of the function to read another request.

In the latter case, the read buffer is checked to see if it already contains another request—it is possible the client sent a pipelined sequence of requests, in which case we can process the next request without reading
more data from the client.

Additionally, `select` is used to put a timeout on the receiving socket, so that the server will wait no more than 10 seconds for the client to send a request. (This could also be accomplished with a `setsocketopt` operation, as we do on the client)
### Asynchronous Server
The `run_async()` method starts the asynchronous server's main loop, which uses `poll()` to asynchronously service all the client sockets.

We created a `ClientState` struct to represent the state of a connected client, so that on each cycle of polling we can continue the operation in progress.
We also have a vector of file descriptors which is passed in to `poll()`.

Like the synchronous server, we accept a connection, then add it to the file descriptor pool. When the socket is ready to read, we receive a fixed amount of data from the socket each cycle until a complete request (denoted by the presence of `\r\n\r\n`) is read. We then set the `ClientState` object to denote a writing mode, in which we will write the response, piece by piece, and send the requested file, piece by piece.

Because we limit how much work is done at a time, and we have no potentially blocking operations, the asynchronous server scales well to having many clients without worrying about spawning too many threads.

We also implement persistent connections on this async server, by reading the HTTP Version and `Connection` header to determine if we should try to receive another request after sending the last response.
* * *
## Client
We used a regular expression to parse the URLs into hostname, port, and path; we then  store the data in an `std::unordered_map<std::string, URL>` where we mapped strings (hostname + port number) to URLs contained in a vector. The URL is a struct we created to hold the different parts of a URL. URLs with the same hostname and port number have the same key and are stored in the same vector of URLs, allowing us to use persistent connections for all requested files on the same host/port, and a new connection for a different host/port pair.

For each key (unique hostname and port number), we iterate over the URLs stored in the associated vector and attempt to download the files. We use `getaddrinfo` to resolve the host and port into a IP address usable by the socket API. We also use `setsockopt` to specify that all sockets used have read and write timeouts of 10 seconds.
Assuming that we are able to successfully connect to a socket, we create a HTTP request (set the path, version, and header information such as connection type and hostname) and write it to the socket. Then we read the response from the socket and save the data to the current working directory. The filename is extracted from the last component of the path, or `index.html` if unspecified.

**We initially attempt to make all requests using HTTP/1.1 persistent connections.** We set the HTTP version to 1.1, and set the `Connection: keep-alive` header before writing the request to the connected socket.
When reading the response, we read from the socket and save the contents in a buffer, increasing its size as needed. As soon as we have all non-body data from the response, we parse it to determine if persistent connections are supported, and if a content-length is provided. If so, we resize the buffer to the provided length.  Assuming that no timeout occurs due to inactivity from the server we write the body of the response and move on to the next file from this host, or to the next host. 

If the headers of the response indicate that persistent connections are not supported or that a content-length has not been provided, we fall back to non-persistent connections and set the `Connection: close` header on all further downloads. We have a separate function in the client for non-persistent connections, though it differs only in that it will open a new connection for each file.
