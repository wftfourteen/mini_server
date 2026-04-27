# 编译器和标准
CXX      = g++
CXXFLAGS = -std=c++14 -Wall -Wextra -g -pthread

# 目标文件名
TARGET = server

# 源文件
SRCS = server.cpp http_request.cpp http_response.cpp

# 目标文件
OBJS = $(SRCS:.cpp=.o)

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

# 编译每个 .cpp 为 .o
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理编译产物
clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
