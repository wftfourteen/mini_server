# 编译器和标准
CXX      = g++
CXXFLAGS = -std=c++14 -Wall -Wextra -g -pthread

# 目标文件名
TARGET = server
TEST_TARGET = unit_tests

# 源文件
COMMON_SRCS = connection.cpp epoller.cpp thread_pool.cpp http_server.cpp http_request.cpp http_response.cpp
SRCS = server.cpp $(COMMON_SRCS)
TEST_SRCS = tests/unit_tests.cpp connection.cpp http_request.cpp http_response.cpp

# 目标文件
OBJS = $(SRCS:.cpp=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)

# 默认目标：编译
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)
	@echo "============================================"
	@echo "  编译成功！运行方式："
	@echo "  ./server"
	@echo "  curl http://localhost:8080"
	@echo "  curl http://localhost:8080/test.html"
	@echo "============================================"

$(TEST_TARGET): $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $(TEST_TARGET) $(TEST_OBJS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

stress:
	python3 scripts/stress_test.py --host 127.0.0.1 --port 8080 --clients 64 --duration 10 --keepalive 4

stress-local: $(TARGET)
	bash scripts/run_stress_local.sh --host 127.0.0.1 --port 8080 --clients 64 --duration 10 --keepalive 4

# 编译每个 .cpp 为 .o
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理编译产物
clean:
	rm -f $(TARGET) $(TEST_TARGET) $(OBJS) $(TEST_OBJS) test_empty.html

.PHONY: all test stress stress-local clean
