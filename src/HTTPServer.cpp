#include "HTTPServer.h"
#include "HTTPRequest.h"   // for HTTPRequest, operator<<
#include "HTTPResponse.h"  // for HTTPResponse
#include "logging.h"       // for LOG_END, LOG_ERROR, LOG_INFO

#ifndef __APPLE__
#include <sys/sendfile.h>  // for sendfile
#endif

#include <arpa/inet.h>     // for inet_ntoa
#include <fcntl.h>         // for open, O_RDONLY
#include <netdb.h>         // for addrinfo, freeaddrinfo, gai_strerror, geta...
#include <netinet/in.h>    // for IPPROTO_TCP, sockaddr_in
#include <poll.h>          // for poll
#include <sys/select.h>    // for select
#include <sys/socket.h>    // for send, accept, bind, listen, recv, setsockopt
#include <sys/stat.h>      // for fstat, stat
#include <sys/time.h>      // for timeval
#include <unistd.h>        // for close, off_t, read, ssize_t
#include <wordexp.h>       // for wordexp

#include <algorithm>       // for transform
#include <cctype>          // for tolower
#include <cerrno>          // for errno, EINTR
#include <csignal>         // for sigaction, SIGINT, SIGTERM, etc
#include <cstdlib>         // for exit
#include <cstring>         // for strerror, memset
#include <exception>       // for exception
#include <iostream>        // for operator<<, basic_ostream, ostream, cout
#include <regex>           // for regex_replace, regex, regex_traits
#include <string>          // for char_traits, string, operator<<, operator==
#include <thread>          // for thread
#include <type_traits>     // for move
#include <vector>          // for vector

static bool keep_running = true;
int HTTPServer::timeout = 10;

/**
 * @summary struct used by run_async to keep track of each client's connection
 */
struct ClientState
{
    enum {
        READ,
        WRITE_RESPONSE,
        WRITE_FILE
    }           state_ = READ;
    std::string buf_;
    std::string remainder_;
    off_t       pos_ = 0;
    int         filefd_ = -1;
    bool        file_ok_ = true;
    bool        keep_alive_ = false;
};

bool HTTPServer::set_conn_type(const HTTPRequest& req, HTTPResponse& resp)
{
    bool keep_alive = req.version() != "HTTP/1.0";
    auto connection = req.header_value("Connection");
    if (connection)
    {
        std::string conn_str = *connection;
        std::transform(conn_str.begin(), conn_str.end(),
                conn_str.begin(), ::tolower);
        if (conn_str == "close")
        {
            keep_alive = false;
        }
        else if (conn_str == "keep-alive")
        {
            keep_alive = true;
        }
    }
    if (keep_alive)
    {
        resp.set_header("Connection", "keep-alive");
        resp.set_header("Keep-Alive", "timeout=" +
                std::to_string(HTTPServer::timeout));
    }
    else
    {
        resp.set_header("Connection", "close");
    }
    return keep_alive;
}

/**
 * @summary Constructs the HTTPServer, performs hostname lookup, binds the
 * socket, changes directory as necessary.
 *
 * @param hostname hostname (or IP) to bind to
 * @param port the numeric port to attach to
 * @param directory the directory from which to serve files
 *                      parses ~ and spaces if possible
 */
