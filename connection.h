#ifndef CONNECTION_H
#define CONNECTION_H

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

class MappedFile;

class Connection {
public:
    Connection(int fd, const std::string& peerIp = "");

    int fd() const { return fd_; }
    const std::string& peerIp() const { return peerIp_; }

    void appendReadData(const char* data, std::size_t size);
    bool extractOneRequest(std::string& rawRequest);

    void appendWriteData(const std::string& data);
    void setMappedWriteData(const std::string& header, const std::shared_ptr<MappedFile>& file);
    bool hasWriteData() const;
    std::string& writeBuffer() { return writeBuf_; }
    const std::shared_ptr<MappedFile>& mappedFile() const { return mappedFile_; }
    std::size_t writeOffset() const { return writeOffset_; }
    std::size_t fileOffset() const { return fileOffset_; }
    void consumeWriteData(std::size_t bytes);
    void clearWriteData();

    bool writeReady() const { return writeReady_; }
    void setWriteReady(bool value) { writeReady_ = value; }

    bool closeAfterWrite() const { return closeAfterWrite_; }
    void setCloseAfterWrite(bool value) { closeAfterWrite_ = value; }

    bool processing() const { return processing_; }
    void setProcessing(bool value) { processing_ = value; }

    bool closed() const { return closed_; }
    void markClosed() { closed_ = true; }

    bool hasPendingRead() const { return !readBuf_.empty(); }
    void touch();
    bool idleForAtLeast(std::chrono::seconds timeout) const;

    std::mutex& mutex() { return mtx_; }

    static std::size_t parseContentLength(const std::string& headersText);

private:
    int fd_;
    std::string peerIp_;
    std::string readBuf_;
    std::string writeBuf_;
    std::shared_ptr<MappedFile> mappedFile_;
    std::size_t writeOffset_;
    std::size_t fileOffset_;
    bool writeReady_;
    bool closeAfterWrite_;
    bool processing_;
    bool closed_;
    std::chrono::steady_clock::time_point lastActive_;
    mutable std::mutex mtx_;
};

#endif // CONNECTION_H
