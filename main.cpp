#include <cstring>
#include <iostream>

#include "toyc/lexer/token.h"

// yylval 定义（只有一个翻译单元提供定义）
toyc::LexerValue yylval;

// yylex() 由 Flex 生成，在 lex.yy.c 中定义
extern int yylex();

int main(int argc, char* argv[]) {
    // 解析命令行参数
    bool optFlag = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-opt") == 0) {
            optFlag = true;
        }
    }

    // 当前阶段：第三阶段 — 词法分析器
    // 从 stdin 读取源程序，输出 Token 流
    int token;
    while ((token = yylex()) != 0) {
        auto type = static_cast<toyc::TokenType>(token);
        std::cout << toyc::tokenTypeName(type);

        // 根据 Token 类型输出附加数据
        switch (type) {
            case toyc::TokenType::ID:
                std::cout << "("
                          << (yylval.strVal ? yylval.strVal : "")
                          << ")";
                free(yylval.strVal);
                yylval.strVal = nullptr;
                break;
            case toyc::TokenType::NUMBER:
                std::cout << "(" << yylval.intVal << ")";
                break;
            default:
                break;
        }

        std::cout << std::endl;
    }

    return 0;
}