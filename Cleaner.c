/*
 * JS Cleaner V7 (Smart Console Fix)
 * 1. 修复：误删非调用式 console.log 后的右括号导致 Syntax Error
 * 2. 修复：console.log 作为参数传递时，替换为 (()=>{}) 防止运行时错误
 * 3. 保持：对正则 /.../ 和 模板字符串 `...` 的完美保护
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

// 判断 / 是否为正则开头
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

// 检查 console 类型
// 返回值: 
// 0: 不是 console
// 1: 是 console.log(...) 调用 -> 需要替换为 void(0) 并吞噬参数
// 2: 是 console.log 引用 -> 需要替换为 (()=>{}) 并跳过
// 3: 仅仅是 console 单词 -> 不处理
int check_console_type(const char *text, long i, long size, long *method_len) {
    if (strncmp(&text[i], "console.", 8) != 0) return 0;
    
    // 检查方法名
    const char *methods[] = {"log", "warn", "error", "info", "debug", NULL};
    int matched = 0;
    long m_len = 0;
    
    for (int k = 0; methods[k]; k++) {
        long len = strlen(methods[k]);
        if (strncmp(&text[i + 8], methods[k], len) == 0) {
            // 确保方法名后不是字母数字 (防止 console.logging)
            char next = (i + 8 + len < size) ? text[i + 8 + len] : 0;
            if (!isalnum(next) && next != '_') {
                matched = 1;
                m_len = 8 + len; // "console." + "log"
                break;
            }
        }
    }
    
    if (!matched) return 0;
    
    *method_len = m_len;
    
    // 向后查找，看看是不是调用 (跟着 '(' )
    long j = i + m_len;
    while (j < size && isspace(text[j])) j++;
    
    if (j < size && text[j] == '(') {
        return 1; // 调用
    } else {
        return 2; // 引用 (作为参数或变量)
    }
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

    char *output = (char*)malloc(size * 2 + 1024); // 预留空间
    long out_idx = 0;
    
    State state = STATE_CODE;
    long i = 0;
    
    int skip_mode = 0; // 1 = 正在吞噬 console.log(...) 的参数
    int paren_depth = 0;
    int in_arg_str = 0; // 参数内的字符串状态

    while (i < size) {
        char c = input[i];
        char next = (i + 1 < size) ? input[i+1] : 0;

        // --- 模式：吞噬 Console 参数 ---
        if (skip_mode) {
            // 必须识别参数里的字符串，以免误判括号
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
                    if (paren_depth == 0) {
                        skip_mode = 0; // 吞噬结束
                    }
                }
            }
            i++;
            continue;
        }

        // --- 正常状态 ---
        if (state == STATE_CODE) {
            long m_len = 0;
            int c_type = check_console_type(input, i, size, &m_len);
            
            if (c_type == 1) { 
                // Case 1: console.log(...) 调用 -> 替换为 void(0) 并吞噬
                const char *rep = "void(0)";
                for(int k=0; rep[k]; k++) output[out_idx++] = rep[k];
                
                // 跳过 "console.log"
                i += m_len; 
                
                // 跳过空格直到 '('
                while(i < size && isspace(input[i])) i++;
                
                // 此时 input[i] 应该是 '(', 开启吞噬模式
                if (input[i] == '(') {
                    skip_mode = 1;
                    paren_depth = 1;
                    i++; // 消耗掉 '('
                }
                continue;
            } 
            else if (c_type == 2) {
                // Case 2: console.log 引用 -> 替换为 (()=>{})
                // 这样作为参数传递给 createUnifiedConnection 时，是一个空函数，不会报错
                const char *rep = "(()=>{})";
                for(int k=0; rep[k]; k++) output[out_idx++] = rep[k];
                i += m_len;
                continue;
            }
            // else: 不是 console，继续处理

            if (c == '\'') { state = STATE_STRING_SQ; output[out_idx++] = c; }
            else if (c == '"')  { state = STATE_STRING_DQ; output[out_idx++] = c; }
            else if (c == '`')  { state = STATE_STRING_TMP; output[out_idx++] = c; }
            else if (c == '/') {
                if (next == '/') {
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
        // --- 字符串 / 正则 / 注释 状态处理 (保持不变) ---
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
        else if (state == STATE_COMMENT_LINE) {
            if (c == '\n') { output[out_idx++] = c; state = STATE_CODE; }
        }
        else if (state == STATE_COMMENT_BLOCK) {
            if (c == '*' && next == '/') {
                state = STATE_CODE;
                output[out_idx++] = ' ';
                i++;
            } else if (c == '\n') output[out_idx++] = c;
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
