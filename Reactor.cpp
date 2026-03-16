#include "Reactor.h"
#include "ThreadPool.h"

// 构造函数：创建epoll + 初始化事件数组
Reactor::Reactor(int max_event) : max_event(max_event), pool(nullptr) {
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    events = new epoll_event[max_event];
    bzero(events, sizeof(epoll_event) * max_event);
}

// 析构函数：释放资源
Reactor::~Reactor() {
    close(epoll_fd);
    delete[] events;
}

// 绑定线程池
void Reactor::bindThreadPool(ThreadPool* tp) {
    pool = tp;
    // 设置线程池任务完成回调：
    // 业务线程处理完后，通知Reactor把该连接切换为写事件
    if (pool) {
        pool->setFinishCallback([this](ClientContext* ctx) {
            this->modEvent(ctx, EPOLLOUT);
        });
    }
}

// 设置fd为非阻塞
void Reactor::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
}

// 添加监听socket事件（统一使用data.ptr）
// 注意：监听fd也必须使用上下文，否则run()里无法统一按data.ptr处理
void Reactor::addListenEvent(int listen_fd, bool is_et) {
    ClientContext* ctx = new ClientContext();
    ctx->fd = listen_fd;
    ctx->is_listen = true;
    ctx->is_et = is_et;

    epoll_event ev;
    bzero(&ev, sizeof(ev));
    ev.data.ptr = ctx;
    ev.events = EPOLLIN;

    // 开启ET模式
    if (is_et) ev.events |= ET_EVENT;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev);
    setnonblocking(listen_fd);
}

// 添加客户端socket事件（初始化上下文，监听读）
// 这里统一给客户端连接绑定ClientContext，后续读写都依赖这个上下文
void Reactor::addProactorEvent(int fd, bool is_et) {
    ClientContext* ctx = new ClientContext();
    ctx->fd = fd;
    ctx->is_listen = false;
    ctx->is_et = is_et;
    ctx->read_buf = new char[4096]();   // 4K读缓存
    ctx->read_len = 0;
    ctx->write_buf = new char[4096]();  // 4K写缓存
    ctx->write_len = 0;

    epoll_event ev;
    bzero(&ev, sizeof(ev));
    ev.data.ptr = ctx;
    ev.events = EPOLLIN | EPOLLRDHUP;   // 监听可读 + 对端半关闭

    // 开启ET模式
    if (is_et) ev.events |= ET_EVENT;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    setnonblocking(fd); // 确保客户端socket非阻塞
}

// 修改fd监听事件
// 典型用途：读完后改成监听写，写完后再改回监听读
void Reactor::modEvent(ClientContext* ctx, int event_type) {
    if (!ctx) return;

    epoll_event ev;
    bzero(&ev, sizeof(ev));
    ev.data.ptr = ctx;
    ev.events = event_type | EPOLLRDHUP;

    // 保持原先ET/LT模式不变
    if (ctx->is_et) ev.events |= ET_EVENT;

    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ctx->fd, &ev);
}

// 删除事件
void Reactor::delEvent(int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
}

// 关闭并释放一个连接上下文
// 注意：监听socket上下文一般不在这里释放，主要用于客户端连接
void Reactor::closeContext(ClientContext* ctx) {
    if (!ctx) return;

    delEvent(ctx->fd);
    close(ctx->fd);
    delete ctx;
}

// 处理新客户端连接
// 关键修正：在ET模式下，必须循环accept直到返回EAGAIN/EWOULDBLOCK
void Reactor::handleAccept(int listen_fd) {
    while (true) {
        sockaddr_in addr;
        socklen_t len = sizeof(addr);

        int client_fd = accept(listen_fd, (sockaddr*)&addr, &len);

        if (client_fd < 0) {
            // ET模式下，accept到这里表示当前连接已经取完
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                perror("accept error");
                break;
            }
        }

        cout << "新客户端连接：" << client_fd << endl;

        setnonblocking(client_fd); // 非阻塞

        // 加入Proactor事件（ET模式）
        addProactorEvent(client_fd, true);
    }
}

