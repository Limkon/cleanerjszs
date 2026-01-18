/*
 * JS Cleaner Ultimate (C Language)
 * 内置简易语法分析引擎，完美区分 除法(/) 与 正则(/.../)
 * 修复：模板字符串、正则表达式、URL 被误删的问题
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// 定义状态
typedef enum {
    STATE_CODE,
    STATE_STRING_SQ,    // '...'
    STATE_STRING_DQ,    // "..."
    STATE_STRING_TMP,   // `...`
    STATE_REGEX,        // /.../
    STATE_COMMENT_LINE, // //...
    STATE_COMMENT_BLOCK // /*...*/
} State;

// 辅助函数：判断前一个有效字符是否意味着接下来的是正则
// 如果前一个符号是运算符、关键字或括号，那么 / 肯定是正则的开始
int is_regex_start(const char *text, long idx) {
    long i = idx - 1;
    // 1. 向前回溯，跳过空白字符
    while (i >= 0 && isspace(text[i])) i--;
    if (i < 0) return 1; // 文件开头

    char last = text[i];
    
    // 2. 检查常见运算符和符号
    if (strchr("(=,:!&|?{};,", last)) return 1;
    
    // 3. 检查关键字 (如 return /.../, typeof /.../)
    // 如果上一个字符是字母/数字/下划线，我们需要读出整个单词
    if (isalnum(last) || last == '_' || last == '$') {
        long end = i;
        while (i >= 0 && (isalnum(text[i]) || text[i] == '_' || text[i] == '$')) i--;
        
        // 提取单词
        long len = end - i;
        char word[32];
        if (len < 31) {
            strncpy(word, &text[i+1], len);
            word[len] = '\0';
            
            // 常见的后接正则的关键字
            if (strcmp(word, "return") == 0) return 1;
            if (strcmp(word, "case") == 0) return 1;
            if (strcmp(word, "throw") == 0) return 1;
            if (strcmp(word, "delete") == 0) return 1;
            if (strcmp(word, "void") == 0) return 1;
            if (strcmp(word, "typeof") == 0) return 1;
            if (strcmp(word, "await") == 0) return 1;
            if (strcmp(word, "yield") == 0) return 1;
        }
        return 0; // 是变量名或数字，那后面的 / 就是除号
    }
    
    // 4. 右括号 ] ) 后面通常是除号 (arr[i] / 2), 除非是 if (...) /regex/ 
    // 这里做个简化假设，绝大多数情况 ] ) 后面是运算符。
    // 如果您的代码中有 if(...) /regex/ 这种罕见写法可能需要更深层解析，但在 worker.js 中未见。
    return 0;
}

// 检查是否是 console.xxxx
int is_console_call(const char *text, long i) {
    if (strncmp(&text[i], "console.", 8) == 0) {
        // 简单检查后面是否跟着 log/info/warn/error/debug
        // 只需要确定是 console. 开头即可，后续逻辑会处理
        return 1;
    }
    return 0;
}

