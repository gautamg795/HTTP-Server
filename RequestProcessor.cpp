#include "RequestProcessor.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "logging.h"
#include <unistd.h>
#include <sys/socket.h>
#include <cstring>
#include <fstream>
#include <sstream>

RequestProcessor::RequestProcessor(int sockfd)
    : sockfd_(sockfd)
{
    LOG_INFO << "RequestProcessor constructing on fd " << sockfd << LOG_END;
    buf_.resize(256); 
}

RequestProcessor::~RequestProcessor()
{
    LOG_INFO << "RequestProcessor destructing..." << LOG_END;
    close(sockfd_);
}

void RequestProcessor::operator()()
{
    if (!load_request())
        return;
    build_response();
    send_response();
}

bool RequestProcessor::load_request()
{
    size_t pos = 0;
    ssize_t bytes_read = 0;
    do {
        bytes_read = recv(sockfd_, &buf_[pos], buf_.size() - pos, 0);
        pos += bytes_read;
        if (buf_.size() - pos < 8)
        {
            buf_.resize(buf_.size() * 2);
        }
        if (bytes_read > 0 && pos > 2 && buf_.substr(pos - 2, 2) == "\r\n")
            break;
    } while (bytes_read > 0);
    if (bytes_read < 0)
    {
        LOG_ERROR << "recv(): " << std::strerror(errno) << LOG_END;
        return false;
    }
    buf_.resize(pos);
    request_.reset(new HTTPRequest(buf_));
    LOG_INFO << "Request recieved:\n"
             << *request_ << LOG_END;
    return true;
}

void RequestProcessor::build_response()
{
   response_.reset(new HTTPResponse);  
   response_->set_version("HTTP/1.0");
   std::ifstream ifs("." + request_->path());
   if (!ifs)
   {
       build_404();
       return;
   }
   std::stringstream ss;
   ss << ifs.rdbuf();
   response_->set_body(ss.str());
   response_->set_status("200");
   response_->set_phrase("OK");
}

void RequestProcessor::build_404()
{
    response_->set_status("404");
    response_->set_phrase("Not Found");
    return;
}

void RequestProcessor::send_response()
{
    std::string response_text = response_->to_string(); 
    size_t pos = 0;
    int bytes_written = 0;
    do
    {
        bytes_written = send(sockfd_, &response_text[pos], response_text.size() - pos, 0);
        pos += bytes_written;
    } while (bytes_written > 0);
}
