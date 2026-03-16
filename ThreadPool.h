#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <queue>
#include <pthread.h>
#include <functional>
#include "ClientContext.h"
using namespace std;
#include "HttpParser.h"
#include "Log.h"

// ===================== 任务结构 =====================
// 线程池中的每个任务，携带一个客户端上下文
struct Task {
    ClientContext* ctx;
};

// ===================== 线程池类 =====================
class ThreadPool {
private:
    int thread_num;                  // 线程数量
    bool is_close;                   // 线程池是否关闭
    queue<Task> task_queue;          // 任务队列

    pthread_mutex_t mutex;           // 互斥锁
    pthread_cond_t cond;             // 条件变量

    // 任务处理完成后的回调
    // 典型用途：业务线程处理完请求后，通知Reactor把该fd切换为EPOLLOUT
    std::function<void(ClientContext*)> finish_callback;

public:
    // 构造函数：创建指定数量的线程
    ThreadPool(int num = 8);

    // 析构函数：关闭线程池，释放资源
    ~ThreadPool();

    // 添加任务
    void addTask(Task task);

    // 设置任务完成回调
    void setFinishCallback(std::function<void(ClientContext*)> cb);

    // 工作线程函数
    static void* worker(void* arg);
};

// ===================== 业务逻辑函数 =====================
// 作用：处理已经读入ctx->read_buf中的HTTP请求，生成响应到ctx->write_buf
void handle_client(ClientContext* ctx);

#endif