#include "HttpRequest.h"

#include <algorithm>
#include <strings.h>

namespace {
inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}
}

HttpRequest::HttpRequest()
    : method_(Method::kInvalid), version_(Version::kUnknown) {}

bool HttpRequest::setMethod(const char* start, const char* end) {
    std::string m(start, end);
    if (m == "GET") method_ = Method::kGet;
    else if (m == "POST") method_ = Method::kPost;
    else if (m == "PUT") method_ = Method::kPut;
    else if (m == "DELETE") method_ = Method::kDelete;
    else method_ = Method::kInvalid;
    return method_ != Method::kInvalid;
}

const char* HttpRequest::methodString() const {
    switch (method_) {
    case Method::kGet: return "GET";
    case Method::kPost: return "POST";
    case Method::kPut: return "PUT";
    case Method::kDelete: return "DELETE";
    default: return "INVALID";
    }
}

void HttpRequest::addHeader(const std::string& key, const std::string& value) {
    headers_[key] = trim(value);
}

std::string HttpRequest::getHeader(const std::string& field) const {
    auto it = headers_.find(field);
    if (it != headers_.end()) {
        return it->second;
    }
    return "";
}

void HttpRequest::swap(HttpRequest& other) {
    std::swap(method_, other.method_);
    std::swap(version_, other.version_);
    path_.swap(other.path_);
    query_.swap(other.query_);
    headers_.swap(other.headers_);
    body_.swap(other.body_);
}

bool HttpRequest::keepAlive() const {
    auto connection = getHeader("Connection");
    if (!connection.empty()) {
        return strcasecmp(connection.c_str(), "close") != 0;
    }
    // HTTP/1.1 默认 keep-alive，HTTP/1.0 默认关闭
    return version_ == Version::kHttp11;
}
