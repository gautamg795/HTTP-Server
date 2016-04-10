#include "HTTPServer.h"
#include "HTTPRequest.h"   // for HTTPRequest, operator<<
#include "HTTPResponse.h"  // for HTTPResponse
#include "logging.h"       // for LOG_END, LOG_ERROR, LOG_INFO

#ifndef __APPLE__
#include <sys/sendfile.h>  // for sendfile
#endif

#include <algorithm>       // for transform
#include <arpa/inet.h>     // for inet_ntoa
#include <cctype>          // for tolower
#include <cerrno>          // for errno, EINTR
#include <csignal>         // for sigaction, SIGINT, SIGTERM, etc
#include <cstdlib>         // for exit
#include <cstring>         // for strerror, memset
#include <exception>       // for exception
#include <fcntl.h>         // for open, O_RDONLY
#include <functional>      // for _Bind, bind
#include <iostream>        // for operator<<, basic_ostream, ostream, cout
#include <netdb.h>         // for addrinfo, freeaddrinfo, gai_strerror, geta...
#include <netinet/in.h>    // for IPPROTO_TCP, sockaddr_in
#include <string>          // for char_traits, string, operator<<, operator==
#include <sys/select.h>    // for select
#include <sys/socket.h>    // for send, accept, bind, listen, recv, setsockopt
#include <sys/stat.h>      // for fstat, stat
#include <sys/time.h>      // for timeval
#include <regex>
#include <thread>          // for thread
#include <unistd.h>        // for close, off_t, read, ssize_t
#include <wordexp.h>       // for wordexp

static bool keep_running = true;
int HTTPServer::timeout = 5;

HTTPServer::HTTPServer(const std::string& hostname,
                       const std::string& port,
                       const std::string& directory) :
    hostname_(hostname), port_(port), directory_(directory), sockfd_(-1)
{
    #if !defined(__GNUC__) || __GNUC__ >= 5
    directory_ = std::regex_replace(directory_, std::regex(R"(([^\\]) )"), R"($1\ )");
    #endif
    ::setenv("IFS", "\n", 1);
    wordexp_t expansion;
    if (wordexp(directory_.c_str(), &expansion, 0) != 0 || expansion.we_wordc < 1)
    {
        LOG_ERROR << "Error expanding directory path given" << LOG_END;
        std::exit(1);
    }
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
        try
        {
            std::thread t{std::bind(process_request, temp_fd)};
            t.detach();
        } catch (const std::exception& ex)
        {
            close(temp_fd);
            LOG_ERROR << "std::thread(): " << ex.what() << LOG_END;
        }
    }
}

void HTTPServer::process_request(int socket)
{
    std::string remainder;
    while(true)
    {
        bool have_complete_request = remainder.find("\r\n\r\n") != std::string::npos;
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(socket, &fdset);
        struct timeval timeout;
        int ret;
        if (!have_complete_request)
        {
            timeout.tv_sec = HTTPServer::timeout;
            timeout.tv_usec = 0;
            ret = select(socket+1, &fdset, nullptr, nullptr, &timeout);
        }
        else
        {
            ret = 0;
        }
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
        std::string buf = remainder;
        off_t pos = buf.size();
        ssize_t bytes_read = 0;
        if (!have_complete_request) // get more data if we don't have a full request
        {
            buf.resize(256 + buf.size());
            do {
                bytes_read = recv(socket, &buf[pos], buf.size() - pos, 0);
                pos += bytes_read;
                if (buf.size() - pos < 8)
                {
                    buf.resize(buf.size() * 2);
                }
                if (buf.find("\r\n\r\n") != std::string::npos)
                    break;
            } while (bytes_read > 0);
            if (bytes_read < 0)
            {
                LOG_ERROR << "recv(): " << std::strerror(errno) << LOG_END;
                return;
            }
            else if (bytes_read == 0)
            {
                LOG_INFO << "Connection closed by peer" << LOG_END;
                close(socket);
                return;
            }
            buf.resize(pos);
        }
        HTTPRequest request;
        try
        {
            HTTPRequest _request(buf, &remainder);
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
            // sendfile broken on Mac, send the file manually
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
        {
            close(socket);
            return;
        }
        else
            continue;
    }
}
