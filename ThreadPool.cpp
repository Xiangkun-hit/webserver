#include "ThreadPool.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <unistd.h>
using namespace std;

// ===================== 工具函数：根据文件后缀返回Content-Type =====================
static const char* get_content_type(const char* file_name) {
    // 查找文件名最后一个 .（后缀）
    const char* ext = strrchr(file_name, '.');
    if (ext == NULL) return "text/html; charset=utf-8";

    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0)  return "text/css";
    if (strcmp(ext, ".js") == 0)   return "application/javascript";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".ico") == 0)  return "image/x-icon";
    if (strcmp(ext, ".txt") == 0)  return "text/plain; charset=utf-8";

    return "application/octet-stream";
}

// ===================== 工具函数：打印请求日志 =====================
static void log_request(const char* method, const char* path)
{
    time_t now = time(NULL);
    char time_str[30];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    printf("[%s] %s 请求 | 路径：%s\n", time_str, method, path);
    // log_info("%s %s", method, path);
}

// ===================== 工具函数：URL简单解码（仅处理最常见场景） =====================
// 说明：这里只处理 '+' -> 空格，%xx 可按需扩展
static void simple_url_decode(char* s) {
    if (!s) return;

    for (char* p = s; *p; ++p) {
        if (*p == '+') *p = ' ';
    }
}

// ===================== 工具函数：给ctx生成完整HTTP响应 =====================
// status_line：例如 "HTTP/1.1 200 OK\r\n"
// content_type：例如 "text/html; charset=utf-8"
// body：响应体
// body_len：响应体长度
static void build_response(ClientContext* ctx,
                           const char* status_line,
                           const char* content_type,
                           const char* body,
                           size_t body_len,
                           const char* connection = "close")
{
    if (!ctx) return;

    // 先拼接HTTP响应头
    char header[512] = {0};
    int header_len = snprintf(header, sizeof(header),
                              "%s"
                              "Content-Type: %s\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: %s\r\n"
                              "\r\n",
                              status_line,
                              content_type,
                              body_len,
                              connection);

    if (header_len < 0) return;

    // 释放旧写缓冲区，重新按实际长度分配
    delete[] ctx->write_buf;
    ctx->write_buf = nullptr;

    ctx->write_len = header_len + (int)body_len;
    ctx->write_buf = new char[ctx->write_len];

    // 复制响应头 + 响应体
    memcpy(ctx->write_buf, header, header_len);
    if (body_len > 0 && body != nullptr) {
        memcpy(ctx->write_buf + header_len, body, body_len);
    }
}

// ===================== 工具函数：从请求中提取POST Body =====================
static char* get_http_body(char* request_buf) {
    if (!request_buf) return nullptr;

    char* body = strstr(request_buf, "\r\n\r\n");
    if (body != nullptr) {
        body += 4;
    }
    return body;
}

// ===================== 工具函数：读取整个文件（二进制） =====================
// 返回值：
// true  -> 成功
// false -> 失败
static bool read_file_all(const char* file_name, char*& out_buf, size_t& out_size) {
    out_buf = nullptr;
    out_size = 0;

    FILE* file = fopen(file_name, "rb");
    if (file == nullptr) {
        return false;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) {
        fclose(file);
        return false;
    }

    out_size = (size_t)size;
    out_buf = new char[out_size > 0 ? out_size : 1];

    if (out_size > 0) {
        size_t read_size = fread(out_buf, 1, out_size, file);
        if (read_size != out_size) {
            fclose(file);
            delete[] out_buf;
            out_buf = nullptr;
            out_size = 0;
            return false;
        }
    }

    fclose(file);
    return true;
}

