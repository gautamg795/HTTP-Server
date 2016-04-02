#include "HTTPServer.h"
#include "HTTPRequest.h"   // for HTTPRequest, operator<<
#include "HTTPResponse.h"  // for HTTPResponse
#include "logging.h"       // for LOG_END, LOG_ERROR, LOG_INFO

#ifndef __APPLE__
#include <sys/sendfile.h>  // for sendfile
#endif

#include <arpa/inet.h>     // for inet_ntoa
#include <csignal>         // for sigaction
#include <cstdlib>         // for exit
#include <cstring>         // for strerror, memset
#include <functional>      // for _Bind, bind
#include <iostream>        // for operator<<, basic_ostream, ostream, cout
#include <netdb.h>         // for addrinfo, freeaddrinfo, gai_strerror, geta...
#include <netinet/in.h>    // for IPPROTO_TCP, sockaddr_in
#include <string>          // for char_traits, string, operator<<, operator==
#include <sys/errno.h>     // for errno, EINTR
#include <sys/fcntl.h>     // for open, O_RDONLY
#include <sys/select.h>    // for select
#include <sys/signal.h>    // for sigaction, SIGINT, SIGTERM, sa_handler
#include <sys/socket.h>    // for send, accept, bind, listen, recv, setsockopt
#include <sys/stat.h>      // for fstat, stat
#include <thread>          // for thread
#include <unistd.h>        // for close, off_t, read, ssize_t

static bool keep_running = true;
int HTTPServer::timeout = 5;
HTTPServer::HTTPServer(const std::string& hostname,
                       const std::string& port,
                       const std::string& directory) :
    hostname_(hostname), port_(port), directory_(directory), sockfd_(-1)
{
    //if(chdir(directory_.c_str()) < 0)
    //{
        //LOG_ERROR << "chdir(): " << std::strerror(errno) << LOG_END;
        //std::exit(1);
    //}
    LOG_INFO << "Initializing HTTP server at "
             << hostname << ':' << port
             << " serving files from " << directory << LOG_END;

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;
    hints.ai_flags = AI_NUMERICSERV;

    struct addrinfo* res;
    int ret = getaddrinfo(hostname_.c_str(), port_.c_str(), &hints, &res);
    if (ret != 0)
    {
        std::cout << gai_strerror(ret) << std::endl;
        std::exit(ret);
    }
    auto ptr = res;
    for (; ptr != nullptr; ptr = ptr->ai_next)
    {
        sockfd_ = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sockfd_ == -1)
        {
            LOG_ERROR << "socket(): " << std::strerror(errno) << LOG_END;
            continue;
        }

        const int one = 1;
        if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)) == -1)
        {
            LOG_ERROR << "setsockopt(): " << std::strerror(errno) << LOG_END;
            std::exit(1);
        }

        int ret = bind(sockfd_, ptr->ai_addr, ptr->ai_addrlen);
        if (ret == -1)
        {
            LOG_ERROR << "bind(): " << std::strerror(errno) << LOG_END;
            close(sockfd_);
            continue;
        }
        break;
    }
    if (ptr == nullptr)
    {
        LOG_ERROR << "Failed to bind socket to " << hostname_
                  << ':' << port_ << LOG_END;
        exit(1);
    }
    LOG_INFO << "Hostname resolved to "
              << inet_ntoa(((sockaddr_in*)ptr->ai_addr)->sin_addr)
              << LOG_END;
    freeaddrinfo(res);
}

HTTPServer::~HTTPServer()
{
    LOG_INFO << "Shutting down HTTP server..." << LOG_END;
    close(sockfd_);
}

