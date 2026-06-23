#pragma once

#include <cstdlib>
#include <string>
#include <vector>

namespace toyc {

class ASTNode;
class ExprNode;
class CompUnit;
class BlockStmt;
class Param;

enum class TokenType : int {
    INT = 258,
    VOID,
    CONST,
    IF,
    ELSE,
    WHILE,
    BREAK,
    CONTINUE,
    RETURN,
    PLUS,
    MINUS,
    STAR,
    SLASH,
    PERCENT,
    LT,
    GT,
    LE,
    GE,
    EQ,
    NE,
    AND,
    OR,
    NOT,
    ASSIGN,
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    SEMICOLON,
    COMMA,
    ID,
    NUMBER,
    END = 0,
};

struct SourceLocation {
    int line = 1;
    int column = 1;
};

struct Token {
    TokenType type = TokenType::END;
    std::string lexeme;
    int value = 0;
    SourceLocation loc;
};

struct LexerValue {
    int intVal = 0;
    char* strVal = nullptr;
    ASTNode* node = nullptr;
    ExprNode* expr = nullptr;
    CompUnit* compUnit = nullptr;
    BlockStmt* block = nullptr;
    Param* param = nullptr;
    std::vector<ASTNode*>* nodeList = nullptr;
    std::vector<Param*>* paramList = nullptr;
    std::vector<ExprNode*>* exprList = nullptr;
    int typeTag = 0;
};

inline const char* tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::INT: return "INT";
        case TokenType::VOID: return "VOID";
        case TokenType::CONST: return "CONST";
        case TokenType::IF: return "IF";
        case TokenType::ELSE: return "ELSE";
        case TokenType::WHILE: return "WHILE";
        case TokenType::BREAK: return "BREAK";
        case TokenType::CONTINUE: return "CONTINUE";
        case TokenType::RETURN: return "RETURN";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::STAR: return "STAR";
        case TokenType::SLASH: return "SLASH";
        case TokenType::PERCENT: return "PERCENT";
        case TokenType::LT: return "LT";
        case TokenType::GT: return "GT";
        case TokenType::LE: return "LE";
        case TokenType::GE: return "GE";
        case TokenType::EQ: return "EQ";
        case TokenType::NE: return "NE";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::NOT: return "NOT";
        case TokenType::ASSIGN: return "ASSIGN";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::COMMA: return "COMMA";
        case TokenType::ID: return "ID";
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::END: return "END";
        default: return "UNKNOWN";
    }
}

}  // namespace toyc

extern toyc::LexerValue parser_yylval;
#define yylval parser_yylval
