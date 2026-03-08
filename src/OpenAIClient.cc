#include "openai.hpp"

#include <curl/curl.h>
#include <cstdlib>
#include <cstring>

#include <Logger.h>
#include <nlohmann/json.hpp>

namespace {
size_t writeToString(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}
}

OpenAIClient::OpenAIClient(const std::string& apiKey)
    : apiKey_(apiKey)
    , model_("gpt-3.5-turbo")
    , baseUrl_("https://api.openai.com/v1/chat/completions") {
    const char* envKey = std::getenv("OPENAI_API_KEY");
    if (apiKey_.empty() && envKey) {
        apiKey_ = envKey;
    }

    const char* envBase = std::getenv("OPENAI_BASE_URL");
    if (envBase && std::strlen(envBase) > 0) {
        baseUrl_ = envBase;
    }
}

std::optional<std::string> OpenAIClient::chatCompletion(const std::vector<ChatMessage>& messages) const {
    if (apiKey_.empty()) {
        LOG_ERROR << "OpenAI API key missing. Set OPENAI_API_KEY env variable.";
        return std::nullopt;
    }

    if (messages.empty()) {
        LOG_ERROR << "chatCompletion called with empty messages";
        return std::nullopt;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR << "Failed to init CURL";
        return std::nullopt;
    }

    std::string responseBuffer;

    struct curl_slist* headers = nullptr;
    std::string authHeader = "Authorization: Bearer " + apiKey_;
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (baseUrl_.find("deepseek") != std::string::npos) {
        headers = curl_slist_append(headers, "Accept: application/json");
    }

    nlohmann::json jsonMessages = nlohmann::json::array();
    for (const auto& msg : messages) {
        jsonMessages.push_back({{"role", msg.role}, {"content", msg.content}});
    }

    nlohmann::json payload = {
        {"model", model_},
        {"messages", jsonMessages}
    };
    std::string payloadStr = payload.dump();
    curl_easy_setopt(curl, CURLOPT_URL, baseUrl_.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payloadStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payloadStr.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
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