void HTTPServer::install_signal_handler() const
{
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = [](int) { keep_running = false; };
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

void HTTPServer::run()
{
    if(listen(sockfd_, 64) != 0)
    {
        LOG_ERROR << "listen(): " << std::strerror(errno) << LOG_END;
        return;
    }
    LOG_INFO << "Listening on port " << port_ << LOG_END;
    while (keep_running)
    {
        int temp_fd = accept(sockfd_, nullptr, nullptr);
        if (temp_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            LOG_ERROR << "accept(): " << strerror(errno) << LOG_END;
            return;
        }
        try {
        std::thread t{std::bind(process_request, temp_fd)};
        t.detach();
        } catch (...)
        { close(temp_fd);  throw; }
    }
}

void HTTPServer::process_request(int socket)
{
start:
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(socket, &fdset);
    struct timeval timeout;
    timeout.tv_sec = HTTPServer::timeout;
    timeout.tv_usec = 0;
    int ret = select(socket+1, &fdset, nullptr, nullptr, &timeout);
    if (ret == 0)
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
    std::string buf;
    buf.resize(256);
    off_t pos = 0;
    ssize_t bytes_read = 0;
    do {
        bytes_read = recv(socket, &buf[pos], buf.size() - pos, 0);
        pos += bytes_read;
        if (buf.size() - pos < 8)
        {
            buf.resize(buf.size() * 2);
        }
        if (bytes_read > 0 && pos > 4 && buf.substr(pos - 4, 4) == "\r\n\r\n")
            break;
    } while (bytes_read > 0);
    if (bytes_read < 0)
    {
        LOG_ERROR << "recv(): " << std::strerror(errno) << LOG_END;
        return;
    }
    else if (bytes_read == 0)
    {
        close(socket);
        return;
    }
    buf.resize(pos);
    HTTPRequest request;
    try
    {
        HTTPRequest _request(buf);
        request = std::move(_request);
    } catch (const std::exception& ex)
    {
        LOG_ERROR << "HTTPRequest construction failed: " << ex.what() << LOG_END;
        close(socket);
        return;
    }
    LOG_INFO << "Request recieved:\n"
             << request << LOG_END;
    HTTPResponse response;
    response.set_version("HTTP/1.1");
    LOG_INFO << "Attempting to open file at " << '.'
             << request.path() << LOG_END;
    bool file_ok = true;
    int filefd = open(("." + request.path()).c_str(), O_RDONLY);
    if (filefd < 0)
    {
        LOG_ERROR << "open(): " << std::strerror(errno) << " opening file "
                  << "." << request.path() << LOG_END;
        file_ok = false;
    }
    off_t filesize = 0;
    if (file_ok) {
        struct stat filestat;
        if (fstat(filefd, &filestat) != -1)
        {
            if (!S_ISREG(filestat.st_mode))
                file_ok = false;
            filesize = filestat.st_size;
        }
        else
            file_ok = false;
    }
    if (!file_ok)
    {
        LOG_INFO << "Response: HTTP/1.0 404 Not Found" << LOG_END;
        response.set_status("404");
        response.set_phrase("Not Found");
        response.set_header("Connection", "close");
        response.set_body("<h1>404 Not Found</h1>");
    }
    else
    {
        LOG_INFO << "Response: HTTP/1.0 200 OK" << LOG_END;
        response.set_status("200");
        response.set_phrase("OK");
        if (file_ok)
        {
            response.set_header("Content-Length", std::to_string(filesize));
        }
        response.set_header("Connection", "close");
        auto connection = request.header_value("Connection");
        if (connection)
        {
            auto conn_str = *connection;
            std::transform(conn_str.begin(), conn_str.end(), conn_str.begin(),
                                                             ::tolower);
            if (conn_str == "keep-alive")
            {
                response.set_header("Connection", "keep-alive");
                response.set_header(
                        "Keep-Alive",
                        "timeout=" + std::to_string(HTTPServer::timeout));
            }
        }
    }
    std::string response_text = response.to_string();
    pos = 0;
    ssize_t bytes_written = 0;
    do
    {
        bytes_written = send(socket, &response_text[pos],
                             response_text.size() - pos, 0);
        pos += bytes_written;
    } while (bytes_written > 0);
    if (file_ok)
    {
        bytes_written = 0;
        pos = 0;
        #ifdef __APPLE__
        // Cheat on Mac; slower
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
    if (*response.header_value("Connection") == "close")
        close(socket);
    else
        goto start;
}
