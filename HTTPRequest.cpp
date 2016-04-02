#include "HTTPRequest.h"

#include <sstream>    // for operator<<, basic_ostream, basic_istream, basic...
#include <stdexcept>  // for logic_error
#include <utility>    // for move, pair
HTTPRequest::HTTPRequest(const std::string& req)
{
    std::istringstream iss(req);
    std::string line;
retry:
    std::getline(iss, line);
    if (line.empty())
    {
        if (iss)
            goto retry;
        else
            throw std::logic_error("empty line and iss");
    }
    line.pop_back(); // remove \r
    verb_ = line.substr(0, line.find_first_of(' '));
    path_ = line.substr(verb_.size() + 1,
                        line.find_last_of(' ') - verb_.size() - 1);
    version_ = line.substr(line.find_last_of(' ') + 1);
    while (std::getline(iss, line)) 
    {
        line.pop_back(); // remove \r
        if (line.size() == 0)
        {
            // body is next, if it's there
            break;
        }
        std::string header = line.substr(0, line.find_first_of(':'));
        std::string value = line.substr(line.find_first_of(' ') + 1);
        headers_.emplace(std::move(header), std::move(value));
    }
    if (iss)
        body_ = iss.str().substr(iss.tellg()); 
}


const std::string& HTTPRequest::verb() const
{
    return verb_;
}


void HTTPRequest::set_verb(const std::string& verb)
{
    verb_ = verb;
}


const std::string& HTTPRequest::path() const
{
    return path_;
}


void HTTPRequest::set_path(const std::string& path)
{
    path_ = path;
}


const std::string& HTTPRequest::version() const
{
    return version_;
}


void HTTPRequest::set_version(const std::string& version)
{
    version_ = version;
}


const std::string& HTTPRequest::body() const
{
    return body_;
}


void HTTPRequest::set_body(const std::string& body)
{
    body_ = body;
}

const std::string* HTTPRequest::header_value(const std::string& value) const
{
    auto iter = headers_.find(value);
    if (iter == headers_.end())
    {
        return nullptr;
    }
    return &iter->second;
}

void HTTPRequest::set_header(const std::string& header, const std::string& value)
{
    headers_.emplace(header, value);
}

std::string HTTPRequest::to_string() const
{
    std::ostringstream oss;
    oss << *this;
    return oss.str();
}

std::ostream& operator<<(std::ostream& os, const HTTPRequest& req)
{
    os << req.verb() << " "
       << req.path() << " "
       << req.version() << "\r\n";
    for (const auto& it : req.headers_)
    {
        os << it.first << ": " << it.second << "\r\n";
    }
    os << "\r\n";
    os << req.body();
    return os;
}