// ===================== 工具函数：路由到静态文件 =====================
static void map_path_to_file(const char* path, char* file_name, size_t file_name_size, bool& is_404_route) {
    is_404_route = false;
    if (!path || !file_name || file_name_size == 0) return;

    memset(file_name, 0, file_name_size);

    // 先判断是否是带后缀的静态文件（.png/.css/.js等）
    const char* ext = strrchr(path, '.');
    if (ext != NULL && strlen(ext) > 1) {
        // 去掉路径开头的 /
        if (path[0] == '/') {
            snprintf(file_name, file_name_size, "%s", path + 1);
        } else {
            snprintf(file_name, file_name_size, "%s", path);
        }
        return;
    }

    // 路由匹配：路径对应本地html文件
    if (strcmp(path, "/") == 0 || strcmp(path, "/index") == 0) {
        snprintf(file_name, file_name_size, "%s", "index.html");
    } else if (strcmp(path, "/hello") == 0) {
        snprintf(file_name, file_name_size, "%s", "hello.html");
    } else if (strcmp(path, "/form") == 0) {
        snprintf(file_name, file_name_size, "%s", "form.html");
    } else if (strcmp(path, "/post") == 0) {
        snprintf(file_name, file_name_size, "%s", "form_post.html");
    } else {
        // 未知路由，优先走404.html
        snprintf(file_name, file_name_size, "%s", "404.html");
        is_404_route = true;
    }
}

// 构造函数：创建指定数量的线程
ThreadPool::ThreadPool(int num) : thread_num(num), is_close(false) {
    // 初始化锁和条件变量
    pthread_mutex_init(&mutex, nullptr);
    pthread_cond_init(&cond, nullptr);

    // 创建线程 -> 分离状态（自动释放资源，无僵尸线程）
    for (int i = 0; i < thread_num; ++i) {
        pthread_t tid;
        pthread_create(&tid, nullptr, worker, this);
        pthread_detach(tid);
    }
}

// 析构函数：关闭线程池，释放资源
ThreadPool::~ThreadPool() {
    pthread_mutex_lock(&mutex);
    is_close = true;
    pthread_mutex_unlock(&mutex);

    pthread_cond_broadcast(&cond);  // 唤醒所有线程

    // 注意：这里使用了detach线程，因此无法join等待
    // 一般在整个进程退出时问题不大；若要更严格回收，应改为非detach + join
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
}

// 添加任务
void ThreadPool::addTask(Task task) {
    pthread_mutex_lock(&mutex);      // 加锁
    task_queue.push(task);           // 加入队列
    pthread_mutex_unlock(&mutex);    // 解锁
    pthread_cond_signal(&cond);      // 唤醒一个线程处理任务
}

// 设置任务完成回调
void ThreadPool::setFinishCallback(function<void(ClientContext*)> cb) {
    finish_callback = cb;
}

// 工作线程
void* ThreadPool::worker(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;

    while (true) {
        // 加锁访问任务队列
        pthread_mutex_lock(&pool->mutex);

        // 无任务 && 线程池未关闭 -> 阻塞等待
        while (pool->task_queue.empty() && !pool->is_close) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }

        // 线程池关闭 -> 退出线程
        if (pool->is_close) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        // 取出一个任务
        Task task = pool->task_queue.front();
        pool->task_queue.pop();
        pthread_mutex_unlock(&pool->mutex);

        // ==================== 核心 ====================
        // 执行业务逻辑：这里只处理已经读入ctx->read_buf的数据
        handle_client(task.ctx);

        // 业务处理完成后，通知Reactor把该连接改成EPOLLOUT
        if (pool->finish_callback && task.ctx != nullptr) {
            pool->finish_callback(task.ctx);
        }
        // ==============================================
    }

    return nullptr;
}

