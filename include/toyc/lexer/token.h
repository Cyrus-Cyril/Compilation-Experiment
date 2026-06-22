#pragma once

#include <cstdlib>
#include <string>

namespace toyc {

/// Token 类型枚举
/// 显式赋值以匹配 Bison 默认 %token 编码（258+），
/// 这样当 parser.y 使用 %token INT VOID ... 时，
/// Bison 生成的 #define 值与本枚举一致。
enum class TokenType : int {
    // 关键字（9 个，258~266）
    INT = 258,
    VOID,
    CONST,
    IF,
    ELSE,
    WHILE,
    BREAK,
    CONTINUE,
    RETURN,
    // 运算符（15 个，267~281）
    PLUS,    // +
    MINUS,   // -
    STAR,    // *
    SLASH,   // /
    PERCENT, // %
    LT,      // <
    GT,      // >
    LE,      // <=
    GE,      // >=
    EQ,      // ==
    NE,      // !=
    AND,     // &&
    OR,      // ||
    NOT,     // !
    ASSIGN,  // =
    // 分隔符（6 个，282~287）
    LPAREN,    // (
    RPAREN,    // )
    LBRACE,    // {
    RBRACE,    // }
    SEMICOLON, // ;
    COMMA,     // ,
    // 字面量 / 标识符（2 个，288~289）
    ID,
    NUMBER,
    // 结束标记（必须为 0 —— yylex() 返回 0 表示 EOF）
    END = 0,
};

/// 源码位置
struct SourceLocation {
    int line = 1;
    int column = 1;
};

/// Token 结构体
struct Token {
    TokenType type = TokenType::END;
    std::string lexeme;
    int value = 0;
    SourceLocation loc;
};

/// 词法分析器值联合 — 在 Flex 规则与 Bison 之间传递 Token 附加数据
struct LexerValue {
    int intVal = 0;         // NUMBER 时使用
    char* strVal = nullptr; // ID 时使用（malloc 分配，调用方负责 free）
};

/// TokenType → 可读字符串
inline const char* tokenTypeName(TokenType type) {
    switch (type) {
        // 关键字
        case TokenType::INT:      return "INT";
        case TokenType::VOID:     return "VOID";
        case TokenType::CONST:    return "CONST";
        case TokenType::IF:       return "IF";
        case TokenType::ELSE:     return "ELSE";
        case TokenType::WHILE:    return "WHILE";
        case TokenType::BREAK:    return "BREAK";
        case TokenType::CONTINUE: return "CONTINUE";
        case TokenType::RETURN:   return "RETURN";
        // 运算符
        case TokenType::PLUS:     return "PLUS";
        case TokenType::MINUS:    return "MINUS";
        case TokenType::STAR:     return "STAR";
        case TokenType::SLASH:    return "SLASH";
        case TokenType::PERCENT:  return "PERCENT";
        case TokenType::LT:       return "LT";
        case TokenType::GT:       return "GT";
        case TokenType::LE:       return "LE";
        case TokenType::GE:       return "GE";
        case TokenType::EQ:       return "EQ";
        case TokenType::NE:       return "NE";
        case TokenType::AND:      return "AND";
        case TokenType::OR:       return "OR";
        case TokenType::NOT:      return "NOT";
        case TokenType::ASSIGN:   return "ASSIGN";
        // 分隔符
        case TokenType::LPAREN:    return "LPAREN";
        case TokenType::RPAREN:    return "RPAREN";
        case TokenType::LBRACE:    return "LBRACE";
        case TokenType::RBRACE:    return "RBRACE";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::COMMA:     return "COMMA";
        // 字面量 / 标识符
        case TokenType::ID:     return "ID";
        case TokenType::NUMBER: return "NUMBER";
        // 结束
        case TokenType::END: return "END";
        default:              return "UNKNOWN";
    }
}

}  // namespace toyc

// yylval 在全局作用域定义（供 Flex/Bison 链接）
extern toyc::LexerValue yylval;