#include "HTTPServer.h"  // for HTTPServer

#include <cstdlib>       // for exit
#include <iostream>      // for operator<<, basic_ostream, char_traits, cout
#include <string>        // for string


int main(int argc, char** argv)
{
    if (argc > 4)
    {
        std::cout << "Usage: " << argv[0] << " [hostname] [port] [file-dir]\n";
        std::exit(1);
    }
    std::string hostname(argc >= 2 ? argv[1] : "localhost");
    std::string port(argc >= 3 ? argv[2] : "4000");
    std::string filedir(argc == 4 ? argv[3] : ".");
    HTTPServer server(hostname, port, filedir);
    server.install_signal_handler();
    server.run_async();
}
