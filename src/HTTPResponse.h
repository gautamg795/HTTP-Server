#ifndef HTTPRESPONSE_H
#define HTTPRESPONSE_H

#include <iosfwd>         // for ostream
#include <stdexcept>      // for runtime_error
#include <string>         // for string
#include <unordered_map>  // for unordered_map

class HTTPResponse
{
public:
    HTTPResponse() = default;
    HTTPResponse(const std::string& resp);
    HTTPResponse(const HTTPResponse&) = default;
    
    const std::string& version() const;
    void set_version(const std::string& version);

    const std::string& status() const;
    void set_status(const std::string& status);

    const std::string& phrase() const;
    void set_phrase(const std::string& phrase);

    const std::string& body() const;
    void set_body(const std::string& body);

    const std::string* header_value(const std::string& header) const;
    void set_header(const std::string& header, const std::string& value);

    std::string to_string() const;

    friend std::ostream& operator<<(std::ostream&, const HTTPResponse&);

private:
    std::string version_;
    std::string status_;
    std::string phrase_;
    std::string body_;
    std::unordered_map<std::string, std::string> headers_;
};

#endif
