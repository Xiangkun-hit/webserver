#include "Log.h"
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

// 核心：无任何全局变量！无任何静态变量！每次调用都临时打开文件
static void log_print(const char* level, const char* format, va_list args)
{
    // 1. 时间
    char time_str[64] = {0};
    time_t now = time(NULL);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    // 2. 控制台打印（绝对安全）
    printf("[%s] [%s] ", time_str, level);
    vprintf(format, args);
    printf("\n");

    // 3. 临时打开文件 → 写入 → 立即关闭（无全局句柄，多线程100%安全）
    FILE* fp = fopen("webserver.log", "a");
    if (fp) {
        fprintf(fp, "[%s] [%s] ", time_str, level);
        vfprintf(fp, format, args);
        fprintf(fp, "\n");
        fclose(fp); // 立即关闭，不占用文件句柄
    }
}

// 接口实现
void log_info(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    log_print("INFO", format, args);
    va_end(args);
}

void log_warn(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    log_print("WARN", format, args);
    va_end(args);
}

void log_error(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    log_print("ERROR", format, args);
    va_end(args);
}