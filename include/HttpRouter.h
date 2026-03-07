#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "HttpRequest.h"
#include "HttpResponse.h"

class HttpRouter {
public:
    using Handler = std::function<void(const HttpRequest&, HttpResponse&)>;

    void addRoute(HttpRequest::Method method, const std::string& path, Handler handler);
    bool route(const HttpRequest& req, HttpResponse& resp) const;
    void setNotFoundHandler(Handler handler) { notFoundHandler_ = std::move(handler); }

private:
    struct RouteKey {
        HttpRequest::Method method;
        std::string path;
        bool operator==(const RouteKey& other) const {
            return method == other.method && path == other.path;
        }
    };

    struct RouteKeyHash {
        size_t operator()(const RouteKey& key) const {
            return std::hash<int>()(static_cast<int>(key.method)) ^ std::hash<std::string>()(key.path);
        }
    };

    std::unordered_map<RouteKey, Handler, RouteKeyHash> routes_;
    Handler notFoundHandler_;
};