// 处理读事件（主线程循环read，读取所有数据）
// 当前版本：主线程负责把数据读完，然后直接构造一个固定响应，切换到写事件
void Reactor::handleRead(ClientContext* ctx) {
    if (!ctx) return;

    int fd = ctx->fd;
    char* buf = ctx->read_buf;
    int& len = ctx->read_len;

    // 每次进入读事件，先重置读长度
    // 如果你后续要支持粘包/半包协议，这里需要改成更完整的状态机
    len = 0;
    if (buf) buf[0] = '\0';

    // ET模式：循环read直到无数据
    while (true) {
        // 避免缓冲区溢出
        if (len >= 4095) {
            break;
        }

        int n = read(fd, buf + len, 4095 - len);
        if (n > 0) {
            len += n;
            buf[len] = '\0';
        } else if (n == 0) { // 客户端正常断开
            cout << "客户端断开：" << fd << endl;
            closeContext(ctx);
            return;
        } else { // 出错或数据读完
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞socket在ET模式下读到这里，说明这轮数据已读完
                break;
            } else {
                perror("read error");
                closeContext(ctx);
                return;
            }
        }
    }

    // 如果什么都没读到，直接返回继续监听读事件
    if (len <= 0) {
        modEvent(ctx, EPOLLIN);
        return;
    }

    // 输出收到的数据（调试用）
    cout << "收到客户端[" << fd << "]数据：" << endl;
    cout << buf << endl;

    // 数据读取完成 → 交给线程池处理业务
    if (pool) {
        pool->addTask({ctx});
    } else {
        // 没有线程池时，直接返回一个兜底响应
        const char* body = "<h1>500 Internal Server Error</h1>";
        delete[] ctx->write_buf;
        ctx->write_buf = nullptr;

        char header[256] = {0};
        int header_len = snprintf(header, sizeof(header),
                                "HTTP/1.1 500 Internal Server Error\r\n"
                                "Content-Type: text/html; charset=utf-8\r\n"
                                "Content-Length: %zu\r\n"
                                "Connection: close\r\n"
                                "\r\n",
                                strlen(body));

        ctx->write_len = header_len + strlen(body);
        ctx->write_buf = new char[ctx->write_len];
        memcpy(ctx->write_buf, header, header_len);
        memcpy(ctx->write_buf + header_len, body, strlen(body));

        modEvent(ctx, EPOLLOUT);
    }
}

// 处理写事件（主线程send，发送响应）
void Reactor::handleWrite(ClientContext* ctx) {
    if (!ctx) return;

    int fd = ctx->fd;
    char* buf = ctx->write_buf;
    int& len = ctx->write_len;

    if (len <= 0) {
        // 没有待发数据，则继续监听读
        modEvent(ctx, EPOLLIN);
        return;
    }

    // 循环send直到发完或socket暂时不可写
    while (len > 0) {
        int n = send(fd, buf, len, 0);
        if (n > 0) {
            len -= n;
            memmove(buf, buf + n, len);
        } else if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // socket发送缓冲区满了，等待下次EPOLLOUT继续发送
                break;
            } else {
                perror("send error");
                closeContext(ctx);
                return;
            }
        }
    }

    // 发送完成 → 清理上下文并关闭连接
    // 当前演示版本按短连接处理：响应发完后直接关闭
    if (len == 0) {
        cout << "响应发送完成：" << fd << endl;

        // 1. 释放写缓冲区（固定操作）
        delete[] ctx->write_buf;
        ctx->write_buf = nullptr;
        ctx->write_len = 0;
        
        // 2. ✅ 长连接：重置读缓冲区，重新监听读事件（复用连接）
        if (ctx->keep_alive) {
            cout << "长连接复用：" << fd << endl;
            ctx->read_len = 0;          // 重置读长度
            memset(ctx->read_buf, 0, 4096); // 清空读缓冲区
            modEvent(ctx, EPOLLIN);    // 切换回监听客户端请求
        }
        // 3. ❌ 短连接：关闭并释放连接（原有逻辑）
        else {
            cout << "短连接关闭：" << fd << endl;
            closeContext(ctx);
        }
    } else {
        // 如果还没发完，则继续监听写事件
        modEvent(ctx, EPOLLOUT);
    }
}

// 反应堆核心事件循环
void Reactor::run() {
    while (true) {
        // 等待事件（阻塞）
        int n = epoll_wait(epoll_fd, events, max_event, -1);

        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait error");
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int event = events[i].events;
            ClientContext* ctx = (ClientContext*)events[i].data.ptr;

            if (!ctx) continue;

            // 监听socket：处理新连接
            if (ctx->is_listen) {
                handleAccept(ctx->fd);
                continue;
            }

            // 连接异常 / 对端关闭
            if (event & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                cout << "客户端异常断开：" << ctx->fd << endl;
                closeContext(ctx);
                continue;
            }

            // 读事件：主线程读取数据
            if (event & EPOLLIN) {
                handleRead(ctx);
            }
            // 写事件：主线程发送数据
            else if (event & EPOLLOUT) {
                handleWrite(ctx);
            }
        }
    }
}