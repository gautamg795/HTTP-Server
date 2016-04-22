#ifndef HTTPSERVER_H
#define HTTPSERVER_H
#include <string>  // for string

class HTTPRequest;
class HTTPResponse;

class HTTPServer
{
public:
    HTTPServer(const std::string& hostname,
               const std::string& port,
               const std::string& directory);
    HTTPServer(const HTTPServer&) = delete; // prevent copy
    HTTPServer& operator=(const HTTPServer&) = delete; // prevent assignment
    ~HTTPServer();

    void install_signal_handler() const;
    void run();
    void run_async();

private:
    static void process_request(int socket);
    static bool set_conn_type(const HTTPRequest& req, HTTPResponse& resp);
    static int  timeout;
    std::string hostname_;
    std::string port_;
    std::string directory_;
    int         sockfd_;
};

#endif
