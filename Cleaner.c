/*
 * Professional JS Cleaner V3 (Comments + Console Logs)
 * 功能：
 * 1. 移除 // 和 / * ... * / 注释
 * 2. 移除 console.log/error/info/debug/warn 调用 (替换为 void 0 以保持语法结构)
 * 3. 完美保护字符串、模板字符串、正则表达式
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// 状态机定义
typedef enum {
    STATE_CODE,
    STATE_STRING_SINGLE,   // '...'
    STATE_STRING_DOUBLE,   // "..."
    STATE_STRING_TEMPLATE, // `...`
    STATE_COMMENT_SINGLE,  // //...
    STATE_COMMENT_MULTI    // /*...*/
} State;

// 读取整个文件到内存
char* read_file_to_memory(const char* filename, long* size) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    rewind(f);
    char* buffer = (char*)malloc(*size + 1);
    if (buffer) {
        fread(buffer, 1, *size, f);
        buffer[*size] = '\0';
    }
    fclose(f);
    return buffer;
}

// 检查是否是 console 调用
// 返回值：0=否，1=是 (并设置 cmd_len 为 console.xxx 到 左括号前的长度)
int is_console_call(const char* text, long i, int* cmd_len) {
    if (strncmp(&text[i], "console.", 8) != 0) return 0;
    
    const char* methods[] = {"log", "warn", "error", "info", "debug", NULL};
    int offset = 8;
    int found = 0;
    
    for (int k = 0; methods[k]; k++) {
        int mlen = strlen(methods[k]);
        if (strncmp(&text[i + offset], methods[k], mlen) == 0) {
            // 确保方法名后不是字母（防止 console.logMyName）
            char next_char = text[i + offset + mlen];
            if (!isalnum(next_char) && next_char != '_') {
                offset += mlen;
                found = 1;
                break;
            }
        }
    }
    if (!found) return 0;

    // 向后查找左括号 (允许空格)
    int j = i + offset;
    while (text[j] && isspace(text[j])) j++;
    
    if (text[j] == '(') {
        *cmd_len = j - i; // 不包含左括号的长度
        return 1;
    }
    return 0;
}

void process_file(const char* filename) {
    printf("Processing: %s ...\n", filename);

    // 1. 备份原文件
    char backup_name[1024];
    snprintf(backup_name, sizeof(backup_name), "%s.bak", filename);
    FILE* src_chk = fopen(filename, "rb");
    if (!src_chk) { printf("  [Error] File not found.\n"); return; }
    FILE* bak = fopen(backup_name, "wb");
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src_chk)) > 0) fwrite(buf, 1, n, bak);
    fclose(src_chk);
    fclose(bak);

    // 2. 读取文件到内存
    long fsize;
    char* text = read_file_to_memory(filename, &fsize);
    if (!text) { printf("  [Error] Cannot read file.\n"); return; }

    // 3. 打开目标文件写入
    FILE* dst = fopen(filename, "wb");
    
    State state = STATE_CODE;
    int skip_console = 0; // 标记是否正在跳过 console 语句
    int console_paren_depth = 0; // console 括号深度
    
    for (long i = 0; i < fsize; i++) {
        char c = text[i];

        // --- 优先处理：正在跳过 Console 日志 ---
        if (skip_console) {
            // 即使在跳过日志，也要识别字符串，防止日志参数里包含 ')' 导致误判结束
            // 例如: console.log("Text with ) inside")
            if (state == STATE_CODE) {
                if (c == '\'') state = STATE_STRING_SINGLE;
                else if (c == '"') state = STATE_STRING_DOUBLE;
                else if (c == '`') state = STATE_STRING_TEMPLATE;
                else if (c == '(') console_paren_depth++;
                else if (c == ')') {
                    console_paren_depth--;
                    if (console_paren_depth == 0) {
                        // console 结束
                        skip_console = 0;
                    }
                }
            } else {
                // 在字符串内，只检查结束引号
                 if (c == '\\') { i++; } // 跳过转义
                 else if (state == STATE_STRING_SINGLE && c == '\'') state = STATE_CODE;
                 else if (state == STATE_STRING_DOUBLE && c == '"') state = STATE_CODE;
                 else if (state == STATE_STRING_TEMPLATE && c == '`') state = STATE_CODE;
            }
            continue; // 不写入任何内容 (即删除)
        }

        // --- 正常状态处理 ---
        
        // 1. 字符串保护
        if (state == STATE_STRING_SINGLE || state == STATE_STRING_DOUBLE || state == STATE_STRING_TEMPLATE) {
            fputc(c, dst);
            if (c == '\\') {
                if (i + 1 < fsize) fputc(text[++i], dst);
            } else {
                if (state == STATE_STRING_SINGLE && c == '\'') state = STATE_CODE;
                else if (state == STATE_STRING_DOUBLE && c == '"') state = STATE_CODE;
                else if (state == STATE_STRING_TEMPLATE && c == '`') state = STATE_CODE;
            }
        }
        // 2. 单行注释
        else if (state == STATE_COMMENT_SINGLE) {
            if (c == '\n') {
                fputc(c, dst);
                state = STATE_CODE;
            }
        }
        // 3. 多行注释
        else if (state == STATE_COMMENT_MULTI) {
            if (c == '*' && i + 1 < fsize && text[i+1] == '/') {
                i++; // 跳过 /
                fputc(' ', dst); // 替换为空格
                state = STATE_CODE;
            } else if (c == '\n') {
                fputc(c, dst);
            }
        }
        // 4. 代码区
        else {
            // 检查是否开始字符串
            if (c == '\'') { state = STATE_STRING_SINGLE; fputc(c, dst); }
            else if (c == '"') { state = STATE_STRING_DOUBLE; fputc(c, dst); }
            else if (c == '`') { state = STATE_STRING_TEMPLATE; fputc(c, dst); }
            // 检查是否开始注释
            else if (c == '/' && i + 1 < fsize && text[i+1] == '/') {
                state = STATE_COMMENT_SINGLE;
                i++;
            }
            else if (c == '/' && i + 1 < fsize && text[i+1] == '*') {
                state = STATE_COMMENT_MULTI;
                i++;
            }
            // 检查是否是 console.xxx
            else {
                int cmd_len = 0;
                if (is_console_call(text, i, &cmd_len)) {
                    // 发现 console.log 等
                    skip_console = 1;
                    console_paren_depth = 0;
                    
                    // 替换为 void 0 以保持语法合法性
                    // 例如: const x = () => console.log(1) 变成 const x = () => void 0
                    fprintf(dst, "void 0");
                    
                    // 跳过 "console.log" 这部分字符，循环继续会处理后面的 '('
                    i += cmd_len - 1; 
                } else {
                    fputc(c, dst);
                }
            }
        }
    }

    fclose(dst);
    free(text);
    printf("  [Success] Cleaned comments and debug logs.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: Drag files onto this exe\n");
        getchar();
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        process_file(argv[i]);
    }
    return 0;
}