HTTPServer::HTTPServer(const std::string& hostname,
                       const std::string& port,
                       const std::string& directory) :
    hostname_(hostname), port_(port), directory_(directory), sockfd_(-1)
{
    // Escape spaces in the directory name so we can cd there
    directory_ = std::regex_replace(directory_, std::regex(R"(([^\\]) )"), R"($1\ )");
    // Expand ~ to the user's home directory
    wordexp_t expansion;
    if (wordexp(directory_.c_str(), &expansion, 0) != 0 || expansion.we_wordc < 1)
    {
        LOG_ERROR << "Error expanding directory path given" << LOG_END;
        std::exit(1);
    }
    // cd to `directory`
    if(chdir(expansion.we_wordv[0]) < 0)
    {
        LOG_ERROR << "chdir(): " << std::strerror(errno)
                  << ": " << expansion.we_wordv[0] << LOG_END;
        std::exit(1);
    }
    LOG_INFO << "Changed directory to " << expansion.we_wordv[0] << LOG_END;
    wordfree(&expansion);
    LOG_INFO << "Initializing HTTP server at "
             << hostname << ':' << port
             << " serving files from " << directory << LOG_END;

    // Resolve `hostname` to an IP address
    // `hints` is used to specify what optins we want
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_protocol = IPPROTO_TCP; // TCP protocol
    hints.ai_socktype = SOCK_STREAM; // Streaming socket
    hints.ai_family = AF_INET; // IPv4
    hints.ai_flags = AI_NUMERICSERV; // Port is a number

    struct addrinfo* res;
    int ret = getaddrinfo(hostname_.c_str(), port_.c_str(), &hints, &res);
    if (ret != 0)
    {
        LOG_ERROR << gai_strerror(ret) << LOG_END;
        std::exit(ret);
    }
    // getaddrinfo populates res as a linked list of results
    // we iterate through them until we find one that we can use
    auto ptr = res;
    for (; ptr != nullptr; ptr = ptr->ai_next)
    {
        // Make the socket file descriptor
        sockfd_ = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sockfd_ == -1)
        {
            LOG_ERROR << "socket(): " << std::strerror(errno) << LOG_END;
            continue;
        }

        // Tell the file descriptor it's okay to reuse a socket that wasn't
        // cleaned up properly
        const int one = 1;
        if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)) == -1)
        {
            LOG_ERROR << "setsockopt(): " << std::strerror(errno) << LOG_END;
            std::exit(1);
        }

        // Bind the socket to the address we got from getaddrinfo
        int ret = bind(sockfd_, ptr->ai_addr, ptr->ai_addrlen);
        if (ret == -1)
        {
            LOG_ERROR << "bind(): " << std::strerror(errno) << LOG_END;
            close(sockfd_);
            continue;
        }
        break;
    }
    // If we make it to the end of the linked list, we couldn't bind to anything
    if (ptr == nullptr)
    {
        LOG_ERROR << "Failed to bind socket to " << hostname_
                  << ':' << port_ << LOG_END;
        exit(1);
    }
    LOG_INFO << "Hostname resolved to "
              << inet_ntoa(((sockaddr_in*)ptr->ai_addr)->sin_addr)
              << LOG_END;

    // Free the linked list created by getaddrinfo
    freeaddrinfo(res);
}

HTTPServer::~HTTPServer()
{
    LOG_INFO << "Shutting down HTTP server..." << LOG_END;
    // Close the file descriptor we were bound to
    close(sockfd_);
}

/**
 * @summary Adds a signal handler for SIGINT and SIGTERM to shut down server
 * (This handles CTRL-C)
 */
void HTTPServer::install_signal_handler() const
{
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    // The function be called when a signal is received -- it sets keep_running
    // to false
    action.sa_handler = [](int) { keep_running = false; };
    // Call that function if we get SIGINT or SIGTERM
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    action.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &action, nullptr);
    sigaction(SIGCHLD, &action, nullptr);
}

/**
 * @summary Run server synchronously; spawns a new thread to process_request
 * whenever a new request comes in on accept()
 */
void HTTPServer::run()
{
    // Listen on sockfd_ with a maximum of 64 requests in the backlog
    if(listen(sockfd_, 64) != 0)
    {
        LOG_ERROR << "listen(): " << std::strerror(errno) << LOG_END;
        return;
    }
    LOG_INFO << "Listening on port " << port_ << LOG_END;
    // Loop until a signal tells us to stop
    while (keep_running)
    {
        // accept a connection from a client
        int temp_fd = accept(sockfd_, nullptr, nullptr);
        if (temp_fd < 0)
        {
            // errno will be EINTR if accept() was interrupted by a signal
            // we just try again if that happens
            if (errno == EINTR)
            {
                continue;
            }
            LOG_ERROR << "accept(): " << strerror(errno) << LOG_END;
            return;
        }
        try
        {
            // Spawn a thread to process the request on temp_fd and detach the
            // thread so it can continue working without worrying about its
            // parent
            std::thread(process_request, temp_fd).detach();
        } catch (const std::exception& ex)
        {
            close(temp_fd);
            LOG_ERROR << "std::thread(): " << ex.what() << LOG_END;
        }
    }
}

