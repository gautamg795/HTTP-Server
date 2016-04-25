#include "web-client.h"
#include "logging.h"
#include <string>
#include <regex>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <fstream>
#include <unordered_map>
#ifdef __APPLE__
#define MSG_NOSIGNAL SO_NOSIGPIPE
#endif

const int OK = 0;
const int SOCKET_ERROR = 1;
const int CONNECTION_CLOSED = 2;


int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " [URL] ...\n";
        std::exit(1);
    }

    // string/key is hostname + port number
    std::unordered_map<std::string, std::vector<URL>> urls;
    for (int i = 1; i < argc; i++)
    {
        URL current_url = parse_url(argv[i]);
        std::string key = current_url.hostname_ + current_url.port_;
        auto it = urls.find(key);
        if (it == urls.end())
        {
            urls.insert({key, std::vector<URL>()});
        }
        urls[key].push_back(current_url);
    }

    // make sure to call download_files
    for (const auto& url_pair : urls)
    {
        download_files(url_pair.second);
    }


    // std::vector<URL> urls;
    // for (int i = 1; i < argc; i++)
    // {
    //  urls.push_back(parse_url(argv[i]));
    // }
    // for (auto url : urls)
    // {
    //  download_file(url);
    // }
}


HTTPRequest construct_request(const URL& input, bool persistent)
{
    HTTPRequest request;
    request.set_verb("GET");
    request.set_path(input.path_);
    request.set_version("HTTP/1.1");
    if (persistent)
    {
        request.set_header("Connection", "keep-alive");
    }
    else
    {
        request.set_header("Connection", "close");
    }
    request.set_header("Host", input.hostname_);
    return request;
}

bool write_request(int sockfd, const HTTPRequest& request)
{
    std::string request_string = request.to_string();
    off_t pos = 0;
    ssize_t bytes_written = 0;
    do
    {
        bytes_written = send(sockfd, &request_string[pos],
            request_string.size() - pos, MSG_NOSIGNAL);

        if (bytes_written < 0)
        {
            if (errno == EPIPE)
            {
                return false;
            }
            if (errno == EAGAIN)
            {
                LOG_ERROR << "Connection to server timed out" << LOG_END;
                return false;
            }
            LOG_ERROR << "send(): " << std::strerror(errno) << LOG_END;
            return false;
        }

        pos += bytes_written;
    } while (bytes_written > 0);
    if (pos == 0)
    {
        return false;
    }
    return true;

}

int read_response(int sockfd, HTTPResponse& response)
{
    std::string buf;
    std::string remainder;
    bool has_length = true;
    ssize_t bytes_read = 0;
    buf.resize(256);
    off_t pos = 0;
    do {
        bytes_read = recv(sockfd, &buf[pos], buf.length() - pos, 0);
        pos += bytes_read;
        if (buf.size() - pos < 8)
        {
            buf.resize(buf.size() * 2);
        }
        if (has_length && buf.find("\r\n\r\n") != std::string::npos)
        {
            buf.resize(pos);
            response = HTTPResponse(buf, &remainder);
            auto length_str = response.header_value("Content-Length");
            if (!length_str)
            {
                LOG_INFO << "No content length provided" << LOG_END;
                has_length = false;
                continue;
            }
            ssize_t content_length = std::stol(*length_str);
            pos = remainder.size();
            remainder.resize(content_length);
            do {
                bytes_read = recv(sockfd, &remainder[pos], remainder.length() - pos, 0);
                pos += bytes_read;
            } while (pos != content_length && bytes_read > 0);
            response.set_body(std::move(remainder));
            break;
        }
    } while (bytes_read > 0);
    if (bytes_read == 0)
    {
        return CONNECTION_CLOSED;
    }
    if (bytes_read < 0)
    {
        if (errno == EAGAIN)
        {
            LOG_ERROR << "Connection to server timed out" << LOG_END;
        }
        else
        {
            LOG_ERROR << "recv(): " << std::strerror(errno) << LOG_END;
        }
        return SOCKET_ERROR;
    }

    if (!has_length)
    {
        response = HTTPResponse(buf);
    }
    return OK;
}


URL parse_url(const char* input)
{
    const std::regex url_regex(R"(^(?:https?:\/\/)?([^\/:]+)(?::(\d+))?(.*)$)");
    std::string url_string(input);
    std::smatch regex_match;
    std::regex_search(url_string, regex_match, url_regex);
    if (regex_match.size() != 4)
    {
        throw std::runtime_error("URL could not be parsed: " + url_string);
    }
    URL parsed;
    parsed.hostname_ = regex_match[1];
    parsed.port_ = (regex_match[2] == "" ? std::string("80") : regex_match[2]);
    parsed.path_ = (regex_match[3] == "" ? std::string("/") : regex_match[3]);

    return parsed;
}

