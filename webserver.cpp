#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <signal.h>

int main() {
    // 1. 创建 socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }


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

    std::cout << "并发loop服务器启动,等待连接... http://127.0.0.1:8080" << std::endl;
    signal(SIGCHLD, SIG_IGN); // 自动清理子进程

    // 4. 循环接受客户端连接
    while (true)
    {
        socklen_t addr_len = sizeof(address);
        int new_socket = accept(server_fd, (sockaddr*)&address, &addr_len);
        std::cout << "新客户端连接！\n";

        pid_t pid = fork();
        if (pid == 0)
        {
            // 子进程：只处理当前客户端
            close(server_fd);
            char buffer[1024] = {0};
            read(new_socket, buffer, 1024);
            
            sleep(5);// 模拟慢请求（sleep5秒），测试并发！

            const char* response = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n\r\n"
                "<h1>Hello WebServer (Day1)</h1>";
            send(new_socket, response, strlen(response), 0);

            close(new_socket);
            std::cout << "连接成功,kill子进程" << std::endl;
            exit(0);
        }
        else
        {
            // 父进程：继续等待新连接，不卡顿
            close(new_socket);
        }
    }

    close(server_fd);
    return 0;
    
}