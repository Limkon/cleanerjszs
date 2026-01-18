/*
 * Professional JS Cleaner V6 (Final Stable)
 * 1. 修复：ES6 模板字符串中 https:// 被误删导致的 Unexpected token
 * 2. 修复：console.log("(:)") 参数中含括号导致解析错误
 * 3. 修复：文件句柄冲突导致的“生成空文件”问题
 * 4. 安全：采用“全内存缓冲”模式，先读后写，确保数据绝对安全
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 定义状态机
typedef enum {
    STATE_CODE,             // 正常代码
    STATE_SLASH,            // 读到了 / (待定)
    STATE_STRING_SINGLE,    // '...'
    STATE_STRING_DOUBLE,    // "..."
    STATE_STRING_TEMPLATE,  // `...` (保护 https:// 的关键)
    STATE_COMMENT_SINGLE,   // //...
    STATE_COMMENT_MULTI,    // /*...*/
    STATE_CONSOLE           // 正在移除 console.xxx(...)
} State;

// 判断是否是 console. 开头
int is_console_start(const char *text, long i, long fsize) {
    if (i + 8 > fsize) return 0;
    // 匹配 console.
    if (strncmp(&text[i], "console.", 8) == 0) return 1;
    return 0;
}

void process_file(const char *filename) {
    printf("Processing: %s ...\n", filename);

    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("  [Error] File not found: %s\n", filename);
        return;
    }

    // 1. 安全读取：获取文件大小并读入内存
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);

    char *text = (char*)malloc(fsize + 1);
    if (!text) {
        printf("  [Error] Memory allocation failed.\n");
        fclose(f);
        return;
    }
    // 读取全部内容后立即关闭文件句柄，避免读写冲突
    if (fread(text, 1, fsize, f) != fsize) {
        printf("  [Error] Read error.\n");
        free(text);
        fclose(f);
        return;
    }
    text[fsize] = '\0';
    fclose(f);

    // 2. 创建备份 (写入 .bak)
    char bak_name[1024];
    snprintf(bak_name, sizeof(bak_name), "%s.bak", filename);
    FILE *bak = fopen(bak_name, "wb");
    if (bak) {
        fwrite(text, 1, fsize, bak);
        fclose(bak);
        printf("  [Info] Backup created: %s\n", bak_name);
    } else {
        printf("  [Warning] Failed to create backup file.\n");
    }

    // 3. 处理并写入原文件
    FILE *out = fopen(filename, "wb");
    if (!out) {
        printf("  [Error] Cannot write to %s\n", filename);
        free(text);
        return;
    }

    State state = STATE_CODE;
    int console_paren_depth = 0; // 记录括号深度
    int console_in_string = 0;   // 记录 console 参数里是否在字符串中
    char console_quote_char = 0; // 记录 console 参数里的引号类型

    for (long i = 0; i < fsize; i++) {
        char c = text[i];
        char next = (i + 1 < fsize) ? text[i+1] : 0;

        switch (state) {
            case STATE_CODE:
                // 检查 console.
                if (is_console_start(text, i, fsize)) {
                    state = STATE_CONSOLE;
                    console_paren_depth = 0;
                    console_in_string = 0;
                    fprintf(out, "void(0)"); // 替换为无操作表达式
                    i += 7; // 跳过 "console." (循环末尾i++会跳过e)
                }
                else if (c == '\'') { state = STATE_STRING_SINGLE; fputc(c, out); }
                else if (c == '"')  { state = STATE_STRING_DOUBLE; fputc(c, out); }
                else if (c == '`')  { state = STATE_STRING_TEMPLATE; fputc(c, out); }
                else if (c == '/')  { state = STATE_SLASH; } // 暂不输出
                else { fputc(c, out); }
                break;

            case STATE_SLASH:
                if (c == '/') { state = STATE_COMMENT_SINGLE; }
                else if (c == '*') { state = STATE_COMMENT_MULTI; }
                else {
                    // 不是注释，恢复输出 / 和当前字符
                    fputc('/', out);
                    // 重新检查当前字符 c 的状态
                    state = STATE_CODE;
                    i--; // 回退一步，让 STATE_CODE 重新处理这个字符
                }
                break;

            case STATE_COMMENT_SINGLE:
                if (c == '\n') {
                    fputc(c, out); // 保留换行
                    state = STATE_CODE;
                }
                break;

            case STATE_COMMENT_MULTI:
                if (c == '*' && next == '/') {
                    state = STATE_CODE;
                    i++; // 跳过 /
                    fputc(' ', out); // 替换为空格
                } else if (c == '\n') {
                    fputc(c, out); // 保留换行
                }
                break;

            // --- 字符串保护 (原样输出) ---
            case STATE_STRING_SINGLE:
                fputc(c, out);
                if (c == '\\') { if(next) { fputc(next, out); i++; } }
                else if (c == '\'') state = STATE_CODE;
                break;

            case STATE_STRING_DOUBLE:
                fputc(c, out);
                if (c == '\\') { if(next) { fputc(next, out); i++; } }
                else if (c == '"') state = STATE_CODE;
                break;

            case STATE_STRING_TEMPLATE: // 关键：保护 https://
                fputc(c, out);
                if (c == '\\') { if(next) { fputc(next, out); i++; } }
                else if (c == '`') state = STATE_CODE;
                break;

            // --- Console 移除逻辑 (增强版) ---
            case STATE_CONSOLE:
                // 如果在 console 参数的字符串里 (例如 console.log(")"))
                if (console_in_string) {
                    if (c == '\\') { i++; } // 跳过转义
                    else if (c == console_quote_char) { console_in_string = 0; }
                }
                else {
                    // 如果遇到引号，进入字符串模式
                    if (c == '"' || c == '\'' || c == '`') {
                        console_in_string = 1;
                        console_quote_char = c;
                    }
                    else if (c == '(') {
                        console_paren_depth++;
                    }
                    else if (c == ')') {
                        console_paren_depth--;
                        if (console_paren_depth == 0) {
                            // Console 调用结束
                            state = STATE_CODE;
                        }
                    }
                }
                // 注意：这里不执行 fputc，即删除内容
                break;
        }
    }

    // 处理文件末尾的 slash
    if (state == STATE_SLASH) fputc('/', out);

    fclose(out);
    free(text);
    printf("  [Success] File cleaned. Logs and comments removed.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: Drag file onto this exe.\n");
        // 尝试处理默认文件
        FILE *test = fopen("_worker.js", "rb");
        if (test) { fclose(test); process_file("_worker.js"); }
        else { getchar(); }
        return 0;
    }
    for (int i = 1; i < argc; i++) process_file(argv[i]);
    printf("Done.\n");
    // getchar(); // 如果需要暂停窗口请取消注释
    return 0;
}
