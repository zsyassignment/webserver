#include "openai.hpp"

#include <curl/curl.h>
#include <cstdlib>
#include <cstring>

#include <Logger.h>
#include <nlohmann/json.hpp>

//每次从网卡收到一段数据，就会调用一次这个函数
namespace {
size_t writeToString(void* contents, size_t size, size_t nmemb, void* userp) 
{
    size_t totalSize = size * nmemb;
    std::string* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}
}

OpenAIClient::OpenAIClient(const std::string& apiKey)
    : apiKey_(apiKey)
    , model_("gpt-3.5-turbo")
    , baseUrl_("https://api.openai.com/v1/chat/completions") 
{
    //避免硬编码api key和base url，优先使用环境变量
    const char* envKey = std::getenv("OPENAI_API_KEY");
    if (apiKey_.empty() && envKey) {
        apiKey_ = envKey;
    }

    const char* envBase = std::getenv("OPENAI_BASE_URL");
    if (envBase && std::strlen(envBase) > 0) {
        baseUrl_ = envBase;
    }
}

//http请求构建和发送，返回结果解析
std::optional<std::string> OpenAIClient::chatCompletion(const std::vector<ChatMessage>& messages) const 
{
    //未设置api key
    if (apiKey_.empty()) 
    {
        LOG_ERROR << "OpenAI API key missing. Set OPENAI_API_KEY env variable.";
        return std::nullopt;
    }

    //消息列表不能为空
    if (messages.empty()) 
    {
        LOG_ERROR << "chatCompletion called with empty messages";
        return std::nullopt;
    }

    CURL* curl = curl_easy_init();
    if (!curl) 
    {
        LOG_ERROR << "Failed to init CURL";
        return std::nullopt;
    }

    //接收缓冲
    std::string responseBuffer;

    struct curl_slist* headers = nullptr;
    //身份验证和内容类型
    std::string authHeader = "Authorization: Bearer " + apiKey_;
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (baseUrl_.find("deepseek") != std::string::npos) 
    {
        headers = curl_slist_append(headers, "Accept: application/json");
    }

    //历史信息
    nlohmann::json jsonMessages = nlohmann::json::array();
    for (const auto& msg : messages) 
    {
        jsonMessages.push_back({{"role", msg.role}, {"content", msg.content}});
    }

    nlohmann::json payload = 
    {
        {"model", model_},
        {"messages", jsonMessages}
    };
    //序列化
    std::string payloadStr = payload.dump();
    LOG_INFO << "Sending OpenAI request: " << payload.dump(4);

    //给curl句柄设置参数
    curl_easy_setopt(curl, CURLOPT_URL, baseUrl_.c_str()); //目标url
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); //请求头
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payloadStr.c_str()); //请求体
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payloadStr.size()); //请求体长度  
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString); //回调函数，接收响应数据
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer); //回调函数的参数，传入接收缓冲

    //阻塞地执行请求，直到完成
    CURLcode res = curl_easy_perform(curl);

    //清理资源
    if (res != CURLE_OK) 
    {
        LOG_ERROR << "CURL perform failed: " << curl_easy_strerror(res);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::nullopt;
    }

    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (responseCode >= 400) {
        LOG_ERROR << "OpenAI API responded with status " << responseCode << " body: " << responseBuffer;
        return std::nullopt;
    }

    try {
        //反序列化
        auto json = nlohmann::json::parse(responseBuffer);
        if (!json.contains("choices") || json["choices"].empty()) {
            LOG_ERROR << "Unexpected OpenAI response: " << responseBuffer;
            return std::nullopt;
        }
        auto content = json["choices"][0]["message"]["content"].get<std::string>();
        return content;
    } catch (const std::exception& ex) {
        LOG_ERROR << "Failed to parse OpenAI response: " << ex.what() << " raw: " << responseBuffer;
        return std::nullopt;
    }
}

std::optional<std::string> OpenAIClient::chatCompletion(const std::string& userMessage) const {
    return chatCompletion(std::vector<ChatMessage>{{"user", userMessage}});
}