void download_file(const URL& input)
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    int sockfd;
    hints.ai_protocol = IPPROTO_TCP; // TCP protocol
    hints.ai_socktype = SOCK_STREAM; // Streaming socket
    hints.ai_family = AF_INET; // IPv4
    hints.ai_flags = AI_NUMERICSERV; // Port is a number

    struct addrinfo* res;
    int ret = getaddrinfo(input.hostname_.c_str(), input.port_.c_str(), &hints, &res);
    if (ret != 0)
    {
        LOG_ERROR << gai_strerror(ret) << LOG_END;
        return;
    }

    auto ptr = res;

    // loop through results from getaddrinfo, find the first socket
    // that we can connect to
    for (; ptr != nullptr; ptr = ptr->ai_next)
    {
        sockfd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

        if (sockfd == -1)
        {
            LOG_ERROR << "socket(): " << std::strerror(errno) << LOG_END;
            continue;
        }

        if (connect(sockfd, ptr->ai_addr, ptr->ai_addrlen) == -1)
        {
            close(sockfd);
            LOG_ERROR << "connect(): " << std::strerror(errno) << LOG_END;
        }

        break;
    }

    if (ptr == nullptr)
    {
        LOG_ERROR << "Client failed to connect to host" << LOG_END;
    }
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        LOG_ERROR << "Could not set receive timeout: " << std::strerror(errno) << LOG_END;
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        LOG_ERROR << "Could not set send timeout: " << std::strerror(errno) << LOG_END;
    }

    // construct http request
    HTTPRequest request = construct_request(input, false);

    // write to socket
    if (!write_request(sockfd, request))
    {
        close(sockfd);
        return;
    }

    // read from socket
    HTTPResponse response;
    if (read_response(sockfd, response) != OK)
    {
        close(sockfd);
        return;
    }
    if (response.status() == "200")
    {
        // write body of response to file
        std::string filename = input.path_.substr(input.path_.find_last_of('/') + 1);
        if (filename.empty())
        {
            filename = "index.html";
        }
        std::fstream fs(filename, std::fstream::out | std::fstream::trunc );
        fs << response.body();
        fs.close();
    }
    else
    {
        LOG_ERROR << "Could not get " << input.hostname_ << ":" << input.port_
                  << input.path_ << LOG_END;
        LOG_ERROR << "Server returned " << response.status() << " ("
                  << response.phrase() << ")" << LOG_END;
    }
}

void download_files(const std::vector<URL>& urls)
{
    const URL& first_url = urls.front();

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    int sockfd;
    hints.ai_protocol = IPPROTO_TCP; // TCP protocol
    hints.ai_socktype = SOCK_STREAM; // Streaming socket
    hints.ai_family = AF_INET; // IPv4
    hints.ai_flags = AI_NUMERICSERV; // Port is a number

    struct addrinfo* res;
    int ret = getaddrinfo(first_url.hostname_.c_str(), first_url.port_.c_str(), &hints, &res);
    if (ret != 0)
    {
        LOG_ERROR << gai_strerror(ret) << LOG_END;
        return;
    }

    auto ptr = res;

    // loop through results from getaddrinfo, find the first socket
    // that we can connect to
    for (; ptr != nullptr; ptr = ptr->ai_next)
    {
        sockfd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

        if (sockfd == -1)
        {
            LOG_ERROR << "socket(): " << std::strerror(errno) << LOG_END;
            continue;
        }

        if (connect(sockfd, ptr->ai_addr, ptr->ai_addrlen) == -1)
        {
            close(sockfd);
            LOG_ERROR << "connect(): " << std::strerror(errno) << LOG_END;
        }

        break;
    }

    if (ptr == nullptr)
    {
        LOG_ERROR << "Client failed to connect to host" << LOG_END;
    }

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        LOG_ERROR << "Could not set receive timeout: " << std::strerror(errno) << LOG_END;
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        LOG_ERROR << "Could not set send timeout: " << std::strerror(errno) << LOG_END;
    }

    for (const URL& current_url : urls)
    {
        // construct http request
        HTTPRequest request = construct_request(current_url, true);

        // write to socket
        if (!write_request(sockfd, request))
        {
            LOG_INFO << "Server closed connection, reverting to non-persistent" << LOG_END;
            for (const URL& single_url : urls)
                download_file(single_url);
            return;
        }

        // read from socket
        HTTPResponse response;
        int ret = read_response(sockfd, response);
        if (ret == SOCKET_ERROR)
        {
            close(sockfd);
            return;
        }
        else if (ret == CONNECTION_CLOSED)
        {
            LOG_INFO << "Server closed connection, reverting to non-persistent" << LOG_END;
            for (const URL& single_url : urls)
                download_file(single_url);
            return;
        }
        if (response.status() == "200")
        {
            // write body of response to file
            std::string filename = current_url.path_.substr(current_url.path_.find_last_of('/') + 1);
            if (filename.empty())
            {
                filename = "index.html";
            }
            std::fstream fs(filename, std::fstream::out | std::fstream::trunc );
            fs << response.body();
        }
        else
        {
            LOG_ERROR << "Could not get " << current_url.hostname_ << ":" << current_url.port_
                      << current_url.path_ << LOG_END;
            LOG_ERROR << "Server returned " << response.status() << " ("
                      << response.phrase() << ")" << LOG_END;
        }
    }

    close(sockfd);
}







