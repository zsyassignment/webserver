#include "HttpServer.h"

#include <functional>
#include <vector>

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
        //placeholders::_1是占位符，表示这个位置的参数会在调用时传入。std::bind会将TcpServer::onConnection成员函数与当前对象this绑定起来，并且指定当onConnection被调用时，传入的参数会被正确地转发到占位符的位置。
        std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(
        std::bind(&HttpServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    
    //如果没有路由规则匹配，默认使用静态文件处理器
    router_.setNotFoundHandler([this](const HttpRequest& req, HttpResponse& resp) {
        staticHandler_.handle(req.path(), resp);
    });
}

void HttpServer::start() {
    server_.start();
}

void HttpServer::onConnection(const TcpConnectionPtr& conn) 
{
    if (conn->connected()) 
    {
        LOG_INFO << "New HTTP connection from " << conn->peerAddress().toIpPort();
        std::lock_guard<std::mutex> lock(contextMutex_);
        //插入一个新的 HttpContext 对象到 contexts_ 哈希表中，每个请求有一个对应的 HttpContext 来维护解析状态和请求数据
        contexts_.emplace(conn->name(), HttpContext());
    } 
    else 
    {
        LOG_INFO << "Connection closed " << conn->peerAddress().toIpPort();
        removeContext(conn);
    }
}

void HttpServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time) 
{
    (void)time;
    auto& ctx = contextFor(conn);//返回对应连接的httpcontext解析器
    if (!ctx.parseRequest(buf)) 
    {
        sendError(conn, HttpStatusCode::k400BadRequest, "Bad Request");
        return;
    }

    //现在假设只能处理4MB的请求体，超过就拒绝，防止恶意攻击占满内存资源
    const size_t kMaxBodySize = 4 * 1024 * 1024; // 4MB safeguard
    if (ctx.request().body().size() > kMaxBodySize) 
    {
        sendError(conn, HttpStatusCode::k413PayloadTooLarge, "Payload Too Large");
        ctx.reset();
        return;
    }

    if (ctx.gotAll()) {
        handleRequest(conn, ctx.request());
        ctx.reset();
    }
}

void HttpServer::handleRequest(const TcpConnectionPtr& conn, HttpRequest& req) 
{
    bool close = !req.keepAlive();

    // 特例：如果是 POST /api/chat 就走专门的异步处理流程，其他请求走常规路由
    if (req.method() == HttpRequest::Method::kPost && req.path() == "/api/chat") 
    {
        handleChatAsync(conn, req, close);
        return;
    }

    if (req.method() == HttpRequest::Method::kGet || req.method() == HttpRequest::Method::kPost) 
    {
        HttpResponse resp(close);
        //路由解析
        if (!router_.route(req, resp)) 
        {
            resp.setStatusCode(HttpStatusCode::k404NotFound);
            resp.setStatusMessage("Not Found");
            resp.setContentType("text/plain; charset=utf-8");
            resp.setBody("404 Not Found");
        }
        sendResponse(conn, resp);
    } 
    else 
    {
        sendError(conn, HttpStatusCode::k405MethodNotAllowed, "Method Not Allowed");
    }
}

