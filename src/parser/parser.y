%{
/* ToyC Parser - 语法分析器 (Bison)
 *
 * 当前为占位骨架，将在第四阶段完整实现。
 * AST 节点定义见 docs/design/ast_design.md
 */

#include <cstdio>
#include <cstdlib>

extern int yylex();
extern void yyerror(const char* msg);
%}

%%

program:
    /* empty */
    ;

%%

void yyerror(const char* msg) {
    fprintf(stderr, "Error: %s\\n", msg);
}