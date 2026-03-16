#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <queue>
// 网络相关头文件
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include "Utils.h"
#include <sys/epoll.h>  // epoll 核心头文件

// 任务结构体
struct Task {
    int client_fd;
};

// 线程池类
class ThreadPool {
private:
    std::queue<Task> task_queue;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int thread_num;
    bool is_close;

public:
    ThreadPool(int num);
    ~ThreadPool();
    void addTask(Task task);

private:
    static void* worker(void* arg);
};



// 声明业务处理函数
void handle_client(int client_fd);

void log_request(const char* method, const char* path);

#endif