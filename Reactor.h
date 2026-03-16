#ifndef REACTOR_H
#define REACTOR_H

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <iostream>
using namespace std;
#include "ClientContext.h"

// ET模式宏（如果你原来已有定义，可删除这里）
#ifndef ET_EVENT
#define ET_EVENT EPOLLET
#endif

class ThreadPool;  // 前向声明


// ===================== Reactor类 =====================
class Reactor {
private:
    int epoll_fd;               // epoll实例fd
    epoll_event* events;        // epoll事件数组
    int max_event;              // 最大事件数
    ThreadPool* pool;           // 线程池（当前版本先保留接口）

public:
    // 构造函数：创建epoll + 初始化事件数组
    Reactor(int max_event = 1024);

    // 析构函数：释放资源
    ~Reactor();

    // 绑定线程池
    void bindThreadPool(ThreadPool* tp);

    // 设置fd为非阻塞
    static void setnonblocking(int fd);

    // 添加监听socket事件（统一使用data.ptr）
    void addListenEvent(int listen_fd, bool is_et);

    // 添加客户端socket事件（初始化上下文，监听读）
    void addProactorEvent(int fd, bool is_et);

    // 修改fd监听事件（EPOLL_CTL_MOD）
    void modEvent(ClientContext* ctx, int event_type);

    // 删除事件
    void delEvent(int fd);

    // 处理新客户端连接
    void handleAccept(int listen_fd);

    // 处理读事件（主线程循环read，读取所有数据）
    void handleRead(ClientContext* ctx);

    // 处理写事件（主线程send，发送响应）
    void handleWrite(ClientContext* ctx);

    // 关闭并释放一个连接上下文
    void closeContext(ClientContext* ctx);

    // 反应堆核心事件循环
    void run();
};

#endif