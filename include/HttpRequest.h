#pragma once

#include <map>
#include <string>

class HttpRequest {
public:
    enum class Method { kInvalid, kGet, kPost, kPut, kDelete };
    enum class Version { kUnknown, kHttp10, kHttp11 };

    HttpRequest();

    bool setMethod(const char* start, const char* end);
    Method method() const { return method_; }
    const char* methodString() const;

    void setVersion(Version v) { version_ = v; }
    Version version() const { return version_; }

    void setPath(const std::string& path) { path_ = path; }
    const std::string& path() const { return path_; }

    void setQuery(const std::string& query) { query_ = query; }
    const std::string& query() const { return query_; }

    void addHeader(const std::string& key, const std::string& value);
    std::string getHeader(const std::string& field) const;
    const std::map<std::string, std::string>& headers() const { return headers_; }

    void setBody(const std::string& body) { body_ = body; }
    const std::string& body() const { return body_; }

    void swap(HttpRequest& other);
    bool keepAlive() const;

private:
    Method method_;
    Version version_;
    std::string path_;
    std::string query_;
    std::map<std::string, std::string> headers_;
    std::string body_;
};
