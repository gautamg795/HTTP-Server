#include "HTTPResponse.h"

#include <sstream>      // for operator<<, basic_ostream, getline, basic_ist...
#include <string>       // for char_traits, operator==, hash, basic_string
#include <type_traits>  // for move
#include <utility>      // for pair


HTTPResponse::HTTPResponse(const std::string& resp)
{
    std::istringstream iss(resp);
    std::string line;
    std::getline(iss, line);
    line.pop_back(); // remove \r
    version_ = line.substr(0, line.find_first_of(' '));
    status_ = line.substr(version_.size() + 1,
                          line.find_last_of(' ') - version_.size() - 1);
    phrase_ = line.substr(line.find_last_of(' ') + 1);
    while (std::getline(iss, line))
    {
        line.pop_back(); // remove \r
        if (line.size() == 0)
        {
            // body is next
            break;
        }
        std::string header = line.substr(0, line.find_first_of(':'));
        std::string value = line.substr(line.find_first_of(' '));
        headers_.emplace(std::move(header), std::move(value));
    }
    body_ = iss.str().substr(iss.tellg());
}


const std::string& HTTPResponse::version() const
{
    return version_;
}


void HTTPResponse::set_version(const std::string& version)
{
    version_ = version;
}


const std::string& HTTPResponse::status() const
{
    return status_;
}


void HTTPResponse::set_status(const std::string& status)
{
    status_ = status;
}


const std::string& HTTPResponse::phrase() const
{
    return phrase_;
}


void HTTPResponse::set_phrase(const std::string& phrase)
{
    phrase_ = phrase;
}


const std::string& HTTPResponse::body() const
{
    return body_;
}


void HTTPResponse::set_body(const std::string& body)
{
    body_ = body;
}

const std::string* HTTPResponse::header_value(
                   const std::string& value) const
{
    auto iter = headers_.find(value);
    if (iter == headers_.end())
    {
        return nullptr;
    }
    return &iter->second;
}

void HTTPResponse::set_header(const std::string& header,
                              const std::string& value)
{
    headers_[header] = value;
}

std::string HTTPResponse::to_string() const
{
    std::ostringstream oss;
    oss << *this;
    return oss.str();
}

std::ostream& operator<<(std::ostream& os, const HTTPResponse& res)
{
    os << res.version() << " "
       << res.status()  << " "
       << res.phrase()  << "\r\n";
    for (const auto& it : res.headers_)
    {
        os << it.first << ": " << it.second << "\r\n";
    }
    os << "\r\n";
    os << res.body();
    return os;
}

void HTTPResponse::make_404()
{
    set_version("HTTP/1.1");
    set_status("404");
    set_phrase("Not Found");
    set_body("<h1>Not Found</h1>");
    set_header("Content-Length", std::to_string(body_.size()));
}

void HTTPResponse::make_400()
{
    set_version("HTTP/1.1");
    set_status("400");
    set_phrase("Bad Request");
    set_body("<h1>Bad Request</h1>");
    set_header("Content-Length", std::to_string(body_.size()));
}

void HTTPResponse::make_501()
{
    set_version("HTTP/1.1");
    set_status("501");
    set_phrase("Not Implemented");
    set_body("<h1>Not Implemented</h1>");
    set_header("Content-Length", std::to_string(body_.size()));

}
