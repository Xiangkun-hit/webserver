#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <cstdio>
#include <cstring>
#include <string>
using namespace std;

// HTTP 解析状态（有限状态机核心）
enum HttpParseStatus {
    PARSE_REQUEST_LINE,     // 解析请求行
    PARSE_HEADER_KEY,       // 解析请求头key
    PARSE_HEADER_VALUE,      // 解析请求头value
    PARSE_EMPTY_LINE,       // 解析空行
    PARSE_REQUEST_BODY,     // 解析请求体
    PARSE_DONE,             // 解析完成
    PARSE_ERROR             // 解析失败
};

// HTTP 解析结果（存储所有解析后的数据）
struct HttpResult {
    // 请求行
    string method;
    string path;
    string version;

    // 请求头
    string host;
    string connection;
    int content_length;

    // 请求体
    string body;

    HttpResult() {
        content_length = 0;
    }
};

// HTTP 状态机解析类
class HttpParser {
private:
    HttpParseStatus status;
    HttpResult result;
    string temp_key;

public:
    HttpParser();
    // 核心解析函数
    HttpResult parse(const char* buffer, int len);
    // 重置解析器
    void reset();
};

#endif