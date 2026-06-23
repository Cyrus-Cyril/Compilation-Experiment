#include <cstring>
#include <iostream>

#include "toyc/lexer/token.h"

extern int yylex();

int main(int argc, char* argv[]) {
    bool optFlag = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-opt") == 0) {
            optFlag = true;
        }
    }

    (void)optFlag;

    int token;
    while ((token = yylex()) != 0) {
        auto type = static_cast<toyc::TokenType>(token);
        std::cout << toyc::tokenTypeName(type);

        switch (type) {
            case toyc::TokenType::ID:
                std::cout << "(" << (yylval.strVal ? yylval.strVal : "") << ")";
                free(yylval.strVal);
                yylval.strVal = nullptr;
                break;
            case toyc::TokenType::NUMBER:
                std::cout << "(" << yylval.intVal << ")";
                break;
            default:
                break;
        }

        std::cout << '\n';
    }

    return 0;
}
