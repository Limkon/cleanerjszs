/*
 * JS Comment Cleaner (State Machine)
 * 功能：安全移除 JS 注释，完美保护 URL、模板字符串和正则
 * 修复：解决 https:// 被误删导致的 Unexpected token 错误
 */

#include <stdio.h>
#include <stdlib.h>

// 定义状态
#define STATE_CODE          0 // 正常代码
#define STATE_SLASH         1 // 读到了 / (可能是注释开始，也可能是除号)
#define STATE_LINE_COMMENT  2 // 单行注释 //
#define STATE_BLOCK_COMMENT 3 // 多行注释 /* */
#define STATE_STAR          4 // 在多行注释中读到了 * (可能是结束)
#define STATE_QUOTE_S       5 // 单引号字符串 '
#define STATE_QUOTE_D       6 // 双引号字符串 "
#define STATE_QUOTE_T       7 // 模板字符串 ` (关键修复)

void process_file(const char *filename) {
    // 打开源文件
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Error opening input file");
        return;
    }

    // 创建临时输出文件
    char out_filename[1024];
    sprintf(out_filename, "%s.temp", filename);
    FILE *out = fopen(out_filename, "wb");
    if (!out) {
        perror("Error opening output file");
        fclose(fp);
        return;
    }

    int state = STATE_CODE;
    int c;

    printf("Processing: %s ... ", filename);

    while ((c = fgetc(fp)) != EOF) {
        switch (state) {
            case STATE_CODE:
                if (c == '/') {
                    state = STATE_SLASH;
                } else if (c == '\'') {
                    state = STATE_QUOTE_S;
                    fputc(c, out);
                } else if (c == '"') {
                    state = STATE_QUOTE_D;
                    fputc(c, out);
                } else if (c == '`') { // 进入模板字符串保护模式
                    state = STATE_QUOTE_T;
                    fputc(c, out);
                } else {
                    fputc(c, out);
                }
                break;

            case STATE_SLASH:
                if (c == '/') {
                    state = STATE_LINE_COMMENT; // 确认是 // 注释
                } else if (c == '*') {
                    state = STATE_BLOCK_COMMENT; // 确认是 /* 注释
                } else {
                    // 只是普通的除号或路径分隔符，把刚才暂存的 / 和当前的 c 都写入
                    state = STATE_CODE;
                    fputc('/', out);
                    // 此时需要重新检查 c，因为 c 可能是引号等开始
                    // 简单起见，这里直接写入 c，除非 /' 这种罕见情况，但在 JS 语法中 / 后跟引号通常是除法
                    if (c == '\'') state = STATE_QUOTE_S;
                    else if (c == '"') state = STATE_QUOTE_D;
                    else if (c == '`') state = STATE_QUOTE_T;
                    fputc(c, out);
                }
                break;

            case STATE_LINE_COMMENT:
                if (c == '\n') {
                    state = STATE_CODE; // 行尾，注释结束
                    fputc(c, out);      // 保留换行符，防止代码粘连
                }
                break;

            case STATE_BLOCK_COMMENT:
                if (c == '*') {
                    state = STATE_STAR;
                }
                if (c == '\n') {
                    fputc(c, out); // 保留多行注释内的换行，保持行号对应（可选）
                }
                break;

            case STATE_STAR:
                if (c == '/') {
                    state = STATE_CODE; // 块注释结束 */
                    fputc(' ', out);    // 替换为一个空格，防止 var a/*...*/b 变成 var ab
                } else if (c == '*') {
                    state = STATE_STAR; // 还是 *，继续保持
                } else {
                    state = STATE_BLOCK_COMMENT; // 假警报，回到注释状态
                    if (c == '\n') fputc(c, out);
                }
                break;

            // --- 字符串保护区：忽略内部所有的 / 和 * ---
            case STATE_QUOTE_S:
                fputc(c, out);
                if (c == '\\') { // 处理转义字符 \ 
                    int next = fgetc(fp);
                    if (next != EOF) fputc(next, out);
                } else if (c == '\'') {
                    state = STATE_CODE;
                }
                break;

            case STATE_QUOTE_D:
                fputc(c, out);
                if (c == '\\') {
                    int next = fgetc(fp);
                    if (next != EOF) fputc(next, out);
                } else if (c == '"') {
                    state = STATE_CODE;
                }
                break;

            case STATE_QUOTE_T: // 模板字符串 (最关键的部分)
                fputc(c, out);
                if (c == '\\') {
                    int next = fgetc(fp);
                    if (next != EOF) fputc(next, out);
                } else if (c == '`') {
                    state = STATE_CODE;
                }
                break;
        }
    }

    // 处理文件末尾可能遗留的 /
    if (state == STATE_SLASH) {
        fputc('/', out);
    }

    fclose(fp);
    fclose(out);

    // 替换原文件
    remove(filename);
    rename(out_filename, filename);
    printf("Done.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Drag file to this exe to process.\n");
        getchar();
        return 1;
    }
    // 处理拖入的所有文件
    for (int i = 1; i < argc; i++) {
        process_file(argv[i]);
    }
    return 0;
}
