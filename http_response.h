/**
 * http_response.h
 * HTTP 响应构建器 —— 根据解析结果和文件内容构建完整 HTTP 响应
 *
 * 功能：
 *   1. 根据文件扩展名设置 Content-Type（MIME 类型映射）
 *   2. 构建状态行（HTTP/1.1 200/404/400）
 *   3. 构建响应头部
 *   4. 组装完整的 HTTP 响应报文
 */

#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <memory>
#include <string>

class MappedFile {
public:
    MappedFile();
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool open(const std::string& filepath);
    const char* data() const { return data_; }
    std::size_t size() const { return size_; }
    bool valid() const { return data_ != nullptr || size_ == 0; }

private:
    char* data_;
    std::size_t size_;
};

struct PreparedResponse {
    int statusCode;
    std::string header;
    std::string body;
    std::shared_ptr<MappedFile> file;

    std::size_t bodySize() const;
};

class HttpResponse {
public:
    HttpResponse();

    /**
     * 构建完整的 HTTP 响应
     * @param path         请求的文件路径
     * @param isKeepAlive  是否保持连接
     * @param htmlRoot     静态文件根目录
     * @return             完整的 HTTP 响应报文（头部 + body）
     */
    std::string build(const std::string& path, bool isKeepAlive, const std::string& htmlRoot);
    PreparedResponse prepare(const std::string& path,
                             bool isKeepAlive,
                             const std::string& htmlRoot);
    static std::string buildText(int statusCode,
                                 const std::string& body,
                                 bool isKeepAlive,
                                 const std::string& contentType);
    static int statusCodeFromResponse(const std::string& response);

private:
    // 根据文件扩展名获取 MIME 类型
    static std::string getMimeType(const std::string& path);

    // 生成 HTTP 状态码对应的状态行
    static std::string getStatusLine(int code);

    // 读取文件内容
    static bool readFileContent(const std::string& filepath, std::string& content);
    static std::string buildHeader(int statusCode,
                                   const std::string& contentType,
                                   std::size_t contentLength,
                                   bool isKeepAlive);

    // ---- 响应组件 ----
    int    statusCode_;
    std::string statusText_;
    std::string contentType_;
    std::string body_;
    std::string headers_;
};

#endif // HTTP_RESPONSE_H
