/**
 * http_request.h
 * HTTP 请求解析器 —— 正则 + 状态机解析 HTTP/1.1 请求报文
 *
 * 解析目标：
 *   1. 请求行：GET /index.html HTTP/1.1
 *   2. 请求头部：Host / Connection / Content-Length 等
 *   3. 请求体（POST 时需要）
 *
 * 状态机设计（每个状态对应解析的一个阶段）：
 *   REQUEST_LINE  → HEADERS → BODY → FINISH
 */

#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>
#include <unordered_map>
#include <regex>

// 解析状态
enum class ParseState {
    REQUEST_LINE,    // 正在解析请求行
    HEADERS,         // 正在解析请求头
    BODY,            // 正在解析请求体
    FINISH           // 解析完成
};

// HTTP 请求方法
enum class HttpMethod {
    GET, POST, PUT, DELETE, BAD_METHOD
};

class HttpRequest {
public:
    HttpRequest();

    /**
     * 解析一段收到的数据
     * @param data  原始数据（可能是不完整的）
     * @return true 表示解析完成，false 表示还需要更多数据或出错
     */
    bool parse(const std::string& data);

    // ========== 获取解析结果 ==========

    HttpMethod getMethod() const { return method_; }
    const std::string& getPath() const { return path_; }
    const std::string& getVersion() const { return version_; }
    bool isKeepAlive() const { return keepAlive_; }

    /** 获取请求头的值，不存在则返回空串 */
    std::string getHeader(const std::string& key) const;

    /** 获取请求体（POST 数据） */
    const std::string& getBody() const { return body_; }

    /** 将路径中的 %XX 编码还原（URL 解码） */
    static std::string urlDecode(const std::string& src);

private:
    // ---- 解析各部分的内部方法 ----
    bool parseRequestLine(const std::string& line);
    bool parseHeader(const std::string& line);
    void parseBody(const std::string& data, size_t bodyStart);
    static std::string normalizeHeaderKey(const std::string& key);
    static std::string trim(const std::string& value);

    // ---- 状态 ----
    ParseState state_;

    // ---- 解析结果 ----
    HttpMethod method_;
    std::string path_;
    std::string version_;
    std::string body_;
    bool keepAlive_;

    // ---- 头部键值对 ----
    std::unordered_map<std::string, std::string> headers_;

    // ---- 正则表达式（匹配请求行） ----
    // 请求行格式：METHOD SP URI SP HTTP/version CRLF
    static const std::regex REQUEST_LINE_REGEX;
};

#endif // HTTP_REQUEST_H
