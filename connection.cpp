#include "connection.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

Connection::Connection(int fd)
    : fd_(fd)
    , writeReady_(false)
    , closeAfterWrite_(false)
    , processing_(false)
    , closed_(false) {}

void Connection::appendReadData(const char* data, std::size_t size) {
    readBuf_.append(data, size);
}

std::size_t Connection::parseContentLength(const std::string& headersText) {
    std::string lowered = to_lower(headersText);
    const std::string key = "content-length:";
    std::size_t pos = lowered.find(key);
    if (pos == std::string::npos) return 0;

    pos += key.size();
    while (pos < lowered.size() && (lowered[pos] == ' ' || lowered[pos] == '\t')) {
        ++pos;
    }

    std::size_t end = pos;
    while (end < lowered.size() &&
           std::isdigit(static_cast<unsigned char>(lowered[end]))) {
        ++end;
    }

    if (end == pos) return 0;

    try {
        return static_cast<std::size_t>(std::stoul(lowered.substr(pos, end - pos)));
    } catch (const std::exception&) {
        return 0;
    }
}

bool Connection::extractOneRequest(std::string& rawRequest) {
    std::size_t headerEnd = readBuf_.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;

    std::size_t headersLen = headerEnd + 4;
    std::string headersText = readBuf_.substr(0, headersLen);
    std::size_t contentLength = parseContentLength(headersText);

    std::size_t totalLen = headersLen + contentLength;
    if (readBuf_.size() < totalLen) return false;

    rawRequest = readBuf_.substr(0, totalLen);
    readBuf_.erase(0, totalLen);
    return true;
}

void Connection::appendWriteData(const std::string& data) {
    writeBuf_.append(data);
    writeReady_ = !writeBuf_.empty();
}

bool Connection::hasWriteData() const {
    return !writeBuf_.empty();
}
