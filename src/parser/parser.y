%{
/* ToyC Parser - 语法分析器 (Bison)
 *
 * 当前为占位骨架，将在第四阶段完整实现。
 * AST 节点定义见 docs/design/ast_design.md
 *
 * 注意：#define yylval 重命名 Bison 生成的 yylval 符号，
 * 避免与 toyc_lexer 库中的 yylval 定义冲突。
 * Phase 4 实现 parser 时需重新对齐。
 */

#include <cstdio>
#include <cstdlib>

#define yylval parser_yylval

extern int yylex();
extern void yyerror(const char* msg);
%}

%%

program:
    /* empty */
    ;

%%

void yyerror(const char* msg) {
    fprintf(stderr, "Error: %s\n", msg);
}