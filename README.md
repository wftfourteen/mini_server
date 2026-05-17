# MiniServer v3 —— 线程池 + Keep-Alive 的 epoll HTTP 静态文件服务器

## 文件结构

```
mini_server/
├── server.cpp          # 主程序：epoll 事件循环 + 连接管理
├── http_request.h      # HTTP 请求解析器（头文件）
├── http_request.cpp    # HTTP 请求解析器（实现）
├── http_response.h     # HTTP 响应构建器（头文件）
├── http_response.cpp   # HTTP 响应构建器（实现）
├── Makefile            # 构建脚本
├── README.md           # 本文件
└── webroot/            # 静态文件根目录
    ├── index.html      # 默认首页
    ├── test.html       # 测试页面
    └── sub/
        └── hello.html  # 子目录测试文件
```

## 快速上手

### 1. WSL 环境中编译运行

```bash
# 复制到 Linux（只做一次）
mkdir -p ~/mini_server
cp -r /mnt/d/code/mini_server/* ~/mini_server/
cd ~/mini_server

# 编译
make

# 运行（确保在 mini_server 目录下，这样 webroot/ 能找到）
./server
```

### 2. 单元测试

```bash
make test
```

测试覆盖 HTTP 请求解析、Header 大小写、Keep-Alive、请求切包、半包等待、响应状态码和空文件响应。

### 3. 手动测试

```bash
# 访问首页
curl http://localhost:8080

# 访问测试页面
curl http://localhost:8080/test.html

# 访问子目录文件
curl http://localhost:8080/sub/hello.html

# 触发 404
curl http://localhost:8080/not-exist

# 查看完整响应头
curl -v http://localhost:8080

# 浏览器测试
# 打开 http://localhost:8080
```

### 4. 压力测试

先启动服务器：

```bash
make
./server
```

另开一个终端运行：

```bash
make stress
```

或者让脚本自动启动并清理本地服务器：

```bash
make stress-local
```

也可以手动调参数：

```bash
python3 scripts/stress_test.py --clients 128 --duration 30 --keepalive 8
```

生成 JSON 压测报告：

```bash
python3 scripts/stress_test.py --clients 128 --duration 30 --keepalive 8 \
  --report reports/stress_128c.json
```

一键验证：

```bash
make validate
```

内存泄漏验证：

```bash
make valgrind-check
```

该目标会在 Valgrind 下启动真实服务，覆盖静态资源、注册登录和空闲连接超时路径，并把报告写入 `reports/valgrind.log`。

---

## 架构说明

当前代码按职责拆分为：

```
server.cpp          # 程序入口
http_server.*       # 监听 socket、epoll 主循环、连接调度
epoller.*           # epoll add/mod/del/wait 封装
connection.*        # 单连接读写缓冲、请求切包、连接状态
thread_pool.*       # 固定线程池和任务队列
http_request.*      # HTTP 请求解析
http_response.*     # 静态文件响应构建
tests/unit_tests.cpp
scripts/stress_test.py
```

## 新增能力

### 访问日志

服务器使用单例异步日志器：业务线程只把日志写入阻塞队列，后台线程批量落盘到 `logs/access.log`，格式如下：

```text
2026-05-16 10:30:00 ACCESS 127.0.0.1 GET /index.html 200 1024
```

### 空闲连接超时

默认空闲超时时间是 30 秒。每个连接进入小根堆定时器，读写活跃时刷新过期时间；主循环周期性弹出堆顶过期节点并关闭连接。新增、刷新和删除的核心复杂度为 `O(logN)`，超时关闭会在日志中记录 `idle timeout`。

### 注册与登录

用户数据保存在 MySQL，服务端维护固定大小的数据库连接池。

注册：

```bash
curl -X POST http://localhost:8080/register \
  -d "username=alice&password=secret"
```

登录：

```bash
curl -X POST http://localhost:8080/login \
  -d "username=alice&password=secret"
```

也可以直接跑端到端认证冒烟测试：

```bash
make smoke-auth
```

