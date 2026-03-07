#pragma once

#include <string>

#include "HttpRequest.h"

class Buffer;
class Timestamp;

class HttpContext {
public:
    enum class ParseState { kExpectRequestLine, kExpectHeaders, kExpectBody, kGotAll };

    HttpContext();

    bool parseRequest(Buffer* buf);
    bool gotAll() const { return state_ == ParseState::kGotAll; }
    void reset();

    const HttpRequest& request() const { return request_; }
    HttpRequest& request() { return request_; }

private:
    bool processRequestLine(const char* begin, const char* end);

    ParseState state_;
    HttpRequest request_;
    size_t bodyExpected_; // Content-Length
};
