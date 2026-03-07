#include "StaticFileHandler.h"

#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include "Logger.h"

StaticFileHandler::StaticFileHandler(const std::string& rootDir)
    : rootDir_(rootDir) {}

bool StaticFileHandler::handle(const std::string& urlPath, HttpResponse& resp) const {
    std::string fullPath = resolvePath(urlPath);
    std::string content;
    if (!readFile(fullPath, content)) {
        resp.setStatusCode(HttpStatusCode::k404NotFound);
        resp.setStatusMessage("Not Found");
        resp.setContentType("text/plain; charset=utf-8");
        resp.setBody("404 Not Found");
        return false;
    }
    resp.setStatusCode(HttpStatusCode::k200Ok);
    resp.setStatusMessage("OK");
    resp.setContentType(detectMime(fullPath));
    resp.setBody(content);
    return true;
}

std::string StaticFileHandler::resolvePath(const std::string& urlPath) const {
    std::string path = urlPath;
    if (path == "/") path = "/index.html";
    if (path.find("..") != std::string::npos) {
        return rootDir_ + "/index.html";
    }
    if (!path.empty() && path.front() == '/') path.erase(path.begin());
    return rootDir_ + "/" + path;
}

bool StaticFileHandler::readFile(const std::string& fullPath, std::string& out) const {
    struct stat st;
    if (::stat(fullPath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        LOG_WARN << "Static file not found: " << fullPath;
        return false;
    }
    std::ifstream file(fullPath, std::ios::in | std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream oss;
    oss << file.rdbuf();
    out = oss.str();
    return true;
}

std::string StaticFileHandler::detectMime(const std::string& path) const {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(pos + 1);
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js") return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    return "application/octet-stream";
}