空闲连接踢除测试：

```bash
make smoke-timeout
```

当前实现面向学习和演示，密码仍以明文保存；如果要进入生产环境，下一步应改成带盐哈希，并补 session/token、限流和更完整的输入校验。

启动前需要先准备 MySQL：

```bash
sudo apt update
sudo apt install -y libmysqlclient-dev
mysql -uroot -p -e "CREATE DATABASE IF NOT EXISTS mini_server;"
```

默认连接参数是：

```text
host=127.0.0.1
port=3306
user=root
password=
database=mini_server
```

可以在 `HttpServerConfig::database` 中按本机环境调整。

更推荐用环境变量覆盖，避免把密码写进源码：

```bash
export MINI_DB_HOST=127.0.0.1
export MINI_DB_PORT=3306
export MINI_DB_USER=mini_user
export MINI_DB_PASSWORD=your_password
export MINI_DB_NAME=mini_server
```

### 请求处理流程

```
客户端请求
    │
    ▼
[epoll_wait] 监听事件
    │
    ├─ listen_fd 可读 → handle_accept()
    │   └─ accept() + 注册 EPOLLIN
    │
    ├─ conn_fd 可读 → handle_read()
    │   ├─ read() 循环读取数据到 readBuf
    │   ├─ 检测 "\r\n\r\n" → 请求头收完
    │   └─ 仅负责收包并投递任务到线程池
    │
    ├─ Worker 线程
    │   ├─ 从任务队列取连接
    │   ├─ 解析完整请求（支持 Content-Length）
    │   ├─ HttpResponse::build() 构建响应
    │   └─ epoll_mod(EPOLLOUT) 通知主线程发送
    │
    └─ conn_fd 可写（主线程） → handle_write()
        ├─ write() 循环写出 writeBuf
        ├─ Connection: close → 写完关闭
        └─ Connection: keep-alive → 回到 EPOLLIN 继续复用
```

### HTTP 请求解析（状态机）

```
┌─────────────────┐
│  REQUEST_LINE   │ ← 解析 "GET /index.html HTTP/1.1"
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│    HEADERS      │ ← 逐行解析 "Key: Value"
└────────┬────────┘
         │ 空行 "\r\n"
         ▼
┌─────────────────┐
│      BODY       │ ← 读取 POST 数据（可选）
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│     FINISH      │ ← 解析完成
└─────────────────┘
```

### MIME 类型支持

| 扩展名 | Content-Type |
|--------|-------------|
| .html | text/html; charset=utf-8 |
| .css | text/css; charset=utf-8 |
| .js | application/javascript; charset=utf-8 |
| .json | application/json; charset=utf-8 |
| .png | image/png |
| .jpg/.jpeg | image/jpeg |
| .gif | image/gif |
| .svg | image/svg+xml |
| .ico | image/x-icon |
| .mp4 | video/mp4 |
| .pdf | application/pdf |
| .zip | application/zip |

---

## 安全特性

- **目录穿越防护**：路径中的 `..` 会被拒绝，返回 403
- **URL 解码**：支持 `%20`、`%2F` 等编码字符
- **请求方法验证**：只允许 GET/POST/PUT/DELETE

---

## 已实现升级

- [x] 线程池：主线程只做事件分发，工作线程负责读写、解析和响应构建
- [x] EPOLLONESHOT：保证同一 fd 同时只被一个工作线程处理
- [x] Keep-Alive 复用：同一个连接可连续处理多个请求
- [x] 单例异步日志：阻塞队列 + 后台线程批量落盘
- [x] 小根堆定时器：按过期时间管理空闲连接
- [x] MySQL 连接池：RAII Lease 管理连接归还

## 验证清单

- `make test`：协议解析、连接切包、响应构建、定时器、异步日志
- `make smoke-auth`：注册、登录、错误密码、重复注册、访问日志
- `make smoke-timeout`：空闲连接超时关闭
- `make stress-local`：本地并发静态资源压测
- `make validate`：一键运行完整验证套件