void HttpServer::handleChatAsync(const TcpConnectionPtr& conn, const HttpRequest& req, bool closeConnection) 
{
    //找到属于的I/O线程
    EventLoop* ioLoop = conn->getLoop();
    std::string body = req.body();
    //需要按值捕获因为后面的lambda会在另一个线程执行，按引用捕获可能会导致访问已销毁的对象
    executor_.submit([this, conn, ioLoop, body, closeConnection]() 
    {
        HttpResponse resp(closeConnection);
        resp.setContentType("application/json");

        //C++ 结构体转 JSON 数组 (序列化)
        //[{"role": "user", "content": "你好"}, {"role": "assistant", "content": "你好！"}]
        auto toJsonHistory = [](const std::vector<OpenAIClient::ChatMessage>& msgs) 
        {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& m : msgs) 
            {
                arr.push_back({{"role", m.role}, {"content", m.content}});
            }
            return arr;
        };

        try 
        {
            LOG_INFO << "Received chat request: " << body;

            //使用nlohmann::json库解析请求体，提取用户消息和历史上下文
            auto jsonReq = nlohmann::json::parse(body);
            if (!jsonReq.contains("message") || !jsonReq["message"].is_string()) 
            {
                resp.setStatusCode(HttpStatusCode::k400BadRequest);
                resp.setStatusMessage("Missing message field");
                resp.setBody("{\"error\":\"message required\"}");
            } 
            else 
            {
                //提取用户消息
                std::string userMessage = jsonReq["message"].get<std::string>();

                std::vector<OpenAIClient::ChatMessage> messages;
                if (jsonReq.contains("history") && jsonReq["history"].is_array()) 
                {
                    for (const auto& item : jsonReq["history"]) 
                    {
                        if (item.contains("role") && item.contains("content") && item["role"].is_string() && item["content"].is_string()) 
                        {
                            messages.push_back({item["role"].get<std::string>(), item["content"].get<std::string>()});
                        }
                    }
                }
                
                //最多包含16条历史消息，保持上下文简洁
                const size_t kMaxMessages = 16; // keep recent context compact
                if (messages.size() > kMaxMessages)
                {
                    messages.erase(messages.begin(), messages.end() - kMaxMessages);
                }

                messages.push_back({"user", userMessage});

                std::string cacheKey = toJsonHistory(messages).dump();
                std::string cached;
                if (cache_.get(cacheKey, cached)) 
                {
                    auto history = messages;
                    history.push_back({"assistant", cached});
                    nlohmann::json out{{"cached", true}, {"answer", cached}, {"history", toJsonHistory(history)}};
                    resp.setStatusCode(HttpStatusCode::k200Ok);
                    resp.setStatusMessage("OK");
                    resp.setBody(out.dump());
                } 
                else 
                {
                    auto answer = aiClient_.chatCompletion(messages);
                    if (answer.has_value()) 
                    {
                        cache_.put(cacheKey, *answer);
                        auto history = messages;
                        history.push_back({"assistant", *answer});
                        nlohmann::json out{{"cached", false}, {"answer", *answer}, {"history", toJsonHistory(history)}};
                        resp.setStatusCode(HttpStatusCode::k200Ok);
                        resp.setStatusMessage("OK");
                        resp.setBody(out.dump());
                    } 
                    else 
                    {
                        resp.setStatusCode(HttpStatusCode::k500InternalServerError);
                        resp.setStatusMessage("AI backend error");
                        resp.setBody("{\"error\":\"AI service unavailable\"}");
                    }
                }
            }
        } 
        catch (const std::exception& ex) 
        {
            resp.setStatusCode(HttpStatusCode::k400BadRequest);
            resp.setStatusMessage("Invalid JSON");
            resp.setBody(std::string("{\"error\":\"invalid json: ") + ex.what() + "\"}");
        }

        //最后用ioLoop发送响应，确保在正确的线程中操作连接对象，直接调用conn->send可能会跨线程访问连接对象，导致线程安全问题
        ioLoop->queueInLoop([conn, resp]() mutable 
        {
            Buffer buffer;
            resp.appendToBuffer(&buffer);
            std::string serialized = buffer.retrieveAllAsString();
            conn->send(serialized);
            if (resp.closeConnection()) 
            {
                conn->shutdown();
            }
        });
    });
}

void HttpServer::sendResponse(const TcpConnectionPtr& conn, HttpResponse& resp) 
{
    Buffer buffer;
    resp.appendToBuffer(&buffer);
    std::string data = buffer.retrieveAllAsString();
    conn->send(data);
    if (resp.closeConnection()) 
    {
        conn->shutdown();
    }
}

void HttpServer::sendError(const TcpConnectionPtr& conn, HttpStatusCode code, const std::string& msg) 
{
    HttpResponse resp(true);
    resp.setStatusCode(code);
    resp.setStatusMessage(msg);
    resp.setContentType("text/plain; charset=utf-8");
    resp.setBody(msg);
    sendResponse(conn, resp);
}

HttpContext& HttpServer::contextFor(const TcpConnectionPtr& conn) 
{
    std::lock_guard<std::mutex> lock(contextMutex_);
    return contexts_[conn->name()];
}

void HttpServer::removeContext(const TcpConnectionPtr& conn) 
{
    std::lock_guard<std::mutex> lock(contextMutex_);
    contexts_.erase(conn->name());
}
