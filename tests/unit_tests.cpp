#include "../connection.h"
#include "../http_request.h"
#include "../http_response.h"
#include "../logger.h"
#include "../timer_heap.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path.c_str(), std::ios::binary);
    out << content;
}

void test_request_line_and_headers() {
    HttpRequest request;
    std::string raw =
        "GET /index.html HTTP/1.1\r\n"
        "host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";

    assert(request.parse(raw));
    assert(request.getMethod() == HttpMethod::GET);
    assert(request.getPath() == "/index.html");
    assert(request.getVersion() == "HTTP/1.1");
    assert(request.getHeader("HOST") == "localhost");
    assert(!request.isKeepAlive());
}

void test_http11_defaults_to_keep_alive() {
    HttpRequest request;
    std::string raw =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    assert(request.parse(raw));
    assert(request.isKeepAlive());
}

void test_body_and_url_decode() {
    HttpRequest request;
    std::string raw =
        "POST /submit%20form HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "hello=world";

    assert(request.parse(raw));
    assert(request.getMethod() == HttpMethod::POST);
    assert(request.getPath() == "/submit form");
    assert(request.getBody() == "hello=world");
}

void test_connection_extracts_pipelined_requests() {
    Connection conn(1);
    std::string pipelined =
        "GET /one HTTP/1.1\r\nHost: x\r\n\r\n"
        "POST /two HTTP/1.1\r\nHost: x\r\ncontent-length: 4\r\n\r\nbody";

    conn.appendReadData(pipelined.data(), pipelined.size());

    std::string first;
    std::string second;
    assert(conn.extractOneRequest(first));
    assert(first == "GET /one HTTP/1.1\r\nHost: x\r\n\r\n");
    assert(conn.extractOneRequest(second));
    assert(second == "POST /two HTTP/1.1\r\nHost: x\r\ncontent-length: 4\r\n\r\nbody");
}

void test_connection_waits_for_complete_body() {
    Connection conn(1);
    std::string partial =
        "POST /two HTTP/1.1\r\n"
        "Host: x\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "abc";

    conn.appendReadData(partial.data(), partial.size());
    std::string raw;
    assert(!conn.extractOneRequest(raw));

    std::string rest = "de";
    conn.appendReadData(rest.data(), rest.size());
    assert(conn.extractOneRequest(raw));
    assert(raw.find("abcde") != std::string::npos);
}

void test_response_builds_200_404_403_and_empty_file() {
    write_file("test_empty.html", "");

    HttpResponse okResponse;
    std::string ok = okResponse.build("/test.html", true, "webroot");
    assert(ok.find("HTTP/1.1 200 OK\r\n") == 0);
    assert(ok.find("Connection: keep-alive\r\n") != std::string::npos);
    assert(ok.find("Content-Type: text/html; charset=utf-8\r\n") != std::string::npos);

    HttpResponse notFoundResponse;
    std::string notFound = notFoundResponse.build("/missing-file.html", false, "webroot");
    assert(notFound.find("HTTP/1.1 404 Not Found\r\n") == 0);
    assert(notFound.find("Connection: close\r\n") != std::string::npos);

    HttpResponse forbiddenResponse;
    std::string forbidden = forbiddenResponse.build("/../secret.txt", false, "webroot");
    assert(forbidden.find("HTTP/1.1 403 Forbidden\r\n") == 0);

    HttpResponse emptyResponse;
    std::string empty = emptyResponse.build("/test_empty.html", false, ".");
    assert(empty.find("HTTP/1.1 200 OK\r\n") == 0);
    assert(empty.find("Content-Length: 0\r\n") != std::string::npos);
}

void test_response_prepares_mapped_static_files() {
    HttpResponse okResponse;
    PreparedResponse ok = okResponse.prepare("/test.html", true, "webroot");
    assert(ok.statusCode == 200);
    assert(ok.file);
    assert(ok.body.empty());
    assert(ok.header.find("HTTP/1.1 200 OK\r\n") == 0);
    assert(ok.header.find("Connection: keep-alive\r\n") != std::string::npos);
    assert(ok.bodySize() == ok.file->size());

    write_file("test_empty.html", "");
    HttpResponse emptyResponse;
    PreparedResponse empty = emptyResponse.prepare("/test_empty.html", false, ".");
    assert(empty.statusCode == 200);
    assert(empty.file);
    assert(empty.bodySize() == 0);
    assert(empty.header.find("Content-Length: 0\r\n") != std::string::npos);

    HttpResponse missingResponse;
    PreparedResponse missing = missingResponse.prepare("/missing-file.html", false, "webroot");
    assert(missing.statusCode == 404);
    assert(!missing.file);
    assert(!missing.body.empty());
}

void test_timer_heap_add_update_remove_and_expire() {
    TimerHeap timers;
    TimerHeap::TimePoint now = TimerHeap::Clock::now();

    timers.addOrUpdate(1, now + std::chrono::seconds(5));
    timers.addOrUpdate(2, now + std::chrono::seconds(2));
    timers.addOrUpdate(1, now + std::chrono::seconds(1));
    timers.remove(2);

    std::vector<int> first = timers.popExpired(now + std::chrono::seconds(1));
    assert(first.size() == 1);
    assert(first[0] == 1);
    assert(timers.empty());

    timers.addOrUpdate(3, now + std::chrono::seconds(3));
    std::vector<int> none = timers.popExpired(now + std::chrono::seconds(2));
    assert(none.empty());
    std::vector<int> second = timers.popExpired(now + std::chrono::seconds(3));
    assert(second.size() == 1);
    assert(second[0] == 3);
}

void test_async_singleton_logger_flushes_lines() {
    const std::string path = "test_access.log";
    std::remove(path.c_str());

    Logger& first = Logger::instance();
    Logger& second = Logger::instance();
    assert(&first == &second);

    first.init(path);
    first.info("timer ready");
    first.access("127.0.0.1", "GET", "/", 200, 128);
    first.shutdown();

    std::ifstream in(path.c_str());
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string content = buffer.str();
    assert(content.find("INFO timer ready") != std::string::npos);
    assert(content.find("ACCESS 127.0.0.1 GET / 200 128") != std::string::npos);
}

} // namespace

int main() {
    test_request_line_and_headers();
    test_http11_defaults_to_keep_alive();
    test_body_and_url_decode();
    test_connection_extracts_pipelined_requests();
    test_connection_waits_for_complete_body();
    test_response_builds_200_404_403_and_empty_file();
    test_response_prepares_mapped_static_files();
    test_timer_heap_add_update_remove_and_expire();
    test_async_singleton_logger_flushes_lines();

    std::cout << "All unit tests passed." << std::endl;
    return 0;
}
