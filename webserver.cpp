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
// ================================================================================

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
            char buffer[4096] = {0};
            read(new_socket, buffer, sizeof(buffer)-1);
            

            // ===================== Day4 核心：解析请求路径 =====================
            //=======统一解析请求
            char method[16] = {0};
            char path[100] = {0};
            sscanf(buffer, "%s %s HTTP", method, path);
            std::cout << "浏览器请求路径：" << path << std::endl;
            // ===================== 新增：屏蔽浏览器图标请求 =====================
            if(strcmp(path, "/favicon.ico") == 0 ||strlen(path) == 0) {
                close(new_socket);
                std::cout << "block ico/ 空路径\n";
                exit(0);
            }
            // ==================================================================               

            // 打印日志
            log_request(method, path);
            sleep(1);

            char response[8192] = {0};

            //===========处理POST提交==============
            if (strcmp(method, "POST") == 0  && strcmp(path, "/submit") == 0)
            {
                // 解析POST请求体（获取表单数据）
                char* body = strstr(buffer, "\r\n\r\n");
                if (body != NULL) body += 4;

                // 解析name参数
                char name[50] = {0};
                sscanf(body, "name=%s", name);

                // POST响应
                sprintf(response,
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=utf-8\r\n\r\n"
                    "<h1>✅ POST表单提交成功!</h1>"
                    "<h2>欢迎你：%s</h2>"
                    "<a href='/post'>返回POST表单页面</a>",
                    name);
                send(new_socket, response, strlen(response), 0);
                close(new_socket);
                exit(0);
            }
            //处理get带参数请求
            else if(strstr(path,"?") != NULL)
            {
                // 查找路径中的 ? 参数分隔符
                char *param_ptr = strstr(path, "?");
                if (param_ptr != NULL) 
                {
                    // 1. 截断路径，只保留前面的路由
                    *param_ptr = '\0';
                    // 2. 指针后移，拿到参数部分（name=xxx）
                    char *params = param_ptr + 1;

                    // 解析 name 参数
                    char name[50] = {0};
                    sscanf(params, "name=%s", name);

                    // 动态生成响应：根据用户传入的名字返回内容
                    sprintf(response,
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html; charset=utf-8\r\n\r\n"
                        "<h1>🎉 动态交互成功！</h1>"
                        "<h2>你好，%s!</h2>"
                            // ===================== 【Day7 新增：返回表单链接】 =====================
                        "<a href='/form'>返回表单页面</a>"
                        "<p>这是服务器为你定制的内容</p>",
                        name);
                    send(new_socket, response, strlen(response), 0);
                    close(new_socket);
                    exit(0);
                }
            }
            
            //处理静态文件/页面
            else
            {
                    // ===================== Day5 核心：读取本地HTML文件 =====================
                char file_name[128] = {0};
                // 先判断是否是带后缀的静态文件（.png/.css/.js等）
                const char* ext = strrchr(path, '.');
                if (ext != NULL && strlen(ext) > 1) {
                    // 去掉路径开头的 /，直接作为文件名
                    if (path[0] == '/') {
                        snprintf(file_name, sizeof(file_name), "%s", path + 1);
                    } else {
                        snprintf(file_name, sizeof(file_name), "%s", path);
                    }
                } 
                else
                {
                    // 路由匹配：路径对应本地html文件
                    if (strcmp(path, "/") == 0 || strcmp(path, "/index") == 0) {
                        strcpy(file_name, "index.html");   // 访问首页
                    } else if (strcmp(path, "/hello") == 0) {
                        strcpy(file_name, "hello.html");   // 访问Hello页
                    }
                    //=============Day7 增加form页面
                    else if (strcmp(path, "/form") == 0) {
                        strcpy(file_name, "form.html");   // 访问form页
                    } 
                    else if (strcmp(path, "/post") == 0) {
                        strcpy(file_name, "form_post.html");   // 访问form页
                    } 
                    else 
                    {
                        strcpy(file_name, "404.html");     // 访问错误页
                    }
                }
 
                // 打开本地文件
                FILE* file = fopen(file_name, "rb");
                char* file_content = NULL;
                // char file_content_empty[4096] = {0};  // 存储文件内容
                size_t file_size = 0;
                if (file != NULL) 
                {
                    //获取文件总大小
                    fseek(file,0,SEEK_END);
                    file_size = ftell(file);
                    fseek(file,0,SEEK_SET);

                    //动态分配足够大的缓冲区
                    file_content = (char*)malloc(file_size);
                    if (file_content == NULL)
                    {
                        file_content = (char*)"<h1>内存不足</h1>";
                        file_size = strlen(file_content);
                    }
                    else
                    {
                        //读取完整文件
                        file_size = fread(file_content,1,file_size,file);
                    }

                    fclose(file);
                    std::cout << "成功读取文件：" << file_name << std::endl;

                } 
                else 
                {
                    file_content = (char*) "<h1>文件读取失败</h1>";
                    std::cout << "读取文件失败：" << file_name << std::endl;
                    file_size = strlen(file_content);
                }

                // ===================== Day8 修改：动态Content-Type =====================
                //先发送HTTP响应头
                char header[256] = {0};
                const char* content_type = get_content_type(file_name);
                snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: %s\r\n\r\n",
                        content_type);
                
                send(new_socket, header, strlen(header), 0);

                // 再发送文件内容（二进制数据）
                send(new_socket, file_content, file_size, 0);

                // 释放动态分配的内存（如果是 malloc 的）
                if (file != NULL && file_content != NULL)
                {
                    free(file_content);
                }
                close(new_socket);
                exit(0);
            }                           
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