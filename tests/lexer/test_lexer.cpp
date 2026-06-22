// ToyC Lexer 单元测试
//
// 测试各类 Token 的正确识别，包括：
//   关键字 / 运算符 / 分隔符 / 数字 / 标识符 / 注释 / 非法字符
//
// 使用方式：
//   编译后直接运行，自动执行全部测试用例并报告通过/失败。

extern int yylex();
extern void* yy_scan_string(const char*);
extern void yy_delete_buffer(void*);

#include "toyc/lexer/token.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static int test_count = 0;
static int pass_count = 0;

/// 断言单个 Token 的类型
static void expect_token(const char* input, int expected_token) {
    ++test_count;
    yy_scan_string(input);
    int token = yylex();
    // ID 的 strVal 需要释放
    if (token == static_cast<int>(toyc::TokenType::ID) && yylval.strVal) {
        free(yylval.strVal);
        yylval.strVal = nullptr;
    }
    if (token == expected_token) {
        ++pass_count;
        printf("  PASS: \"%s\" -> %s\n", input,
               toyc::tokenTypeName(static_cast<toyc::TokenType>(token)));
    } else {
        printf("  FAIL: \"%s\" -> got %s, expected %s\n", input,
               toyc::tokenTypeName(static_cast<toyc::TokenType>(token)),
               toyc::tokenTypeName(static_cast<toyc::TokenType>(expected_token)));
    }
}

/// 断言 NUMBER Token 的类型和值
static void expect_number(const char* input, int expected_val) {
    ++test_count;
    yy_scan_string(input);
    int token = yylex();
    bool ok = (token == static_cast<int>(toyc::TokenType::NUMBER) &&
               yylval.intVal == expected_val);
    if (ok) {
        ++pass_count;
        printf("  PASS: \"%s\" -> NUMBER(%d)\n", input, yylval.intVal);
    } else {
        printf("  FAIL: \"%s\" -> got %s", input,
               toyc::tokenTypeName(static_cast<toyc::TokenType>(token)));
        if (token == static_cast<int>(toyc::TokenType::NUMBER))
            printf("(%d)", yylval.intVal);
        printf(", expected NUMBER(%d)\n", expected_val);
    }
}

/// 断言 ID Token 的类型和字符串值
static void expect_id(const char* input, const char* expected_name) {
    ++test_count;
    yy_scan_string(input);
    int token = yylex();
    bool ok = (token == static_cast<int>(toyc::TokenType::ID) &&
               yylval.strVal && strcmp(yylval.strVal, expected_name) == 0);
    if (ok) {
        ++pass_count;
        printf("  PASS: \"%s\" -> ID(%s)\n", input, yylval.strVal);
    } else {
        printf("  FAIL: \"%s\" -> got %s", input,
               toyc::tokenTypeName(static_cast<toyc::TokenType>(token)));
        if (token == static_cast<int>(toyc::TokenType::ID) && yylval.strVal)
            printf("(%s)", yylval.strVal);
        printf(", expected ID(%s)\n", expected_name);
    }
    if (yylval.strVal) {
        free(yylval.strVal);
        yylval.strVal = nullptr;
    }
}

