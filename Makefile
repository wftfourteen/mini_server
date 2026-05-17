# 编译器和标准
CXX      = g++
MYSQL_CFLAGS := $(shell mysql_config --cflags 2>/dev/null)
MYSQL_LIBS   := $(shell mysql_config --libs 2>/dev/null)
CXXFLAGS = -std=c++14 -Wall -Wextra -g -pthread $(MYSQL_CFLAGS)
LDLIBS   = $(MYSQL_LIBS)

# 目标文件名
TARGET = server
TEST_TARGET = unit_tests

# 源文件
COMMON_SRCS = connection.cpp epoller.cpp thread_pool.cpp http_server.cpp http_request.cpp http_response.cpp logger.cpp database_pool.cpp user_service.cpp request_router.cpp timer_heap.cpp
SRCS = server.cpp $(COMMON_SRCS)
TEST_SRCS = tests/unit_tests.cpp connection.cpp http_request.cpp http_response.cpp database_pool.cpp user_service.cpp request_router.cpp timer_heap.cpp logger.cpp

# 目标文件
OBJS = $(SRCS:.cpp=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)

# 默认目标：编译
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDLIBS)
	@echo "============================================"
	@echo "  编译成功！运行方式："
	@echo "  ./server"
	@echo "  curl http://localhost:8080"
	@echo "  curl http://localhost:8080/test.html"
	@echo "============================================"

$(TEST_TARGET): $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(LDLIBS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

stress:
	python3 scripts/stress_test.py --host 127.0.0.1 --port 8080 --clients 64 --duration 10 --keepalive 4

stress-local: $(TARGET)
	bash scripts/run_stress_local.sh --host 127.0.0.1 --port 8080 --clients 64 --duration 10 --keepalive 4

smoke-auth: $(TARGET)
	bash scripts/smoke_auth.sh

smoke-timeout: $(TARGET)
	python3 scripts/smoke_timeout.py

validate: $(TARGET)
	bash scripts/run_validation_suite.sh

valgrind-check: $(TARGET)
	bash scripts/valgrind_check.sh

# 编译每个 .cpp 为 .o
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理编译产物
clean:
	rm -f $(TARGET) $(TEST_TARGET) $(OBJS) $(TEST_OBJS) test_empty.html test_users.db test_access.log

.PHONY: all test stress stress-local smoke-auth smoke-timeout validate valgrind-check clean
