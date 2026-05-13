#ifndef CONNECTION_H
#define CONNECTION_H

#include <cstddef>
#include <mutex>
#include <string>

class Connection {
public:
    explicit Connection(int fd);

    int fd() const { return fd_; }

    void appendReadData(const char* data, std::size_t size);
    bool extractOneRequest(std::string& rawRequest);

    void appendWriteData(const std::string& data);
    bool hasWriteData() const;
    std::string& writeBuffer() { return writeBuf_; }

    bool writeReady() const { return writeReady_; }
    void setWriteReady(bool value) { writeReady_ = value; }

    bool closeAfterWrite() const { return closeAfterWrite_; }
    void setCloseAfterWrite(bool value) { closeAfterWrite_ = value; }

    bool processing() const { return processing_; }
    void setProcessing(bool value) { processing_ = value; }

    bool closed() const { return closed_; }
    void markClosed() { closed_ = true; }

    bool hasPendingRead() const { return !readBuf_.empty(); }

    std::mutex& mutex() { return mtx_; }

    static std::size_t parseContentLength(const std::string& headersText);

private:
    int fd_;
    std::string readBuf_;
    std::string writeBuf_;
    bool writeReady_;
    bool closeAfterWrite_;
    bool processing_;
    bool closed_;
    mutable std::mutex mtx_;
};

#endif // CONNECTION_H
