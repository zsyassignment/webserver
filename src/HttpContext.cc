#include "HttpContext.h"

#include <algorithm>
#include <cstdlib>

#include "Buffer.h"

HttpContext::HttpContext()
    : state_(ParseState::kExpectRequestLine), bodyExpected_(0) {}

void HttpContext::reset() {
    state_ = ParseState::kExpectRequestLine;
    HttpRequest empty;
    request_.swap(empty);
    bodyExpected_ = 0;
}

bool HttpContext::processRequestLine(const char* begin, const char* end) {
    const char* start = begin;
    const char* space = std::find(start, end, ' ');
    if (space == end || !request_.setMethod(start, space)) {
        return false;
    }
    start = space + 1;
    space = std::find(start, end, ' ');
    if (space == end) {
        return false;
    }
    const char* question = std::find(start, space, '?');
    if (question == space) {
        request_.setPath(std::string(start, space));
    } else {
        request_.setPath(std::string(start, question));
        request_.setQuery(std::string(question + 1, space));
    }
    start = space + 1;
    if (end - start == 8 && std::equal(start, end, "HTTP/1.1")) {
        request_.setVersion(HttpRequest::Version::kHttp11);
    } else if (end - start == 8 && std::equal(start, end, "HTTP/1.0")) {
        request_.setVersion(HttpRequest::Version::kHttp10);
    } else {
        return false;
    }
    return true;
}

bool HttpContext::parseRequest(Buffer* buf) {
    bool ok = true;
    while (ok) {
        if (state_ == ParseState::kExpectRequestLine) {
            const char* end = buf->peek() + buf->readableBytes();
            const char* crlf = std::search(buf->peek(), end, "\r\n", "\r\n" + 2);
            if (crlf == end) break;
            ok = processRequestLine(buf->peek(), crlf);
            if (!ok) break;
            buf->retrieve(crlf - buf->peek() + 2);
            state_ = ParseState::kExpectHeaders;
        } else if (state_ == ParseState::kExpectHeaders) {
            const char* end = buf->peek() + buf->readableBytes();
            const char* crlf = std::search(buf->peek(), end, "\r\n", "\r\n" + 2);
            if (crlf == end) break;
            if (crlf == buf->peek()) {
                buf->retrieve(2);
                auto lenStr = request_.getHeader("Content-Length");
                bodyExpected_ = lenStr.empty() ? 0 : static_cast<size_t>(atoi(lenStr.c_str()));
                if (bodyExpected_ > 0) {
                    state_ = ParseState::kExpectBody;
                } else {
                    state_ = ParseState::kGotAll;
                }
                continue;
            }
            const char* colon = std::find(buf->peek(), crlf, ':');
            if (colon != crlf) {
                std::string field(buf->peek(), colon);
                std::string value(colon + 1, crlf);
                request_.addHeader(field, value);
            }
            buf->retrieve(crlf - buf->peek() + 2);
        } else if (state_ == ParseState::kExpectBody) {
            if (buf->readableBytes() < bodyExpected_) break;
            request_.setBody(std::string(buf->peek(), buf->peek() + bodyExpected_));
            buf->retrieve(bodyExpected_);
            state_ = ParseState::kGotAll;
        } else {
            break;
        }
    }
    return ok;
}
