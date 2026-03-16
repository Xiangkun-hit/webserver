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

// ===================== Day8 新增：根据文件后缀返回Content-Type =====================
const char* get_content_type(const char* file_name) {
    // 查找文件名最后一个 .（后缀）
    const char* ext = strrchr(file_name, '.');
    if (ext == NULL) return "text/html; charset=utf-8";
    
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0)  return "text/css";
    if (strcmp(ext, ".js") == 0)   return "application/javascript";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0) return "image/jpeg";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    
    return "text/html; charset=utf-8";
}

//打印请求日志
void log_request(const char* method, const char* path)
{
    time_t now = time(NULL);
    char time_str[30];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    printf("[%s] %s 请求 | 路径：%s\n", time_str, method, path);
}

int main() {
    // 1. 创建 socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    setnonblocking(server_fd);

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

    // 创建 epoll 实例（EPOLL_CLOEXEC 防止子进程继承fd泄漏）
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        perror("epoll_create1 failed");
        exit(EXIT_FAILURE);
    }

    // 定义 epoll 事件结构体
    struct epoll_event ev;
    // 监听 socket 关注「可读事件」（新连接），LT 模式（默认，无需额外标记）
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd;  // 绑定监听 socket
    // 将监听 socket 加入 epoll 监听列表
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("epoll_ctl add server_fd failed");
        exit(EXIT_FAILURE);
    }

    // ==================== Day2 新增：创建线程池（8个线程，可修改） ====================
    ThreadPool pool(8);
    struct epoll_event events[1024];

    // 4. 循环接受客户端连接
    while (true)
    {
        // 等待事件发生（阻塞直到有事件，-1 表示无限等待）
        int n = epoll_wait(epfd, events, 1024, -1);
        if (n < 0) {
            perror("epoll_wait failed");
            exit(EXIT_FAILURE);
        }
        for(int i = 0 ; i < n; ++i){
            int fd = events[i].data.fd;

            // 事件 1：监听 socket 有新连接
            if (fd == server_fd) {
                socklen_t addr_len = sizeof(address);
                int client_fd = accept(server_fd, (sockaddr*)&address, &addr_len);

                if(client_fd < 0) continue; // 非阻塞模式下 无连接则直接跳过

                std::cout << "新客户端连接！\n";
                setnonblocking(client_fd);

                // 将客户端 socket 加入 epoll 监听，关注「可读事件」（客户端发数据）
                ev.events = EPOLLIN | EPOLLET;  // ET 模式
                ev.data.fd = client_fd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                perror("epoll_ctl add client_fd failed");
                close(client_fd);
                continue;
                }
            }
            // 事件 2：客户端 socket 有数据可读
            else if(events[i].events & EPOLLIN){
                // 先从 epoll 中删除该 client_fd（避免重复触发）
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                // 封装成任务，交给线程池处理
                Task task;
                task.client_fd = fd;
                pool.addTask(task);
            }
        }
    }
    close(epfd); 
    close(server_fd);
    return 0;
    
}