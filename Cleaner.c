/*
 * JS Comment Cleaner (State Machine V4)
 * Fix: Variable scope compilation error
 * Feature: Added support for regex escapes (\/) in code
 */

#include <stdio.h>
#include <stdlib.h>

// 定义状态
#define STATE_CODE          0 // 正常代码
#define STATE_SLASH         1 // 读到了 /
#define STATE_LINE_COMMENT  2 // 单行注释 //
#define STATE_BLOCK_COMMENT 3 // 多行注释 /* */
#define STATE_STAR          4 // 块注释中的 *
#define STATE_QUOTE_S       5 // 单引号 '
#define STATE_QUOTE_D       6 // 双引号 "
#define STATE_QUOTE_T       7 // 模板字符串 `

void process_file(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("Error opening input file: %s\n", filename);
        return;
    }

    char out_filename[1024];
    sprintf(out_filename, "%s.temp", filename);
    FILE *out = fopen(out_filename, "wb");
    if (!out) {
        printf("Error opening output file\n");
        fclose(fp);
        return;
    }

    int state = STATE_CODE;
    int c;
    int next_c; // 【修复】变量提前声明，解决编译错误

    printf("Processing: %s ... ", filename);

    while ((c = fgetc(fp)) != EOF) {
        switch (state) {
            case STATE_CODE:
                if (c == '\\') { 
                    // 【新增】处理代码中的转义（如正则表达式中的 \/）
                    fputc(c, out);
                    next_c = fgetc(fp);
                    if (next_c != EOF) fputc(next_c, out);
                } else if (c == '/') {
                    state = STATE_SLASH;
                } else if (c == '\'') {
                    state = STATE_QUOTE_S;
                    fputc(c, out);
                } else if (c == '"') {
                    state = STATE_QUOTE_D;
                    fputc(c, out);
                } else if (c == '`') { 
                    state = STATE_QUOTE_T;
                    fputc(c, out);
                } else {
                    fputc(c, out);
                }
                break;

            case STATE_SLASH:
                if (c == '/') {
                    state = STATE_LINE_COMMENT; // 确认是 //
                } else if (c == '*') {
                    state = STATE_BLOCK_COMMENT; // 确认是 /*
                } else {
                    // 只是普通除号或路径
                    state = STATE_CODE;
                    fputc('/', out);
                    // 检查当前字符是否开启字符串
                    if (c == '\'') state = STATE_QUOTE_S;
                    else if (c == '"') state = STATE_QUOTE_D;
                    else if (c == '`') state = STATE_QUOTE_T;
                    fputc(c, out);
                }
                break;

            case STATE_LINE_COMMENT:
                if (c == '\n') {
                    state = STATE_CODE;
                    fputc(c, out); // 保留换行
                }
                break;

            case STATE_BLOCK_COMMENT:
                if (c == '*') {
                    state = STATE_STAR;
                }
                if (c == '\n') {
                    fputc(c, out); // 保留换行
                }
                break;

            case STATE_STAR:
                if (c == '/') {
                    state = STATE_CODE; // 结束 */
                    fputc(' ', out);
                } else if (c == '*') {
                    state = STATE_STAR;
                } else {
                    state = STATE_BLOCK_COMMENT;
                    if (c == '\n') fputc(c, out);
                }
                break;

            // --- 字符串保护区 ---
            case STATE_QUOTE_S:
                fputc(c, out);
                if (c == '\\') {
                    next_c = fgetc(fp);
                    if (next_c != EOF) fputc(next_c, out);
                } else if (c == '\'') {
                    state = STATE_CODE;
                }
                break;

            case STATE_QUOTE_D:
                fputc(c, out);
                if (c == '\\') {
                    next_c = fgetc(fp);
                    if (next_c != EOF) fputc(next_c, out);
                } else if (c == '"') {
                    state = STATE_CODE;
                }
                break;

            case STATE_QUOTE_T:
                fputc(c, out);
                if (c == '\\') {
                    next_c = fgetc(fp);
                    if (next_c != EOF) fputc(next_c, out);
                } else if (c == '`') {
                    state = STATE_CODE;
                }
                break;
        }
    }

    if (state == STATE_SLASH) {
        fputc('/', out);
    }

    fclose(fp);
    fclose(out);

    remove(filename);
    rename(out_filename, filename);
    printf("Done.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        // 如果没有参数，尝试处理默认文件（方便调试）
        FILE *test = fopen("_worker.js", "rb");
        if (test) {
            fclose(test);
            process_file("_worker.js");
        } else {
            printf("Usage: Drag _worker.js onto this exe\n");
            getchar();
            return 1;
        }
    } else {
        for (int i = 1; i < argc; i++) {
            process_file(argv[i]);
        }
    }
    return 0;
}
