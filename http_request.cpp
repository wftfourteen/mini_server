/**
 * http_request.cpp
 * HTTP 请求解析器实现
 */

#include "http_request.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <cstdlib>

// 匹配 HTTP 请求行的正则表达式
// 捕获组：(方法) (路径) (版本)
const std::regex HttpRequest::REQUEST_LINE_REGEX(
    "^([A-Z]+) ([^ ]+) HTTP/([0-9.]+)\r?$"
);

HttpRequest::HttpRequest()
    : state_(ParseState::REQUEST_LINE)
    , method_(HttpMethod::BAD_METHOD)
    , keepAlive_(false)
{}

// ============================================================
// parse() —— 对外主接口，逐行驱动状态机
// ============================================================
bool HttpRequest::parse(const std::string& data) {
    if (data.empty()) return false;

    size_t pos = 0;
    size_t line_start = 0;

    while (pos < data.size()) {
        // 查找行尾：\r\n
        size_t line_end = data.find("\r\n", pos);

        if (line_end == std::string::npos) {
            // 数据不完整，等下次再解析
            break;
        }

        std::string line = data.substr(line_start, line_end - line_start);

        switch (state_) {
            case ParseState::REQUEST_LINE:
                if (!parseRequestLine(line)) return false;
                state_ = ParseState::HEADERS;
                break;

            case ParseState::HEADERS:
                if (line.empty()) {
                    // 空行 = 头部结束
                    // GET 请求没有 body，直接完成
                    if (method_ == HttpMethod::GET) {
                        state_ = ParseState::FINISH;
                        return true;
                    }
                    // POST/PUT 可能有 body
                    state_ = ParseState::BODY;
                    line_start = line_end + 2;  // 跳过 \r\n
                    // 把剩余数据作为 body
                    parseBody(data, line_start);
                    return true;
                }
                if (!parseHeader(line)) return false;
                break;

            case ParseState::BODY:
                // 不会走到这里，body 在上面直接处理了
                break;

            case ParseState::FINISH:
                return true;
        }

        pos = line_end + 2;  // 跳过 \r\n
        line_start = pos;
    }

    return (state_ == ParseState::FINISH);
}

// ============================================================
// 解析请求行：GET /index.html HTTP/1.1
// ============================================================
bool HttpRequest::parseRequestLine(const std::string& line) {
    std::smatch matches;
    if (!std::regex_match(line, matches, REQUEST_LINE_REGEX)) {
        std::cerr << "[解析错误] 请求行格式错误: " << line << std::endl;
        return false;
    }

    // 捕获组1: 方法
    const std::string& method_str = matches[1].str();
    if      (method_str == "GET")    method_ = HttpMethod::GET;
    else if (method_str == "POST")   method_ = HttpMethod::POST;
    else if (method_str == "PUT")    method_ = HttpMethod::PUT;
    else if (method_str == "DELETE") method_ = HttpMethod::DELETE;
    else {
        method_ = HttpMethod::BAD_METHOD;
        std::cerr << "[解析错误] 不支持的请求方法: " << method_str << std::endl;
        return false;
    }

    // 捕获组2: 路径（URL 解码）
    path_ = urlDecode(matches[2].str());

    // 捕获组3: HTTP 版本
    version_ = "HTTP/" + matches[3].str();
    keepAlive_ = (version_ == "HTTP/1.1");

    std::cout << "[解析] 请求行: " << method_str << " " << path_
              << " " << version_ << std::endl;
    return true;
}

// ============================================================
// 解析请求头：Key: Value
// ============================================================
bool HttpRequest::parseHeader(const std::string& line) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) {
        std::cerr << "[解析错误] 头部格式错误: " << line << std::endl;
        return false;
    }

    std::string key = normalizeHeaderKey(trim(line.substr(0, colon)));
    std::string value = trim(line.substr(colon + 1));

    headers_[key] = value;

    // 特殊处理 Connection 头
    if (key == "connection") {
        std::string loweredValue = normalizeHeaderKey(value);
        if (loweredValue == "close") {
            keepAlive_ = false;
        } else if (loweredValue == "keep-alive") {
            keepAlive_ = true;
        }
    }

    return true;
}

// ============================================================
// 解析请求体（POST 数据）
// ============================================================
void HttpRequest::parseBody(const std::string& data, size_t bodyStart) {
    if (bodyStart < data.size()) {
        body_ = data.substr(bodyStart);
    }
    std::cout << "[解析] 请求体长度: " << body_.size() << " 字节" << std::endl;
}

// ============================================================
// 获取头部值
// ============================================================
std::string HttpRequest::getHeader(const std::string& key) const {
    auto it = headers_.find(normalizeHeaderKey(key));
    return (it != headers_.end()) ? it->second : "";
}

std::string HttpRequest::normalizeHeaderKey(const std::string& key) {
    std::string normalized = key;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return normalized;
}

std::string HttpRequest::trim(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

// ============================================================
// URL 解码：把 %20 还原为空格，+ 还原为空格等
// ============================================================
std::string HttpRequest::urlDecode(const std::string& src) {
    std::string result;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '%' && i + 2 < src.size()) {
            // %XX → 十六进制字符
            char hex[3] = { src[i+1], src[i+2], '\0' };
            char ch = static_cast<char>(strtol(hex, nullptr, 16));
            result += ch;
            i += 2;
        } else if (src[i] == '+') {
            result += ' ';
        } else {
            result += src[i];
        }
    }
    return result;
}
