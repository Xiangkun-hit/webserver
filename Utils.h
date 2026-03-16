// Utils.h
#ifndef UTILS_H
#define UTILS_H

// 包含必须的头文件
#include <time.h>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>

// ==================== 公共函数声明 ====================
// 日志打印函数（声明，实现还在你的主文件里）
void log_request(const char* method, const char* path);

// 文件类型判断函数（声明）
const char* get_content_type(const char* file_name);

// 非阻塞函数（声明）
int setnonblocking(int fd);

#endif