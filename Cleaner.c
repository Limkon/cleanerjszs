/*
 * Professional JS Cleaner V5 (Final Fix)
 * 功能：
 * 1. 移除 // 和 / * ... * / 注释
 * 2. 移除 console.log/error/info 等调试信息 (替换为 void(0))
 * 3. 核心修复：完美保护 ES6 模板字符串 (`...`) 中的 URL (https://)
 * 4. 稳健备份：确保先生成备份文件再修改原文件
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 定义状态机状态
typedef enum {
    STATE_CODE,             // 正常代码
    STATE_SLASH,            // 读到了 / (准备判断是否为注释)
    STATE_STRING_SINGLE,    // 单引号字符串 '...'
    STATE_STRING_DOUBLE,    // 双引号字符串 "..."
    STATE_STRING_TEMPLATE,  // ES6 模板字符串 `...` (关键修复状态)
    STATE_COMMENT_SINGLE,   // 单行注释 //...
    STATE_COMMENT_MULTI,    // 多行注释 /*...*/
    STATE_CONSOLE           // 正在识别/跳过 console.xxx(...)
} State;

// 辅助：判断是否开始 console 调用
int check_console_start(const char* buffer, size_t idx, size_t len) {
    if (idx + 8 > len) return 0;
    if (strncmp(&buffer[idx], "console.", 8) == 0) return 1;
    return 0;
}

// 复制文件 (用于备份)
int copy_file(const char *src_path, const char *dst_path) {
    FILE *src = fopen(src_path, "rb");
    if (!src) return 0;
    FILE *dst = fopen(dst_path, "wb");
    if (!dst) { fclose(src); return 0; }
    
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
    }
    fclose(src);
    fclose(dst);
    return 1;
}

void process_file(const char *filename) {
    printf("Processing: %s ...\n", filename);

    // 1. 强制备份 (Backup First)
    char backup_name[1024];
    snprintf(backup_name, sizeof(backup_name), "%s.bak", filename);
    
    // 检查源文件是否存在
    FILE *check = fopen(filename, "rb");
    if (!check) {
        printf("  [Error] Input file not found!\n");
        return;
    }
    fclose(check);

    if (!copy_file(filename, backup_name)) {
        printf("  [Error] Failed to create backup. Aborting safely.\n");
        return;
    }
    printf("  [Info] Backup created: %s\n", backup_name);

    // 2. 读取整个文件到内存 (便于向后预读 console 语句)
    FILE *f = fopen(filename, "rb");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    
    char *text = (char*)malloc(fsize + 1);
    if (!text) { printf("  [Error] Memory allocation failed.\n"); fclose(f); return; }
    fread(text, 1, fsize, f);
    text[fsize] = '\0';
    fclose(f);

    // 3. 打开文件准备写入
    FILE *out = fopen(filename, "wb");
    if (!out) { printf("  [Error] Cannot write to file.\n"); free(text); return; }

    State state = STATE_CODE;
    int paren_depth = 0; // 用于跟踪 console.log(...) 的括号深度

    for (long i = 0; i < fsize; i++) {
        char c = text[i];
        char next = (i + 1 < fsize) ? text[i + 1] : '\0';

        // --- 状态机逻辑 ---
        switch (state) {
            case STATE_CODE:
                // 检查是否是 console.xxx
                if (check_console_start(text, i, fsize)) {
                    state = STATE_CONSOLE;
                    paren_depth = 0;
                    fprintf(out, "void(0)"); // 安全替换，保持语法结构 (如箭头函数)
                    i += 7; // 跳过 "console." (循环末尾会+1, 所以这里少加一点)
                }
                else if (c == '\'') { state = STATE_STRING_SINGLE; fputc(c, out); }
                else if (c == '"')  { state = STATE_STRING_DOUBLE; fputc(c, out); }
                else if (c == '`')  { state = STATE_STRING_TEMPLATE; fputc(c, out); } // 进入模板字符串
                else if (c == '/')  { state = STATE_SLASH; } // 暂不写入，等待确认
                else { fputc(c, out); }
                break;

            case STATE_SLASH:
                if (c == '/') { state = STATE_COMMENT_SINGLE; } // 确认是 //
                else if (c == '*') { state = STATE_COMMENT_MULTI; } // 确认是 /*
                else {
                    // 只是普通的除号，补上刚才跳过的 /，并写入当前字符
                    fputc('/', out);
                    state = STATE_CODE;
                    
                    // 重新检查当前字符 c，因为它可能是引号或 console 的开始
                    // 简单回退索引重新处理 (简单粗暴但有效)
                    i--; 
                }
                break;

            case STATE_COMMENT_SINGLE:
                if (c == '\n') {
                    fputc(c, out); // 保留换行符，防止行合并错误
                    state = STATE_CODE;
                }
                break;

            case STATE_COMMENT_MULTI:
                if (c == '*' && next == '/') {
                    state = STATE_CODE;
                    i++; // 跳过 /
                    fputc(' ', out); // 替换为一个空格
                } else if (c == '\n') {
                    fputc(c, out); // 保留多行注释内的换行，保持行号一致
                }
                break;

            // --- 字符串保护状态 (忽略内部的 // 和 console) ---
            case STATE_STRING_SINGLE:
                fputc(c, out);
                if (c == '\\') { if (next) { fputc(next, out); i++; } } // 跳过转义
                else if (c == '\'') { state = STATE_CODE; }
                break;

            case STATE_STRING_DOUBLE:
                fputc(c, out);
                if (c == '\\') { if (next) { fputc(next, out); i++; } }
                else if (c == '"') { state = STATE_CODE; }
                break;

            case STATE_STRING_TEMPLATE: // 关键：保护 `https://...`
                fputc(c, out);
                if (c == '\\') { if (next) { fputc(next, out); i++; } }
                else if (c == '`') { state = STATE_CODE; }
                break;

            case STATE_CONSOLE:
                // 在跳过 console 语句时，必须小心字符串内的括号
                // 简化处理：我们假设 console 语句内括号是匹配的
                // 为了极度安全，这里只做简单的括号计数，如果遇到引号也简单跳过
                if (c == '(') paren_depth++;
                else if (c == ')') {
                    paren_depth--;
                    if (paren_depth <= 0) state = STATE_CODE; // console 结束
                }
                // 注意：这里完全不写入任何内容，达到删除效果
                break;
        }
    }

    // 处理文件末尾如果还停留在 STATE_SLASH
    if (state == STATE_SLASH) fputc('/', out);

    fclose(out);
    free(text);
    printf("  [Success] File cleaned successfully.\n");
}

int main(int argc, char *argv[]) {
    printf("JS Cleaner Tool V5\n");
    if (argc < 2) {
        printf("Usage: Drag _worker.js file onto this exe.\n");
        // 方便调试，如果有默认文件直接处理
        FILE *f = fopen("_worker.js", "rb");
        if(f) { fclose(f); process_file("_worker.js"); }
        else { getchar(); }
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        process_file(argv[i]);
    }
    printf("Done. Press Enter to exit...");
    getchar();
    return 0;
}
