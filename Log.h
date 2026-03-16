#ifndef LOG_H
#define LOG_H

// 只有三个函数，无宏、无类、无静态、无全局
void log_info(const char* format, ...);
void log_warn(const char* format, ...);
void log_error(const char* format, ...);

#endif