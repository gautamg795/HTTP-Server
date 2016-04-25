#ifndef WEBCLIENT_H
#define WEBCLIENT_H

#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include <vector>

struct URL
{

public:
	std::string hostname_;
	std::string port_;
	std::string path_;

};

URL parse_url(const char* input);

void download_file(const URL& input);

void download_files(const std::vector<URL>& urls);

HTTPRequest construct_request(const URL& input, bool persistent);

int write_request(int sockfd, const HTTPRequest& request);

int read_response(int sockfd, HTTPResponse& response);
int read_response_persistent(int sockfd, HTTPResponse& response);


#endif
