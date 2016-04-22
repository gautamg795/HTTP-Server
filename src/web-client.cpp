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

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cout << "Usage: " << argv[0] << " [URL] ...\n";
		std::exit(1);
	}

	std::vector<URL> urls;
	for (int i = 1; i < argc; i++)
	{
		urls.push_back(parse_url(argv[i]));
	}
	for (auto url : urls)
	{
		download_file(url);
	}
}


HTTPRequest construct_request(const URL& input)
{
	HTTPRequest request;
	request.set_verb("GET");
	request.set_path(input.path_);
	request.set_version("HTTP/1.0");
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
			request_string.size() - pos, 0);

		if (bytes_written < 0)
		{
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

	return true;

}

bool read_response(int sockfd, HTTPResponse& response)
{
	std::string buf;
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

	} while (bytes_read > 0);
	if (bytes_read < 0)
	{
		if (errno == EAGAIN)
		{
			LOG_ERROR << "Connection to server timed out" << LOG_END;
			return false;
		}
		LOG_ERROR << "recv(): " << std::strerror(errno) << LOG_END;
		return false;
	}

	buf.resize(pos);
	response = HTTPResponse(buf);
	return true;
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
    HTTPRequest request = construct_request(input);

    // write to socket
    if (!write_request(sockfd, request))
    {
    	close(sockfd);
    	return;
    }

    // read from socket
    HTTPResponse response;
    if (!read_response(sockfd, response))
    {
    	close(sockfd);
    	return;
    }
    LOG_INFO << response << LOG_END;
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