// ===================== 业务逻辑函数 =====================
// 说明：
// 1. 这里不再read(client_fd)
// 2. 这里只解析ctx->read_buf
// 3. 生成完整HTTP响应写入ctx->write_buf
void handle_client(ClientContext* ctx)
{
    if (ctx == nullptr) return;
    if (ctx->read_buf == nullptr || ctx->read_len <= 0) return;

    char* buffer = ctx->read_buf;

    // 统一解析请求行
    char method[16] = {0};
    char path[256] = {0};
    sscanf(buffer, "%15s %255s HTTP", method, path);

    // 解析代码的后面，添加：
    // 判断是否长连接（HTTP/1.1 默认开启 keep-alive）
    ctx->keep_alive = false;
    // 兼容标准 Connection 请求头
    if (strstr(ctx->read_buf, "Connection: keep-alive") != nullptr 
        || strstr(ctx->read_buf, "Connection:Keep-Alive") != nullptr) {
        ctx->keep_alive = true;
    }
    // HTTP/1.1 默认长连接（可选增强）
    if (strstr(ctx->read_buf, "HTTP/1.1") != nullptr) {
        ctx->keep_alive = true;
    }

    if (strlen(method) == 0 || strlen(path) == 0) {
        const char* body = "<h1>400 Bad Request</h1>";
        build_response(ctx,
                       "HTTP/1.1 400 Bad Request\r\n",
                       "text/html; charset=utf-8",
                       body,
                       strlen(body),
                        ctx->keep_alive ? "keep-alive" : "close");
        return;
    }

    // 打印日志
    log_request(method, path);

    // ===================== 屏蔽浏览器图标请求 =====================
    if (strcmp(path, "/favicon.ico") == 0) {
        const char* body = "";
        build_response(ctx,
                       "HTTP/1.1 204 No Content\r\n",
                       "text/plain; charset=utf-8",
                       body,
                       0,
                        ctx->keep_alive ? "keep-alive" : "close");
        return;
    }

    // ===================== 处理POST提交 =====================
    if (strcmp(method, "POST") == 0 && strcmp(path, "/submit") == 0)
    {
        // 解析POST请求体（获取表单数据）
        char* body = get_http_body(buffer);

        // 解析name参数
        char name[64] = {0};
        if (body != nullptr) {
            sscanf(body, "name=%63s", name);
            simple_url_decode(name);
        }

        char html[1024] = {0};
        snprintf(html, sizeof(html),
                 "<h1>✅ POST表单提交成功!</h1>"
                 "<h2>欢迎你：%s</h2>"
                 "<a href='/post'>返回POST表单页面</a>",
                 strlen(name) > 0 ? name : "匿名用户");

        build_response(ctx,
                       "HTTP/1.1 200 OK\r\n",
                       "text/html; charset=utf-8",
                       html,
                       strlen(html),
                    ctx->keep_alive ? "keep-alive" : "close");
        return;
    }

    // ===================== 处理GET带参数请求 =====================
    if (strstr(path, "?") != NULL)
    {
        // 查找路径中的 ? 参数分隔符
        char* param_ptr = strstr(path, "?");
        if (param_ptr != NULL)
        {
            // 1. 截断路径，只保留前面的路由
            *param_ptr = '\0';

            // 2. 后面的部分是参数（例如 name=xxx）
            char* params = param_ptr + 1;

            // 解析name参数
            char name[64] = {0};
            sscanf(params, "name=%63s", name);
            simple_url_decode(name);

            // 动态生成响应：根据用户传入的名字返回内容
            char html[1024] = {0};
            snprintf(html, sizeof(html),
                     "<h1>🎉 动态交互成功！</h1>"
                     "<h2>你好，%s!</h2>"
                     "<a href='/form'>返回表单页面</a>"
                     "<p>这是服务器为你定制的内容</p>",
                     strlen(name) > 0 ? name : "匿名用户");

            build_response(ctx,
                           "HTTP/1.1 200 OK\r\n",
                           "text/html; charset=utf-8",
                           html,
                           strlen(html),
                            ctx->keep_alive ? "keep-alive" : "close");
            return;
        }
    }

    // ===================== 处理静态文件/页面 =====================
    {
        char file_name[256] = {0};
        bool is_404_route = false;
        map_path_to_file(path, file_name, sizeof(file_name), is_404_route);

        char* file_content = nullptr;
        size_t file_size = 0;

        if (read_file_all(file_name, file_content, file_size))
        {
            cout << "成功读取文件：" << file_name << endl;

            const char* content_type = get_content_type(file_name);

            if (is_404_route) {
                build_response(ctx,
                               "HTTP/1.1 404 Not Found\r\n",
                               content_type,
                               file_content,
                               file_size,
                            ctx->keep_alive ? "keep-alive" : "close");
            } else {
                build_response(ctx,
                               "HTTP/1.1 200 OK\r\n",
                               content_type,
                               file_content,
                               file_size,
                            ctx->keep_alive ? "keep-alive" : "close");
            }

            delete[] file_content;
            return;
        }
        else
        {
            cout << "读取文件失败：" << file_name << endl;

            const char* body = "<h1>404 Not Found</h1>";
            build_response(ctx,
                           "HTTP/1.1 404 Not Found\r\n",
                           "text/html; charset=utf-8",
                           body,
                           strlen(body),
                           ctx->keep_alive ? "keep-alive" : "close");
            return;
        }
    }
}