/*
 * JS Cleaner V8 (Final Polish)
 * 1. 新增：自动识别并彻底删除“整行注释”，不留空行
 * 2. 保持：V7 的所有安全特性 (修复 Unexpected token, 保护 https://, 智能处理 console)
 * 3. 细节：自动清理注释前的缩进空格
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

// 判断 / 是否为正则开头 (同V7)
int is_regex_start(const char *text, long idx) {
    long i = idx - 1;
    while (i >= 0 && isspace(text[i])) i--;
    if (i < 0) return 1;
    char last = text[i];
    if (strchr("(=,:!&|?{};,", last)) return 1;
    if (isalnum(last) || last == '_' || last == '$' || last == ')') {
        long end = i;
        while (i >= 0 && (isalnum(text[i]) || text[i] == '_' || text[i] == '$')) i--;
        long len = end - i;
        char word[32];
        if (len < 31) {
            strncpy(word, &text[i+1], len);
            word[len] = '\0';
            if (strcmp(word, "return") == 0) return 1;
            if (strcmp(word, "case") == 0) return 1;
            if (strcmp(word, "throw") == 0) return 1;
            if (strcmp(word, "delete") == 0) return 1;
            if (strcmp(word, "void") == 0) return 1;
            if (strcmp(word, "typeof") == 0) return 1;
            if (strcmp(word, "await") == 0) return 1;
            if (strcmp(word, "yield") == 0) return 1;
        }
        return 0;
    }
    return 0;
}

// 检查 console 类型 (同V7)
int check_console_type(const char *text, long i, long size, long *method_len) {
    if (strncmp(&text[i], "console.", 8) != 0) return 0;
    const char *methods[] = {"log", "warn", "error", "info", "debug", NULL};
    int matched = 0;
    long m_len = 0;
    for (int k = 0; methods[k]; k++) {
        long len = strlen(methods[k]);
        if (strncmp(&text[i + 8], methods[k], len) == 0) {
            char next = (i + 8 + len < size) ? text[i + 8 + len] : 0;
            if (!isalnum(next) && next != '_') {
                matched = 1;
                m_len = 8 + len;
                break;
            }
        }
    }
    if (!matched) return 0;
    *method_len = m_len;
    long j = i + m_len;
    while (j < size && isspace(text[j])) j++;
    if (j < size && text[j] == '(') return 1; 
    else return 2;
}

void process_file(const char *filename) {
    printf("Processing: %s ...\n", filename);
    
    FILE *f = fopen(filename, "rb");
    if (!f) { printf("  [Error] File not found.\n"); return; }
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

    char *output = (char*)malloc(size * 2 + 1024);
    long out_idx = 0;
    
    State state = STATE_CODE;
    long i = 0;
    
    int skip_mode = 0; 
    int paren_depth = 0;
    int in_arg_str = 0; 
    
    // 新增：标记当前是否为整行注释
    int is_whole_line_comment = 0;

    while (i < size) {
        char c = input[i];
        char next = (i + 1 < size) ? input[i+1] : 0;

        // --- Console 参数吞噬模式 ---
        if (skip_mode) {
            if (in_arg_str) {
                if (c == '\\') { i++; } 
                else if ((in_arg_str == 1 && c == '\'') ||
                         (in_arg_str == 2 && c == '"') ||
                         (in_arg_str == 3 && c == '`')) {
                    in_arg_str = 0;
                }
            } else {
                if (c == '\'') in_arg_str = 1;
                else if (c == '"') in_arg_str = 2;
                else if (c == '`') in_arg_str = 3;
                else if (c == '(') paren_depth++;
                else if (c == ')') {
                    paren_depth--;
                    if (paren_depth == 0) skip_mode = 0;
                }
            }
            i++;
            continue;
        }

        // --- 正常模式 ---
        if (state == STATE_CODE) {
            long m_len = 0;
            int c_type = check_console_type(input, i, size, &m_len);
            
            if (c_type == 1) { // console.log(...) 调用
                const char *rep = "void(0)";
                for(int k=0; rep[k]; k++) output[out_idx++] = rep[k];
                i += m_len; 
                while(i < size && isspace(input[i])) i++;
                if (input[i] == '(') {
                    skip_mode = 1;
                    paren_depth = 1;
                    i++; 
                }
                continue;
            } 
            else if (c_type == 2) { // console.log 引用
                const char *rep = "(()=>{})";
                for(int k=0; rep[k]; k++) output[out_idx++] = rep[k];
                i += m_len;
                continue;
            }

            if (c == '\'') { state = STATE_STRING_SQ; output[out_idx++] = c; }
            else if (c == '"')  { state = STATE_STRING_DQ; output[out_idx++] = c; }
            else if (c == '`')  { state = STATE_STRING_TMP; output[out_idx++] = c; }
            else if (c == '/') {
                if (next == '/') {
                    // --- 核心改进：检测是否为整行注释 ---
                    // 回溯检查 output 缓冲区，看这一行前面是否全是空白
                    long temp_idx = out_idx;
                    int only_spaces = 1;
                    while (temp_idx > 0) {
                        char prev = output[temp_idx - 1];
                        if (prev == '\n' || prev == '\r') break; // 到了上一行末尾
                        if (prev != ' ' && prev != '\t') {
                            only_spaces = 0;
                            break;
                        }
                        temp_idx--;
                    }
                    
                    if (only_spaces) {
                        // 是整行注释！
                        out_idx = temp_idx; // 撤销之前写入的空格（回退指针）
                        is_whole_line_comment = 1; // 标记
                    } else {
                        is_whole_line_comment = 0;
                    }

                    state = STATE_COMMENT_LINE;
                    i++; 
                } else if (next == '*') {
                    state = STATE_COMMENT_BLOCK;
                    i++; 
                } else {
                    if (is_regex_start(input, i)) state = STATE_REGEX;
                    output[out_idx++] = c;
                }
            }
            else {
                output[out_idx++] = c;
            }
        }
        // --- 字符串 / 正则 ---
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
            if (c == '\\') { if(next) { output[out_idx++] = next; i++; } }
            else if (c == '/') state = STATE_CODE;
            else if (c == '\n') state = STATE_CODE;
        }
        // --- 注释处理 (带空行清理) ---
        else if (state == STATE_COMMENT_LINE) {
            if (c == '\n') {
                if (!is_whole_line_comment) {
                    output[out_idx++] = c; // 如果是行尾注释，保留换行
                }
                // 如果是整行注释，c (\n) 不会被写入，相当于整行被删
                state = STATE_CODE;
                is_whole_line_comment = 0; // 重置
            }
        }
        else if (state == STATE_COMMENT_BLOCK) {
            if (c == '*' && next == '/') {
                state = STATE_CODE;
                output[out_idx++] = ' ';
                i++;
            } else if (c == '\n') {
                output[out_idx++] = c;
            }
        }
        i++;
    }

    FILE *out = fopen(filename, "wb");
    fwrite(output, 1, out_idx, out);
    fclose(out);
    free(input);
    free(output);
    printf("Done.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        FILE *f = fopen("_worker.js", "rb");
        if(f) { fclose(f); process_file("_worker.js"); }
        else { printf("Usage: Drag file here.\n"); getchar(); }
        return 0;
    }
    for(int i=1; i<argc; i++) process_file(argv[i]);
    return 0;
}
