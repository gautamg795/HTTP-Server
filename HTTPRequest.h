#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#include <iosfwd>         // for ostream
#include <string>         // for string
#include <unordered_map>  // for unordered_map

class HTTPRequest
{
public:
    HTTPRequest();
    HTTPRequest(const std::string& req);

    const std::string& verb() const;
    void set_verb(const std::string& verb);

    const std::string& path() const;
    void set_path(const std::string& path);

    const std::string& version() const;
    void set_version(const std::string& version);

    const std::string& body() const;
    void set_body(const std::string& body);

    const std::string* header_value(const std::string& header) const;
    void set_header(const std::string& header, const std::string& value);

    std::string to_string() const;

    friend std::ostream& operator<<(std::ostream&, const HTTPRequest&);

private:
    std::string verb_;
    std::string path_;
    std::string version_;
    std::string body_;
    std::unordered_map<std::string, std::string> headers_;
};

std::ostream& operator<<(std::ostream& os, const HTTPRequest& req);
#endif
