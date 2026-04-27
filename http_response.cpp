/**
 * http_response.cpp
 * HTTP 响应构建器实现
 */

#include "http_response.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <iostream>

HttpResponse::HttpResponse()
    : statusCode_(200)
{}

// ============================================================
// MIME 类型映射表
// ============================================================
std::string HttpResponse::getMimeType(const std::string& path) {
    // 找文件扩展名（最后一个 . 之后的部分）
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";

    std::string ext = path.substr(dot + 1);
    // 转小写
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    static const std::unordered_map<std::string, std::string> MIME_MAP = {
        // 文本类
        {"html",  "text/html; charset=utf-8"},
        {"htm",   "text/html; charset=utf-8"},
        {"css",   "text/css; charset=utf-8"},
        {"js",    "application/javascript; charset=utf-8"},
        {"json",  "application/json; charset=utf-8"},
        {"xml",   "application/xml; charset=utf-8"},
        {"txt",   "text/plain; charset=utf-8"},
        {"md",    "text/markdown; charset=utf-8"},
        {"csv",   "text/csv; charset=utf-8"},

        // 图片类
        {"png",   "image/png"},
        {"jpg",   "image/jpeg"},
        {"jpeg",  "image/jpeg"},
        {"gif",   "image/gif"},
        {"svg",   "image/svg+xml"},
        {"ico",   "image/x-icon"},
        {"webp",  "image/webp"},
        {"bmp",   "image/bmp"},

        // 音视频
        {"mp3",   "audio/mpeg"},
        {"mp4",   "video/mp4"},
        {"webm",  "video/webm"},
        {"ogg",   "audio/ogg"},
        {"wav",   "audio/wav"},

        // 字体
        {"woff",  "font/woff"},
        {"woff2", "font/woff2"},
        {"ttf",   "font/ttf"},
        {"eot",   "application/vnd.ms-fontobject"},

        // 压缩包
        {"zip",   "application/zip"},
        {"gz",    "application/gzip"},
        {"pdf",   "application/pdf"},
    };

    auto it = MIME_MAP.find(ext);
    if (it != MIME_MAP.end()) return it->second;

    return "application/octet-stream";  // 未知类型，浏览器会尝试下载
}

// ============================================================
// HTTP 状态行
// ============================================================
std::string HttpResponse::getStatusLine(int code) {
    static const std::unordered_map<int, std::string> STATUS_MAP = {
        {200, "OK"},
        {301, "Moved Permanently"},
        {304, "Not Modified"},
        {400, "Bad Request"},
        {403, "Forbidden"},
        {404, "Not Found"},
        {405, "Method Not Allowed"},
        {413, "Payload Too Large"},
        {500, "Internal Server Error"},
        {502, "Bad Gateway"},
        {503, "Service Unavailable"},
    };

    std::string text = "Unknown";
    auto it = STATUS_MAP.find(code);
    if (it != STATUS_MAP.end()) text = it->second;

    return "HTTP/1.1 " + std::to_string(code) + " " + text + "\r\n";
}

// ============================================================
// 读取文件内容
// ============================================================
std::string HttpResponse::readFileContent(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    // 获取文件大小
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    // 读取全部内容
    std::string content(size, '\0');
    file.read(&content[0], size);

    return content;
}

// ============================================================
// build() —— 构建完整 HTTP 响应
// ============================================================
std::string HttpResponse::build(const std::string& path,
                                bool isKeepAlive,
                                const std::string& htmlRoot) {
    // ---- 1. 路径安全检查 ----
    // 防止目录穿越攻击：不允许路径中包含 ".."
    if (path.find("..") != std::string::npos) {
        statusCode_ = 403;
        body_ = "<html><body><h1>403 Forbidden</h1><p>Access denied.</p></body></html>";
        contentType_ = "text/html; charset=utf-8";
    }
    // ---- 2. 默认首页 ----
    else if (path == "/" || path.empty()) {
        statusCode_ = 200;
        contentType_ = "text/html; charset=utf-8";
        body_ = readFileContent(htmlRoot + "/index.html");
        if (body_.empty()) {
            // index.html 不存在，生成默认欢迎页
            statusCode_ = 200;
            body_ = "<html><body>"
                    "<h1>Welcome to MiniServer!</h1>"
                    "<p>Static file server is running.</p>"
                    "<p>Put your files in the <b>webroot/</b> directory.</p>"
                    "</body></html>";
        }
    }
    // ---- 3. 正常文件请求 ----
    else {
        std::string filepath = htmlRoot + path;
        body_ = readFileContent(filepath);

        if (body_.empty()) {
            // 文件不存在 → 404
            statusCode_ = 404;
            contentType_ = "text/html; charset=utf-8";
            body_ = "<html><body>"
                    "<h1>404 Not Found</h1>"
                    "<p>The requested URL was not found on this server.</p>"
                    "</body></html>";
            std::cout << "[响应] 404 文件未找到: " << filepath << std::endl;
        } else {
            statusCode_ = 200;
            contentType_ = getMimeType(filepath);
        }
    }

    // ---- 4. 构建响应 ----
    std::string response;
    response += getStatusLine(statusCode_);

    // 响应头部
    response += "Server: MiniServer/1.0\r\n";
    response += "Content-Type: " + contentType_ + "\r\n";
    response += "Content-Length: " + std::to_string(body_.size()) + "\r\n";
    response += "Connection: " + std::string(isKeepAlive ? "keep-alive" : "close") + "\r\n";
    // 关闭 gzip（最小服务器暂不实现压缩）
    response += "Accept-Ranges: bytes\r\n";

    // 空行分隔头部和 body
    response += "\r\n";

    // 拼接 body
    response += body_;

    return response;
}
