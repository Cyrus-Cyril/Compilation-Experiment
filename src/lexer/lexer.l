%{
/* ToyC Lexer - 词法分析器 (Flex)
 *
 * 读取 ToyC 源代码（stdin），经词法分析后输出 Token 流。
 * Token 类型定义见 include/toyc/lexer/token.h
 *
 * 接口约定（面向 Bison Parser）：
 *   - yylex() 返回 int，值为 toyc::TokenType 枚举值（显式赋值 258+）
 *   - 文件结束返回 0 (= toyc::TokenType::END)
 *   - yylval.intVal  承载 NUMBER 的整数值
 *   - yylval.strVal  承载 ID 的字符串（malloc 分配，调用方负责 free）
 */

#include "toyc/lexer/token.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

/* yylval 由 main.cpp / test 文件定义，此处仅声明 */
extern toyc::LexerValue yylval;

/* 列号追踪（Flex 内置 yylineno 但无列号） */
static int col = 1;

/* 更新列号的辅助宏 */
#define UPDATE_COL()  col += static_cast<int>(yyleng)
%}

%option noyywrap yylineno
%x COMMENT

%%

[ \t\r]+    { UPDATE_COL(); }
\n          { yylineno++; col = 1; }

"//"[^\n]*  { /* single-line comment, skip */ }

"/*"        { BEGIN(COMMENT); }
<COMMENT>[^*\n]*        { UPDATE_COL(); }
<COMMENT>"*"+[^*/\n]*   { UPDATE_COL(); }
<COMMENT>\n             { yylineno++; col = 1; }
<COMMENT>"*"+"/"        { UPDATE_COL(); BEGIN(INITIAL); }
<COMMENT><<EOF>>        {
    fprintf(stderr,
            "Error(%d:%d): unterminated block comment\n",
            yylineno, col);
    return 0;
}

"int"       { UPDATE_COL(); return static_cast<int>(toyc::TokenType::INT); }
"void"      { UPDATE_COL(); return static_cast<int>(toyc::TokenType::VOID); }
"const"     { UPDATE_COL(); return static_cast<int>(toyc::TokenType::CONST); }
"if"        { UPDATE_COL(); return static_cast<int>(toyc::TokenType::IF); }
"else"      { UPDATE_COL(); return static_cast<int>(toyc::TokenType::ELSE); }
"while"     { UPDATE_COL(); return static_cast<int>(toyc::TokenType::WHILE); }
"break"     { UPDATE_COL(); return static_cast<int>(toyc::TokenType::BREAK); }
"continue"  { UPDATE_COL(); return static_cast<int>(toyc::TokenType::CONTINUE); }
"return"    { UPDATE_COL(); return static_cast<int>(toyc::TokenType::RETURN); }

"<="        { UPDATE_COL(); return static_cast<int>(toyc::TokenType::LE); }
">="        { UPDATE_COL(); return static_cast<int>(toyc::TokenType::GE); }
"=="        { UPDATE_COL(); return static_cast<int>(toyc::TokenType::EQ); }
"!="        { UPDATE_COL(); return static_cast<int>(toyc::TokenType::NE); }
"&&"        { UPDATE_COL(); return static_cast<int>(toyc::TokenType::AND); }
"||"        { UPDATE_COL(); return static_cast<int>(toyc::TokenType::OR); }
"+"         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::PLUS); }
"-"         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::MINUS); }
"*"         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::STAR); }
"/"         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::SLASH); }
"%"         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::PERCENT); }
"<"         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::LT); }
">"         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::GT); }
"!"         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::NOT); }
"="         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::ASSIGN); }

"("         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::LPAREN); }
")"         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::RPAREN); }
"{"         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::LBRACE); }
"}"         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::RBRACE); }
";"         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::SEMICOLON); }
","         { UPDATE_COL(); return static_cast<int>(toyc::TokenType::COMMA); }

("0"|[1-9][0-9]*) {
    yylval.intVal = static_cast<int>(strtol(yytext, nullptr, 10));
    UPDATE_COL();
    return static_cast<int>(toyc::TokenType::NUMBER);
}

[_A-Za-z][_A-Za-z0-9]* {
    yylval.strVal = static_cast<char*>(malloc(static_cast<size_t>(yyleng) + 1));
    if (yylval.strVal) {
        strcpy(yylval.strVal, yytext);
    }
    UPDATE_COL();
    return static_cast<int>(toyc::TokenType::ID);
}

. {
    fprintf(stderr,
            "Error(%d:%d): illegal character '%c'\n",
            yylineno, col, *yytext);
    col++;
}
%%
