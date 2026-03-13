#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

int main() {
    // 1. 创建 socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

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

    std::cout << "服务器启动，等待连接... http://127.0.0.1:8080" << std::endl;

    // 4. 接受客户端连接
    socklen_t addr_len = sizeof(address);
    int new_socket = accept(server_fd, (sockaddr*)&address, &addr_len);
    if (new_socket < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    std::cout << "有客户端连接！" << std::endl;

    // 5. 回复浏览器
    const char* response = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<h1>Hello WebServer (Day1)</h1>";
    send(new_socket, response, strlen(response), 0);

    // 6. 关闭连接
    close(new_socket);
    close(server_fd);
    return 0;
}