int main() {
    printf("=== ToyC Lexer Unit Tests ===\n\n");

    // ---- 关键字 ----
    printf("-- Keywords --\n");
    expect_token("int",      static_cast<int>(toyc::TokenType::INT));
    expect_token("void",     static_cast<int>(toyc::TokenType::VOID));
    expect_token("const",    static_cast<int>(toyc::TokenType::CONST));
    expect_token("if",       static_cast<int>(toyc::TokenType::IF));
    expect_token("else",     static_cast<int>(toyc::TokenType::ELSE));
    expect_token("while",    static_cast<int>(toyc::TokenType::WHILE));
    expect_token("break",    static_cast<int>(toyc::TokenType::BREAK));
    expect_token("continue", static_cast<int>(toyc::TokenType::CONTINUE));
    expect_token("return",   static_cast<int>(toyc::TokenType::RETURN));

    // ---- 运算符 ----
    printf("\n-- Operators --\n");
    expect_token("+",  static_cast<int>(toyc::TokenType::PLUS));
    expect_token("-",  static_cast<int>(toyc::TokenType::MINUS));
    expect_token("*",  static_cast<int>(toyc::TokenType::STAR));
    expect_token("/",  static_cast<int>(toyc::TokenType::SLASH));
    expect_token("%",  static_cast<int>(toyc::TokenType::PERCENT));
    expect_token("<",  static_cast<int>(toyc::TokenType::LT));
    expect_token(">",  static_cast<int>(toyc::TokenType::GT));
    expect_token("<=", static_cast<int>(toyc::TokenType::LE));
    expect_token(">=", static_cast<int>(toyc::TokenType::GE));
    expect_token("==", static_cast<int>(toyc::TokenType::EQ));
    expect_token("!=", static_cast<int>(toyc::TokenType::NE));
    expect_token("&&", static_cast<int>(toyc::TokenType::AND));
    expect_token("||", static_cast<int>(toyc::TokenType::OR));
    expect_token("!",  static_cast<int>(toyc::TokenType::NOT));
    expect_token("=",  static_cast<int>(toyc::TokenType::ASSIGN));

    // ---- 分隔符 ----
    printf("\n-- Delimiters --\n");
    expect_token("(", static_cast<int>(toyc::TokenType::LPAREN));
    expect_token(")", static_cast<int>(toyc::TokenType::RPAREN));
    expect_token("{", static_cast<int>(toyc::TokenType::LBRACE));
    expect_token("}", static_cast<int>(toyc::TokenType::RBRACE));
    expect_token(";", static_cast<int>(toyc::TokenType::SEMICOLON));
    expect_token(",", static_cast<int>(toyc::TokenType::COMMA));

    // ---- 数字 ----
    printf("\n-- Numbers --\n");
    expect_number("0",     0);
    expect_number("42",    42);
    expect_number("1",     1);
    expect_number("-0",    0);    // -0 == 0
    expect_number("-5",   -5);
    expect_number("-123", -123);

    // ---- 标识符 ----
    printf("\n-- Identifiers --\n");
    expect_id("x",     "x");
    expect_id("_tmp",  "_tmp");
    expect_id("foo123", "foo123");
    expect_id("_A",    "_A");

    // ---- 注释与空白（不产生 Token）----
    printf("\n-- Comments & Whitespace (should produce no token) --\n");
    // 单行注释
    {
        ++test_count;
        yy_scan_string("// this is a comment\n");
        int token = yylex();  // 应该跳过注释，遇到 EOF 返回 0
        if (token == 0) {
            ++pass_count;
            printf("  PASS: single-line comment skipped\n");
        } else {
            printf("  FAIL: single-line comment produced token %s\n",
                   toyc::tokenTypeName(static_cast<toyc::TokenType>(token)));
        }
    }
    // 多行注释
    {
        ++test_count;
        yy_scan_string("/* block\ncomment */");
        int token = yylex();
        if (token == 0) {
            ++pass_count;
            printf("  PASS: multi-line comment skipped\n");
        } else {
            printf("  FAIL: multi-line comment produced token %s\n",
                   toyc::tokenTypeName(static_cast<toyc::TokenType>(token)));
        }
    }
    // 空白
    {
        ++test_count;
        yy_scan_string("   \t\r\n");
        int token = yylex();
        if (token == 0) {
            ++pass_count;
            printf("  PASS: whitespace skipped\n");
        } else {
            printf("  FAIL: whitespace produced token %s\n",
                   toyc::tokenTypeName(static_cast<toyc::TokenType>(token)));
        }
    }

    // ---- 混合 Token 序列 ----
    printf("\n-- Sequence --\n");
    {
        ++test_count;
        yy_scan_string("int a = 42;");
        int t1 = yylex();  // INT
        int t2 = yylex();  // ID(a)
        if (yylval.strVal) { free(yylval.strVal); yylval.strVal = nullptr; }
        int t3 = yylex();  // ASSIGN
        int t4 = yylex();  // NUMBER(42)
        int t5 = yylex();  // SEMICOLON
        bool ok = (t1 == static_cast<int>(toyc::TokenType::INT) &&
                   t2 == static_cast<int>(toyc::TokenType::ID) &&
                   t3 == static_cast<int>(toyc::TokenType::ASSIGN) &&
                   t4 == static_cast<int>(toyc::TokenType::NUMBER) &&
                   yylval.intVal == 42 &&
                   t5 == static_cast<int>(toyc::TokenType::SEMICOLON));
        if (ok) {
            ++pass_count;
            printf("  PASS: \"int a = 42;\" -> INT ID ASSIGN NUMBER(42) SEMICOLON\n");
        } else {
            printf("  FAIL: \"int a = 42;\" sequence mismatch\n");
        }
    }

    // ---- 非法字符 ----
    printf("\n-- Illegal Characters (stderr, skip) --\n");
    {
        ++test_count;
        yy_scan_string("@");
        int token = yylex();
        if (token == 0) {
            ++pass_count;
            printf("  PASS: illegal char '@' reported to stderr and skipped\n");
        } else {
            printf("  FAIL: illegal char produced token %s\n",
                   toyc::tokenTypeName(static_cast<toyc::TokenType>(token)));
        }
    }

    // ---- 报告 ----
    printf("\n==============================\n");
    printf("  %d / %d tests passed\n", pass_count, test_count);
    printf("==============================\n");

    return (pass_count == test_count) ? 0 : 1;
}