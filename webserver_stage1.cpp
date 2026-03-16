#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <signal.h>
//day 5 new:文件操作头文件
#include <cstdio>
#include <cstdlib>
#include <time.h>
#include <fcntl.h>

#include "ThreadPool.h"
using namespace std;
#include "Utils.h"
#include <sys/epoll.h>  // epoll 核心头文件
#include <errno.h>
#include "Reactor.h"

// // ===================== Day8 新增：根据文件后缀返回Content-Type =====================
// const char* get_content_type(const char* file_name) {
//     // 查找文件名最后一个 .（后缀）
//     const char* ext = strrchr(file_name, '.');
//     if (ext == NULL) return "text/html; charset=utf-8";
    
//     if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
//     if (strcmp(ext, ".css") == 0)  return "text/css";
//     if (strcmp(ext, ".js") == 0)   return "application/javascript";
//     if (strcmp(ext, ".png") == 0) return "image/png";
//     if (strcmp(ext, ".jpg") == 0) return "image/jpeg";
//     if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    
//     return "text/html; charset=utf-8";
// }

//打印请求日志
// void log_request(const char* method, const char* path)
// {
//     time_t now = time(NULL);
//     char time_str[30];
//     strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
//     printf("[%s] %s 请求 | 路径：%s\n", time_str, method, path);
// }
// ===================== 工具函数：打印请求日志 =====================


int main() {
    // 1. 创建 socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    Reactor::setnonblocking(server_fd);

    // ========== 端口复用（解决端口占用报错，第三天必加）==========
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    // 2. 绑定 IP 和端口
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080); // 端口 8080

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 3. 开始监听
    if (listen(server_fd, 5) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "POST交互式并发loop服务器启动,等待连接... http://127.0.0.1:8080" << std::endl;
    signal(SIGCHLD, SIG_IGN); // 自动清理子进程

    // ==================== Reactor 核心 ====================
    ThreadPool pool(8);               // 创建线程池
    Reactor reactor;                   // 创建反应堆
    reactor.bindThreadPool(&pool);     // 绑定线程池

    // 👇 关键：替换为 Proactor 模型（ET模式）
    reactor.addListenEvent(server_fd, true);
    cout << "✅ 模拟Proactor服务器启动!端口:8080" << endl; 
    // 若想切回Reactor，改回这行：
    // reactor.addEvent(server_fd, READ_EVENT, true);  // 监听socket加入Reactor（ET模式）
    // cout << "Reactor服务器启动成功!端口:8080" << endl;

    // 启动反应堆事件循环
    reactor.run();

    // 关闭
    close(server_fd);
    return 0;
    
}