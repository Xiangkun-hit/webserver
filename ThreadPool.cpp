#include "ThreadPool.h"
#include <iostream>
using namespace std;

// 非阻塞函数实现
int setnonblocking(int fd) {
    // 1. 获取fd当前的状态标记
    int old_flag = fcntl(fd, F_GETFL);
    // 2. 在原有标记上，添加 非阻塞标记 O_NONBLOCK
    int new_flag = old_flag | O_NONBLOCK;
    // 3. 把新标记设置回去
    fcntl(fd, F_SETFL, new_flag);
    // 返回旧标记（可忽略，不影响使用）
    return old_flag;
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

//析构函数：关闭线程池，释放资源析构函数
ThreadPool::~ThreadPool() {
    is_close = true;
    pthread_cond_broadcast(&cond);  // 唤醒所有线程
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
}

// 添加任务
void ThreadPool::addTask(Task task) {
    pthread_mutex_lock(&mutex); //加锁
    task_queue.push(task);   //加入队列
    pthread_mutex_unlock(&mutex);   //解锁
    pthread_cond_signal(&cond);   //唤醒一个线程处理任务
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
        // 执行你原来的业务逻辑！（处理客户端请求）
        handle_client(task.client_fd);
        // ==============================================
    }
    return nullptr;
}



//业务逻辑函数
void handle_client(int client_fd)
{
    char buffer[4096] = {0};
    int len = 0;
    int total_len = 0;

    // ET 模式：循环读，直到无数据（EAGAIN）
    while( (len = read(client_fd, buffer + total_len, sizeof(buffer) - 1 - total_len)) > 0 ){
        total_len += len;
    }
    // len == -1 且 errno == EAGAIN：代表数据读取完毕（正常情况）
    if( len == -1 && errno != EAGAIN && errno != EWOULDBLOCK ){
        perror("read error");
        close(client_fd);
        return;
    }
    

    // ===================== Day4 核心：解析请求路径 =====================
    //=======统一解析请求
    char method[16] = {0};
    char path[100] = {0};
    sscanf(buffer, "%s %s HTTP", method, path);
    std::cout << "浏览器请求路径：" << path << std::endl;
    // ===================== 新增：屏蔽浏览器图标请求 =====================
    if(strcmp(path, "/favicon.ico") == 0 ||strlen(path) == 0) {
        close(client_fd);
        std::cout << "block ico/ 空路径\n";
        return;
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
        send(client_fd, response, strlen(response), 0);
        close(client_fd);
        return;
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
            send(client_fd, response, strlen(response), 0);
            close(client_fd);
            return;
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
        
        send(client_fd, header, strlen(header), 0);

        // 再发送文件内容（二进制数据）
        send(client_fd, file_content, file_size, 0);

        // 释放动态分配的内存（如果是 malloc 的）
        if (file != NULL && file_content != NULL)
        {
            free(file_content);
        }
        close(client_fd);
        return;                         

        // {
        //     // 父进程：继续等待新连接，不卡顿
        //     close(client_fd);
        // }
    }    
}
            