/**
 * @summary Asynchronously run the server with non-blocking sockets.
 * Uses poll() to check when sockets are ready for read/write and loops through
 * the ready sockets, reading/writing as necessary
 * (this is for the extra credit)
 */
void HTTPServer::run_async()
{
    // Put the socket in non-blocking mode
    fcntl(sockfd_, F_SETFL, O_NONBLOCK);

    // vector of pollfd used to tell poll() when to notify us
    std::vector<struct pollfd> fds(std::max(256, sockfd_), {-1, POLLIN, 0});

    // vector of ClientState used to keep track of a client's connection state
    // (so we know when to write, read, etc)
    std::vector<ClientState> clientstates(fds.size());

    // Initialize with the server's main socket, set to notify when ready to
    // read
    fds[sockfd_] = {sockfd_, POLLIN, 0};
    if (listen(sockfd_, 64) != 0)
    {
        LOG_ERROR << "listen(): " << std::strerror(errno) << LOG_END;
        return;
    }
    LOG_INFO << "Listening on port " << port_ << LOG_END;
    while (keep_running)
    {
        // Wait until an fd is ready for read or write
        int ret = poll(&fds[0], fds.size(), -1);
        if (ret == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            LOG_ERROR << "poll(): " << std::strerror(errno) << LOG_END;
        }
        // Iterate over all sockets
        for (const struct pollfd & poll_fd : fds)
        {
            // socket is ready for read
            if (poll_fd.fd > -1 && poll_fd.revents & POLLIN)
            {
                // it's the main server socket
                if (poll_fd.fd == sockfd_)
                {
                    // accept the connection, set the new fd to nonblocking
                    // mode, and store it in the fds array.
                    int temp_fd = accept(sockfd_, nullptr, nullptr);
                    if (temp_fd < 0)
                    {
                        LOG_ERROR << "accept(): " << strerror(errno) << LOG_END;
                    }
                    fcntl(temp_fd, F_SETFL, O_NONBLOCK);
                    // resize if we have to
                    if (fds.size() < (unsigned)temp_fd)
                    {
                        fds.resize(temp_fd + 64, {-1, POLLIN, 0});
                        clientstates.resize(temp_fd + 64);
                        fds[temp_fd] = {temp_fd, POLLIN, 0};
                        break;
                    }
                    // POLLIN so we know when to read from that socket
                    fds[temp_fd] = {temp_fd, POLLIN, 0};
                }
                else // Ready to read from a client socket
                {
                    // Use 'state' as local variable to avoid long typing
                    ClientState& state = clientstates[poll_fd.fd];
                    // Get the remainder of a incomplete request, if necessary
                    state.buf_ = std::move(state.remainder_);
                    // Start writing where we left off
                    state.pos_ = state.buf_.size();
                    if (state.buf_.find("\r\n\r\n") == std::string::npos)
                    {
                        // Make sure we have some room to read
                        state.buf_.resize(256 + state.buf_.size());
                        // Read as much as we can
                        int bytes_read = recv(poll_fd.fd, &state.buf_[state.pos_],
                                state.buf_.size() - state.pos_, 0);
                        state.pos_ += bytes_read;
                        // If recv returned 0, the client disconnected
                        if (bytes_read == 0)
                        {
                            LOG_INFO << "Connection closed by peer" << LOG_END;
                            close(poll_fd.fd);
                            fds[poll_fd.fd].fd = -1;
                            state = ClientState();
                            continue;
                        }
                        // Error check
                        if (bytes_read < 0)
                        {
                            // EWOULDBLOCK means the client wasn't actually ready
                            // so try again later
                            if (errno == EWOULDBLOCK)
                            {
                                LOG_INFO << "Read would block" << LOG_END;
                                continue;
                            }
                            LOG_ERROR << "read(): " << std::strerror(errno) << LOG_END;
                            close(poll_fd.fd);
                            fds[poll_fd.fd].fd = -1;
                            state = ClientState();
                            break;
                        }
                    }
                    state.buf_.resize(state.pos_);
                    // Check if we've read a full request in now
                    if (state.buf_.find("\r\n\r\n") != std::string::npos)
                    {
                        // If so, try to parse it
                       HTTPRequest request;
                       HTTPResponse response;
                       response.set_version("HTTP/1.1");
                       try
                       {
                           HTTPRequest _request(state.buf_, &state.remainder_);
                           request = std::move(_request);
                       } catch (const std::exception& ex)
                       {
                           LOG_ERROR << "HTTPRequest construction failed: "
                                     << ex.what() << LOG_END;
                           response.make_400();
                           state.keep_alive_ = true;
                           response.set_header("Connection", "keep-alive");
                           state.file_ok_ = false;
                           // Store the prepared response to send on our next cycle
                           state.buf_ = response.to_string();
                           state.pos_ = 0;
                           // Set the state to WRITE_RESPONSE so we know what to do
                           state.state_ = ClientState::WRITE_RESPONSE;
                           // We want to be notified when the client is ready to
                           // receive data
                           fds[poll_fd.fd].events = POLLOUT;
                           continue;
                       }
                       // Set persistent connection as necessary
                       state.keep_alive_ = set_conn_type(request, response);
                       LOG_INFO << "Request recieved:\n"
                           << request << LOG_END;
                       // Start working on the response
                       // Try to open the file
                       state.filefd_ = open(("." + request.path()).c_str(), O_RDONLY);
                       if (state.filefd_ < 0)
                       {
                           LOG_ERROR << "open(): " << std::strerror(errno)
                                     << " opening file ." << request.path()
                                     << LOG_END;
                           state.file_ok_ = false;
                       }
                       // Get the file size, if we succeeded
                       off_t filesize = 0;
                       if (state.file_ok_)
                       {
                           struct stat filestat;
                           if (fstat(state.filefd_, &filestat) != -1)
                           {
                               // Make sure it's a regular file
                               if (!S_ISREG(filestat.st_mode))
                                   state.file_ok_ = false;
                               filesize = filestat.st_size;
                           }
                           else
                               state.file_ok_ = false;
                       }
                       // If we failed to open/stat/anything the file at any
                       // point, send back a 404
                       if (!state.file_ok_)
                       {
                           LOG_INFO << "Response: HTTP/1.1 404 Not Found"
                                    << LOG_END;
                           response.make_404();
                       }
                       // Otherwise prepare the 200 response
                       else
                       {
                           LOG_INFO << "Response: HTTP/1.1 200 OK" << LOG_END;
                           response.set_status("200");
                           response.set_phrase("OK");
                           if (state.file_ok_)
                           {
                               // Set the content-length header
                               response.set_header("Content-Length",
                                                   std::to_string(filesize));
                           }
                       }
                       // Store the prepared response to send on our next cycle
                       state.buf_ = response.to_string();
                       state.pos_ = 0;
                       // Set the state to WRITE_RESPONSE so we know what to do
                       state.state_ = ClientState::WRITE_RESPONSE;
                       // We want to be notified when the client is ready to
                       // receive data
                       fds[poll_fd.fd].events = POLLOUT;
                    }
                    else // We don't have a full request yet
                    {
                        // So save what we have and continue next cycle
                        state.buf_.resize(state.pos_);
                        state.remainder_ = std::move(state.buf_);
                    }
                }

            }
            // A client is ready to receive data from us
            else if (poll_fd.fd > -1 && poll_fd.revents & POLLOUT)
            {
                ClientState& state = clientstates[poll_fd.fd];
                // If we are currently writing the response headers
                if (state.state_ == ClientState::WRITE_RESPONSE)
                {
                    // Send from the buffer, keeping track of how much has been
                    // sent so we can continue next cycle, if necessary
                    ssize_t bytes_written = send(poll_fd.fd,
                                                 &state.buf_[state.pos_],
                                                 state.buf_.size() - state.pos_,
                                                 0);
                    state.pos_ += bytes_written;
                    if (bytes_written < 0)
                    {
                        // Again, EWOULDBLOCK means client wasn't ready, so try
                        // again later
                        if (errno == EWOULDBLOCK)
                        {
                            continue;
                        }
                        LOG_ERROR << "send(): " << std::strerror(errno) << LOG_END;
                        close(poll_fd.fd);
                        close(state.filefd_);
                        state = ClientState();
                        fds[poll_fd.fd].fd = -1;
                    }
                    // If there's nothing else to write, now we can start
                    // writing the file instead (if there is one)
                    else if (bytes_written == 0 || state.pos_ == (off_t)state.buf_.size())
                    {
                        if (state.file_ok_)
                        {
                            // Set state to WRITE_FILE, and get ready to write
                            // the file
                            state.state_ = ClientState::WRITE_FILE;
                            state.pos_ = 0;
                            state.buf_.clear();
                            state.buf_.resize(2048);
                        }
                        else // No file to be sent, just finish up now
                        {
                            // Get back into READ mode if keep-alive
                            if (state.keep_alive_)
                            {
                                fds[poll_fd.fd].events = POLLIN;
                                state.state_ = ClientState::READ;
                                state.pos_ = 0;
                                state.filefd_ = -1;
                            }
                            // Otherwise close the connection
                            else
                            {
                                close(poll_fd.fd);
                                close(state.filefd_);
                                state = ClientState();
                                fds[poll_fd.fd].fd = -1;
                            }
                        }
                    }
                }
                // We are ready to write the file
                else if (state.state_ == ClientState::WRITE_FILE)
                {
                    // Read as much into the buffer as we can
                    ssize_t bytes_read = read(state.filefd_, &state.buf_[0], state.buf_.size());
                    // Then write it to the client
                    ssize_t bytes_written = send(poll_fd.fd, &state.buf_[0], bytes_read, 0);
                    if (bytes_read < 0 || bytes_written < 0)
                    {
                        LOG_ERROR << "read()/send(): "
                                  << std::strerror(errno) << LOG_END;
                        close(poll_fd.fd);
                        close(state.filefd_);
                        fds[poll_fd.fd].fd = -1;
                        state = ClientState();
                    }
                    // If there's nothing else to read from the file
                    if (bytes_read == 0)
                    {
                        // Close the file
                        close(state.filefd_);
                        // Go back in READ mode if keep-alive
                        if (state.keep_alive_)
                        {
                            fds[poll_fd.fd].events = POLLIN;
                            state.state_ = ClientState::READ;
                            state.pos_ = 0;
                            state.filefd_ = -1;
                        }
                        // Else close the connection
                        else
                        {
                            close(poll_fd.fd);
                            fds[poll_fd.fd].fd = -1;
                            state = ClientState();
                        }
                    }
                }
            }
        }
    }
}

