#include "HttpParser.h"

HttpParser::HttpParser() {
    reset();
}

void HttpParser::reset() {
    status = PARSE_REQUEST_LINE;
    result = HttpResult();
    temp_key.clear();
}

HttpResult HttpParser::parse(const char* buffer, int len) {
    reset();
    int i = 0;

    while (i < len && status != PARSE_DONE && status != PARSE_ERROR) {
        char c = buffer[i];

        switch (status) {
            // 1. 解析请求行：GET /index.html HTTP/1.1
            case PARSE_REQUEST_LINE: {
                char method[16], path[256], version[16];
                sscanf(buffer, "%s %s %s", method, path, version);
                result.method = method;
                result.path = path;
                result.version = version;

                // 跳到请求头解析
                while (i < len && buffer[i] != '\n') i++;
                i++;
                status = PARSE_HEADER_KEY;
                break;
            }

            // 2. 解析请求头Key
            case PARSE_HEADER_KEY: {
                if (c == ':') {
                    status = PARSE_HEADER_VALUE;
                    i++;
                } else if (c == '\r' || c == '\n') {
                    // 空行：请求头结束
                    status = PARSE_EMPTY_LINE;
                } else {
                    temp_key += c;
                }
                i++;
                break;
            }

            // 3. 解析请求头Value
            case PARSE_HEADER_VALUE: {
                if (c == '\r') {
                    i++;
                    continue;
                }
                if (c == '\n') {
                    // 匹配请求头
                    if (temp_key == "Host") result.host = temp_key;
                    if (temp_key == "Connection") result.connection = temp_key;
                    if (temp_key == "Content-Length") {
                        result.content_length = atoi(temp_key.c_str());
                    }
                    temp_key.clear();
                    status = PARSE_HEADER_KEY;
                } else {
                    temp_key += c;
                }
                i++;
                break;
            }

            // 4. 解析空行
            case PARSE_EMPTY_LINE: {
                i++;
                // 有请求体（POST）
                if (result.content_length > 0) {
                    status = PARSE_REQUEST_BODY;
                } else {
                    status = PARSE_DONE;
                }
                break;
            }

            // 5. 解析POST请求体
            case PARSE_REQUEST_BODY: {
                int body_len = result.content_length;
                if (i + body_len <= len) {
                    result.body = string(buffer + i, body_len);
                    i += body_len;
                    status = PARSE_DONE;
                } else {
                    status = PARSE_ERROR;
                }
                break;
            }

            default:
                status = PARSE_ERROR;
                break;
        }
    }

    return result;
}