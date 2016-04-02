#include <iostream>
#include <cassert>
#include "HTTPRequest.h"
#include <string>
int main()
{
    std::string r = "GET /b/ss/[rsid]/0?g=apps.sillystring.com%2Fsummary.do&r=http%3A%2F%2Fapps.sillystring.com%"
        "2Fsummary.do&ip=192.168.10.1&gn=summary&v2=14911&c10=Brazil&vid=1286556420966514130&ts=2009-03-05T01%3A00%3A01-05 HTTP/1.0\r\n"
        "Host: [rsid].112.2o7.net\r\n\r\n";
    HTTPRequest req(r);
    std::cout << req << std::endl;
    assert(req.to_string() == r);
}