/**
 * @summary Synchronously reads and responds to the request on 'socket'
 * Should be run by a separate thread
 * Handles keep-alive if necessary
 *
 * @param socket the file descriptor returned by accept()
 */
void HTTPServer::process_request(int socket)
{
    // String to hold partial requests in between read/write cycles
    std::string remainder;
    while(true)
    {
        // Check if the 'remainder' string contains a full request (so we don't
        // need to read more from the client)
        bool have_complete_request = remainder.find("\r\n\r\n") != std::string::npos;
        // Next three variables are used by select() to monitor a socket
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(socket, &fdset);
        struct timeval timeout;
        int ret;
        // if we don't have a complete request yet, wait at most
        // HTTPServer::timeout seconds for data from the client
        if (!have_complete_request)
        {
            timeout.tv_sec = HTTPServer::timeout;
            timeout.tv_usec = 0;
            // Monitor the socket, waiting `timeout` seconds for data
            ret = select(socket+1, &fdset, nullptr, nullptr, &timeout);
        }
        else
        {
            ret = 0;
        }
        // 0 means we timed out
        if (ret == 0 && remainder.empty()) // keepalive timeout only applies between requests
        {
            LOG_INFO << "Keepalive timeout, closing connection" << LOG_END;
            close(socket);
            return;
        }
        if (ret < 0)
        {
            LOG_ERROR << "select(): " << std::strerror(errno) << LOG_END;
            return;
        }
        // Start with what remains from the previous cycle, if any
        std::string buf = remainder;
        // Start writing where we left off
        off_t pos = buf.size();
        ssize_t bytes_read = 0;
        if (!have_complete_request) // get more data if we don't have a full request
        {
            // Make some room
            buf.resize(256 + buf.size());
            do {
                bytes_read = recv(socket, &buf[pos], buf.size() - pos, 0);
                pos += bytes_read;
                if (buf.size() - pos < 8)
                {
                    buf.resize(buf.size() * 2);
                }
                // Stop trying to read if we have a full request
                if (buf.find("\r\n\r\n") != std::string::npos)
                    break;
            } while (bytes_read > 0);
            if (bytes_read < 0)
            {
                LOG_ERROR << "recv(): " << std::strerror(errno) << LOG_END;
                return;
            }
            // recv returning 0 means client disconnected
            else if (bytes_read == 0)
            {
                LOG_INFO << "Connection closed by peer" << LOG_END;
                close(socket);
                return;
            }
            // Shrink buffer to fit
            buf.resize(pos);
        }
        // Try to parse the request
        bool request_ok = true;
        bool file_ok = false;
        int filefd = -1;
        off_t filesize = 0;
        HTTPRequest request;
        HTTPResponse response;
        try
        {
            HTTPRequest _request(buf, &remainder);
            // If we succesfully constructed the request, move it back to the
            // original variable
            request = std::move(_request);
        } catch (const std::exception& ex)
        {
            LOG_ERROR << "HTTPRequest construction failed: " << ex.what() << LOG_END;
            request_ok = false;
            response.make_400();
        }
        // Set persistent if necessary
        response.set_version("HTTP/1.1");
        set_conn_type(request, response);
        if (request.verb() != "GET")
        {
            LOG_ERROR << "Non-GET request received" << LOG_END;
            request_ok = false;
            response.make_501();
        }
        if (request_ok)
        {
            LOG_INFO << "Request recieved:\n"
                << request << LOG_END;
            response.set_version("HTTP/1.1");
            LOG_INFO << "Attempting to open file at " << '.'
                << request.path() << LOG_END;
            // Flag that we set to false if file opening fails at any point
            file_ok = true;
            // Prepend '.' because the path starts with a '/'
            filefd = open(("." + request.path()).c_str(), O_RDONLY);
            if (filefd < 0)
            {
                LOG_ERROR << "open(): " << std::strerror(errno) << " opening file "
                    << "." << request.path() << LOG_END;
                file_ok = false;
            }
            // Try to get the filesize, if it opened correctly
            if (file_ok) {
                struct stat filestat;
                if (fstat(filefd, &filestat) != -1)
                {
                    // Make sure we opened a regular file
                    if (!S_ISREG(filestat.st_mode))
                        file_ok = false;
                    filesize = filestat.st_size;
                }
                else
                    file_ok = false;
            }
            // If finding a file fails at any point, send a 404
            if (!file_ok)
            {
                LOG_INFO << "Response: HTTP/1.1 404 Not Found" << LOG_END;
                response.make_404();
            }
            else
            {
                LOG_INFO << "Response: HTTP/1.1 200 OK" << LOG_END;
                response.set_status("200");
                response.set_phrase("OK");
                // Set the content-length header if we opened a file
                if (file_ok)
                {
                    response.set_header("Content-Length", std::to_string(filesize));
                }
            }
        }
        // Generate the response text we're sending back
        std::string response_text = response.to_string();
        // Loop and write it to the client
        pos = 0;
        ssize_t bytes_written = 0;
        do
        {
            bytes_written = send(socket, &response_text[pos],
                                response_text.size() - pos, 0);
            pos += bytes_written;
        } while (bytes_written > 0);
        // Now send the file, if we opened it succesfully
        if (file_ok)
        {
            bytes_written = 0;
            pos = 0;
            #ifdef __APPLE__
            // sendfile broken on Mac, send the file by reading/writing
            {
                char buf[8192];
                do {
                    bytes_read = read(filefd, buf, 8192);
                    bytes_written = send(socket, buf, bytes_read, 0);
                } while (bytes_read > 0 && bytes_written > 0);
                if (bytes_read < 0 || bytes_written < 0)
                {
                    LOG_ERROR << "read()/send(): "
                            << std::strerror(errno) << LOG_END;
                    close(socket);
                    return;
                }
            }
            #else
            // sendfile, if it works, is a fast way to send data from a file to
            // a socket without much effort
            do
            {
                bytes_written = sendfile(socket, filefd, &pos, filesize - pos);
                if (bytes_written < 0)
                {
                    LOG_ERROR << "sendfile(): " << std::strerror(errno) << LOG_END;
                    close(socket);
                    return;
                }
            } while (pos != filesize);
            #endif
            close(filefd);
        }
        // Close the connection if we should
        if (*response.header_value("Connection") == "close")
        {
            close(socket);
            return;
        }
        // Otherwise loop back and try again
        else
            continue;
    }
}
