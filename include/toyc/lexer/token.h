#pragma once

#include <string>

namespace toyc {

/// Token 类型枚举
enum class TokenType {
    // 关键字
    INT,
    VOID,
    CONST,
    IF,
    ELSE,
    WHILE,
    BREAK,
    CONTINUE,
    RETURN,
    // 运算符
    PLUS,   // +
    MINUS,  // -
    STAR,   // *
    SLASH,  // /
    PERCENT,// %
    LT,     // <
    GT,     // >
    LE,     // <=
    GE,     // >=
    EQ,     // ==
    NE,     // !=
    AND,    // &&
    OR,     // ||
    NOT,    // !
    ASSIGN, // =
    // 分隔符
    LPAREN,    // (
    RPAREN,    // )
    LBRACE,    // {
    RBRACE,    // }
    SEMICOLON, // ;
    COMMA,     // ,
    // 字面量 / 标识符
    ID,
    NUMBER,
    // 结束标记
    END,
};

/// 源码位置
struct SourceLocation {
    int line = 0;
    int column = 0;
};

/// Token 结构体
struct Token {
    TokenType type = TokenType::END;
    std::string lexeme;
    int value = 0;
    SourceLocation loc;
};

}  // namespace toyc