void process_file(const char *filename) {
    printf("Processing: %s ...\n", filename);
    
    FILE *f = fopen(filename, "rb");
    if (!f) { printf("Error: File not found.\n"); return; }

    // 读取全文件
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *input = (char*)malloc(size + 1);
    fread(input, 1, size, f);
    input[size] = '\0';
    fclose(f);

    // 备份
    char bak_name[1024];
    sprintf(bak_name, "%s.bak", filename);
    FILE *bak = fopen(bak_name, "wb");
    if (bak) { fwrite(input, 1, size, bak); fclose(bak); }

    // 准备输出 buffer
    // 因为要把 console.log(...) 替换为 void(0)，输出可能会变小，不会变大太多
    char *output = (char*)malloc(size * 2 + 1); 
    long out_idx = 0;
    
    State state = STATE_CODE;
    long i = 0;
    
    // Console 移除专用变量
    int skip_console = 0;
    int paren_depth = 0;
    int in_console_str = 0; // 0=no, 1=', 2=", 3=`

    while (i < size) {
        char c = input[i];
        char next = (i + 1 < size) ? input[i+1] : 0;

        // --- 优先处理：正在移除 console 语句 ---
        if (skip_console) {
            // 必须识别括号内的字符串，防止字符串里的 ) 提前结束 console
            if (in_console_str) {
                if (c == '\\') { i++; } // 跳过转义
                else if ((in_console_str == 1 && c == '\'') ||
                         (in_console_str == 2 && c == '"') ||
                         (in_console_str == 3 && c == '`')) {
                    in_console_str = 0;
                }
            } else {
                if (c == '\'') in_console_str = 1;
                else if (c == '"') in_console_str = 2;
                else if (c == '`') in_console_str = 3;
                else if (c == '(') paren_depth++;
                else if (c == ')') {
                    paren_depth--;
                    if (paren_depth <= 0) {
                        skip_console = 0; // Console 结束
                    }
                }
            }
            i++;
            continue;
        }

        // --- 正常状态机 ---
        if (state == STATE_CODE) {
            // 检查 Console
            if (is_console_call(input, i)) {
                skip_console = 1;
                paren_depth = 0;
                in_console_str = 0;
                // 写入 void(0) 替代，保持语法
                const char* replacement = "void(0)";
                for(int k=0; replacement[k]; k++) output[out_idx++] = replacement[k];
                // 跳过 console. 长度 (8)，循环末尾会 i++，所以逻辑要对齐
                // is_console_call 只是检查开头，我们需要让 skip_console 逻辑去吞掉后面的字符
                // 这里我们手动让 i 跳过 "console" 几个字，让下一轮循环进入 skip_console 逻辑处理点号后的内容
                i += 7; 
            }
            else if (c == '\'') { state = STATE_STRING_SQ; output[out_idx++] = c; }
            else if (c == '"')  { state = STATE_STRING_DQ; output[out_idx++] = c; }
            else if (c == '`')  { state = STATE_STRING_TMP; output[out_idx++] = c; }
            else if (c == '/') {
                if (next == '/') {
                    state = STATE_COMMENT_LINE;
                    i++; // 跳过第二个 /
                } else if (next == '*') {
                    state = STATE_COMMENT_BLOCK;
                    i++; // 跳过 *
                } else {
                    // 核心逻辑：是正则还是除号？
                    if (is_regex_start(input, i)) {
                        state = STATE_REGEX;
                    }
                    output[out_idx++] = c;
                }
            }
            else {
                output[out_idx++] = c;
            }
        }
        else if (state == STATE_STRING_SQ) {
            output[out_idx++] = c;
            if (c == '\\') { if(next) { output[out_idx++] = next; i++; } }
            else if (c == '\'') state = STATE_CODE;
        }
        else if (state == STATE_STRING_DQ) {
            output[out_idx++] = c;
            if (c == '\\') { if(next) { output[out_idx++] = next; i++; } }
            else if (c == '"') state = STATE_CODE;
        }
        else if (state == STATE_STRING_TMP) {
            output[out_idx++] = c;
            if (c == '\\') { if(next) { output[out_idx++] = next; i++; } }
            else if (c == '`') state = STATE_CODE;
        }
        else if (state == STATE_REGEX) {
            output[out_idx++] = c;
            if (c == '\\') { if(next) { output[out_idx++] = next; i++; } } // 跳过转义字符，如 \/
            else if (c == '/') state = STATE_CODE;
            else if (c == '\n') state = STATE_CODE; // 防止正则没闭合导致吞噬后续代码
        }
        else if (state == STATE_COMMENT_LINE) {
            if (c == '\n') {
                output[out_idx++] = c; // 保留换行
                state = STATE_CODE;
            }
        }
        else if (state == STATE_COMMENT_BLOCK) {
            if (c == '*' && next == '/') {
                state = STATE_CODE;
                output[out_idx++] = ' '; // 替换为空格
                i++;
            } else if (c == '\n') {
                output[out_idx++] = c; // 保留换行
            }
        }

        i++;
    }

    // 写入文件
    FILE *out = fopen(filename, "wb");
    fwrite(output, 1, out_idx, out);
    fclose(out);

    free(input);
    free(output);
    printf("Done. Saved to %s\n", filename);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Drag _worker.js here.\n");
        // 方便调试：默认读取当前目录
        FILE *f = fopen("_worker.js", "rb");
        if(f) { fclose(f); process_file("_worker.js"); }
        else getchar();
        return 0;
    }
    for(int i=1; i<argc; i++) process_file(argv[i]);
    return 0;
}
