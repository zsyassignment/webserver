#include "HttpServer.h"

#include <functional>

#include <nlohmann/json.hpp>

#include "Buffer.h"
#include "Logger.h"

HttpServer::HttpServer(EventLoop* loop,
                       const InetAddress& addr,
                       const std::string& name,
                       TaskExecutor& executor,
                       OpenAIClient& aiClient,
                       KamaCache::KLfuCache<std::string, std::string>& cache,
                       const std::string& staticDir)
    : server_(loop, addr, name)
    , staticHandler_(staticDir)
    , executor_(executor)
    , aiClient_(aiClient)
    , cache_(cache) {
    server_.setConnectionCallback(
        std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(
        std::bind(&HttpServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    router_.setNotFoundHandler([this](const HttpRequest& req, HttpResponse& resp) {
        staticHandler_.handle(req.path(), resp);
    });
}

void HttpServer::start() {
    server_.start();
}

void HttpServer::onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        LOG_INFO << "New HTTP connection from " << conn->peerAddress().toIpPort();
        std::lock_guard<std::mutex> lock(contextMutex_);
        contexts_.emplace(conn->name(), HttpContext());
    } else {
        LOG_INFO << "Connection closed " << conn->peerAddress().toIpPort();
        removeContext(conn);
    }
}

void HttpServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time) {
    (void)time;
    auto& ctx = contextFor(conn);
    if (!ctx.parseRequest(buf)) {
        sendError(conn, HttpStatusCode::k400BadRequest, "Bad Request");
        return;
    }

    const size_t kMaxBodySize = 4 * 1024 * 1024; // 4MB safeguard
    if (ctx.request().body().size() > kMaxBodySize) {
        sendError(conn, HttpStatusCode::k413PayloadTooLarge, "Payload Too Large");
        ctx.reset();
        return;
    }

    if (ctx.gotAll()) {
        handleRequest(conn, ctx.request());
        ctx.reset();
    }
}

void HttpServer::handleRequest(const TcpConnectionPtr& conn, HttpRequest& req) {
    bool close = !req.keepAlive();

    if (req.method() == HttpRequest::Method::kPost && req.path() == "/api/chat") {
        handleChatAsync(conn, req, close);
        return;
    }

    if (req.method() == HttpRequest::Method::kGet || req.method() == HttpRequest::Method::kPost) {
        HttpResponse resp(close);
        if (!router_.route(req, resp)) {
            resp.setStatusCode(HttpStatusCode::k404NotFound);
            resp.setStatusMessage("Not Found");
            resp.setContentType("text/plain; charset=utf-8");
            resp.setBody("404 Not Found");
        }
        sendResponse(conn, resp);
    } else {
        sendError(conn, HttpStatusCode::k405MethodNotAllowed, "Method Not Allowed");
    }
}

void HttpServer::handleChatAsync(const TcpConnectionPtr& conn, const HttpRequest& req, bool closeConnection) {
    EventLoop* ioLoop = conn->getLoop();
    std::string body = req.body();
    executor_.submit([this, conn, ioLoop, body, closeConnection]() {
        HttpResponse resp(closeConnection);
        resp.setContentType("application/json");

        try {
            auto jsonReq = nlohmann::json::parse(body);
            if (!jsonReq.contains("message")) {
                resp.setStatusCode(HttpStatusCode::k400BadRequest);
                resp.setStatusMessage("Missing message field");
                resp.setBody("{\"error\":\"message required\"}");
            } else {
                std::string userMessage = jsonReq["message"].get<std::string>();

                std::string cached;
                if (cache_.get(userMessage, cached)) {
                    nlohmann::json out{{"cached", true}, {"answer", cached}};
                    resp.setStatusCode(HttpStatusCode::k200Ok);
                    resp.setStatusMessage("OK");
                    resp.setBody(out.dump());
                } else {
                    auto answer = aiClient_.chatCompletion(userMessage);
                    if (answer.has_value()) {
                        cache_.put(userMessage, *answer);
                        nlohmann::json out{{"cached", false}, {"answer", *answer}};
                        resp.setStatusCode(HttpStatusCode::k200Ok);
                        resp.setStatusMessage("OK");
                        resp.setBody(out.dump());
                    } else {
                        resp.setStatusCode(HttpStatusCode::k500InternalServerError);
                        resp.setStatusMessage("AI backend error");
                        resp.setBody("{\"error\":\"AI service unavailable\"}");
                    }
                }
            }
        } catch (const std::exception& ex) {
            resp.setStatusCode(HttpStatusCode::k400BadRequest);
            resp.setStatusMessage("Invalid JSON");
            resp.setBody(std::string("{\"error\":\"invalid json: ") + ex.what() + "\"}");
        }

        ioLoop->queueInLoop([conn, resp]() mutable {
            Buffer buffer;
            resp.appendToBuffer(&buffer);
            std::string serialized = buffer.retrieveAllAsString();
            conn->send(serialized);
            if (resp.closeConnection()) {
                conn->shutdown();
            }
        });
    });
}

void HttpServer::sendResponse(const TcpConnectionPtr& conn, HttpResponse& resp) {
    Buffer buffer;
    resp.appendToBuffer(&buffer);
    std::string data = buffer.retrieveAllAsString();
    conn->send(data);
    if (resp.closeConnection()) {
        conn->shutdown();
    }
}

void HttpServer::sendError(const TcpConnectionPtr& conn, HttpStatusCode code, const std::string& msg) {
    HttpResponse resp(true);
    resp.setStatusCode(code);
    resp.setStatusMessage(msg);
    resp.setContentType("text/plain; charset=utf-8");
    resp.setBody(msg);
    sendResponse(conn, resp);
}

HttpContext& HttpServer::contextFor(const TcpConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(contextMutex_);
    return contexts_[conn->name()];
}

void HttpServer::removeContext(const TcpConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(contextMutex_);
    contexts_.erase(conn->name());
}
