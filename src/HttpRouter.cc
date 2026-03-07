#include "HttpRouter.h"

void HttpRouter::addRoute(HttpRequest::Method method, const std::string& path, Handler handler) {
    routes_[RouteKey{method, path}] = std::move(handler);
}

bool HttpRouter::route(const HttpRequest& req, HttpResponse& resp) const {
    RouteKey key{req.method(), req.path()};
    auto it = routes_.find(key);
    if (it != routes_.end()) {
        it->second(req, resp);
        return true;
    }
    if (notFoundHandler_) {
        notFoundHandler_(req, resp);
        return true;
    }
    return false;
}
