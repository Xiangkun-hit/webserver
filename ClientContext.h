#ifndef CLIENTCONTEXT_H
#define CLIENTCONTEXT_H

// ===================== 客户端上下文 =====================
// 作用：把一个fd对应的所有状态统一管理，便于跨函数传递
struct ClientContext {
    int fd;                 // 对应的socket fd
    bool is_listen;         // 是否为监听socket
    bool is_et;             // 是否使用ET模式

    char* read_buf;         // 读缓冲区
    int read_len;           // 当前已读取长度

    char* write_buf;        // 写缓冲区
    int write_len;          // 当前待发送长度

    bool keep_alive;

    // 构造函数：初始化成员
    ClientContext()
        : fd(-1),
          is_listen(false),
          is_et(true),
          read_buf(nullptr),
          read_len(0),
          write_buf(nullptr),
          write_len(0) ,
          keep_alive(false) {}

    // 析构函数：释放动态分配内存
    ~ClientContext() {
        delete[] read_buf;
        delete[] write_buf;
    }
};

#endif