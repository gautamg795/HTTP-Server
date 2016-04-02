#ifndef HTTPSERVER_H
#define HTTPSERVER_H
#include <string>  // for string

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

private:
    static void process_request(int socket);
    static int  timeout;
    std::string hostname_;
    std::string port_;
    std::string directory_;
    int         sockfd_;
};

#endif