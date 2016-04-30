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
