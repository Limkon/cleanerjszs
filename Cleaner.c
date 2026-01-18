/* * Professional JS Comment Cleaner (C State Machine)
 * 核心功能：精准移除 JS 注释，完美保护 ES6 模板字符串、正则表达式和 URL
 * 编译命令示例：gcc cleaner.c -o cleaner.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 定义状态机状态
typedef enum {
    STATE_CODE,          // 正常代码
    STATE_STRING_SINGLE, // 单引号字符串 '...'
    STATE_STRING_DOUBLE, // 双引号字符串 "..."
    STATE_STRING_TEMPLATE, // ES6 模板字符串 `...` (关键修复)
    STATE_COMMENT_SINGLE, // 单行注释 //...
    STATE_COMMENT_MULTI   // 多行注释 /*...*/
} State;

void process_file(const char *filename) {
    char backup_name[1024];
    snprintf(backup_name, sizeof(backup_name), "%s.bak", filename);

    // 1. 创建备份
    FILE *src = fopen(filename, "rb"); // 二进制读取，防止换行符转换问题
    if (!src) {
        printf("[Error] Cannot open file: %s\n", filename);
        return;
    }
    
    FILE *bak = fopen(backup_name, "wb");
    if (!bak) {
        printf("[Error] Cannot create backup.\n");
        fclose(src);
        return;
    }

    // 复制内容到备份
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, n, bak);
    }
    fclose(bak);
    rewind(src); //回到文件开头准备处理

    // 2. 准备写回原文件 (覆盖)
    FILE *dst = fopen(filename, "wb");
    if (!dst) {
        printf("[Error] Cannot write to file: %s\n", filename);
        fclose(src);
        return;
    }

    printf("Processing: %s ... ", filename);

    State state = STATE_CODE;
    int c, next;

    while ((c = fgetc(src)) != EOF) {
        // ------------------------------------------------
        // 1. 字符串/模板状态 (优先级最高，保护内容)
        // ------------------------------------------------
        if (state == STATE_STRING_SINGLE || 
            state == STATE_STRING_DOUBLE || 
            state == STATE_STRING_TEMPLATE) {
            
            fputc(c, dst); // 照原样写入
            
            // 处理转义字符 (如 \")
            if (c == '\\') {
                int escaped = fgetc(src);
                if (escaped != EOF) fputc(escaped, dst);
            } 
            // 检查结束引号
            else {
                if (state == STATE_STRING_SINGLE && c == '\'') state = STATE_CODE;
                else if (state == STATE_STRING_DOUBLE && c == '"') state = STATE_CODE;
                else if (state == STATE_STRING_TEMPLATE && c == '`') state = STATE_CODE;
            }
        }
        // ------------------------------------------------
        // 2. 单行注释状态 // ...
        // ------------------------------------------------
        else if (state == STATE_COMMENT_SINGLE) {
            // 只写入换行符，忽略其他内容
            if (c == '\n') {
                fputc(c, dst);
                state = STATE_CODE;
            }
        }
        // ------------------------------------------------
        // 3. 多行注释状态 /* ... */
        // ------------------------------------------------
        else if (state == STATE_COMMENT_MULTI) {
            if (c == '*') {
                int next_c = fgetc(src);
                if (next_c == '/') {
                    // 注释结束，写入一个空格防止粘连 (var a/*...*/b -> var a b)
                    fputc(' ', dst); 
                    state = STATE_CODE;
                } else {
                    ungetc(next_c, src); // 没结束，放回去
                }
            }
            else if (c == '\n') {
                fputc(c, dst); // 保留换行，避免破坏行号结构
            }
        }
        // ------------------------------------------------
        // 4. 正常代码状态 (寻找状态入口)
        // ------------------------------------------------
        else {
            if (c == '\'') state = STATE_STRING_SINGLE;
            else if (c == '"') state = STATE_STRING_DOUBLE;
            else if (c == '`') state = STATE_STRING_TEMPLATE; // 核心修复：进入模板模式
            else if (c == '/') {
                int next_c = fgetc(src);
                if (next_c == '/') {
                    state = STATE_COMMENT_SINGLE; // 发现 //
                    continue; // 跳过输出
                } 
                else if (next_c == '*') {
                    state = STATE_COMMENT_MULTI;  // 发现 /*
                    continue; // 跳过输出
                } 
                else {
                    // 只是普通的除号或正则斜杠，不是注释
                    fputc(c, dst);
                    if (next_c != EOF) ungetc(next_c, src); // 放回去下轮处理
                    continue;
                }
            }
            
            // 写入普通字符
            if (state == STATE_CODE) fputc(c, dst);
        }
    }

    fclose(src);
    fclose(dst);
    printf("Done.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: drag files onto this exe\n");
        getchar();
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        process_file(argv[i]);
    }
    
    printf("\nAll files processed. Press Enter to exit...");
    getchar();
    return 0;
}
