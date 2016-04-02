#ifndef REQUESTPROCESSOR_H
#define REQUESTPROCESSOR_H
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include <string>
#include <memory>


class RequestProcessor
{
public:
    RequestProcessor(int sockfd);
    RequestProcessor(RequestProcessor&&) = default;
    ~RequestProcessor();
    void operator()();
private:
    bool load_request();
    void build_response();
    void build_404();
    void send_response();

    int sockfd_;
    std::string buf_;
    std::unique_ptr<HTTPResponse> response_;
    std::unique_ptr<HTTPRequest> request_;
};

#endif
