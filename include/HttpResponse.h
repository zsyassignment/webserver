#pragma once

#include <map>
#include <string>

class Buffer;

enum class HttpStatusCode {
    k200Ok = 200,
    k400BadRequest = 400,
    k404NotFound = 404,
    k405MethodNotAllowed = 405,
    k413PayloadTooLarge = 413,
    k500InternalServerError = 500
};

class HttpResponse {
public:
    explicit HttpResponse(bool close = false);

    void setStatusCode(HttpStatusCode code) { statusCode_ = code; }
    void setStatusMessage(const std::string& message) { statusMessage_ = message; }
    void setCloseConnection(bool on) { closeConnection_ = on; }
    bool closeConnection() const { return closeConnection_; }

    void setContentType(const std::string& type) { addHeader("Content-Type", type); }
    void addHeader(const std::string& key, const std::string& value);

    void setBody(const std::string& body) { body_ = body; }
    const std::string& body() const { return body_; }

    void appendToBuffer(Buffer* output) const;

private:
    static const char* reasonPhrase(HttpStatusCode code);

    std::map<std::string, std::string> headers_;
    HttpStatusCode statusCode_;
    std::string statusMessage_;
    std::string body_;
    bool closeConnection_;
};
