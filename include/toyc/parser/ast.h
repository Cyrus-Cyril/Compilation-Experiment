#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "toyc/lexer/token.h"

namespace toyc {

enum class NodeKind {
    CompUnit,
    VarDecl,
    ConstDecl,
    FuncDef,
    Param,
    BlockStmt,
    IfStmt,
    WhileStmt,
    ReturnStmt,
    BreakStmt,
    ContinueStmt,
    ExprStmt,
    AssignStmt,
    NullStmt,
    DeclStmt,
    BinaryExpr,
    UnaryExpr,
    CallExpr,
    IdExpr,
    NumberExpr,
};

enum class Type { INT, VOID };

enum class BinaryOp {
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
    LT,
    GT,
    LE,
    GE,
    EQ,
    NE,
    AND,
    OR,
};

enum class UnaryOp { POS, NEG, NOT };

class ASTNode {
public:
    SourceLocation loc;

    explicit ASTNode(SourceLocation loc) : loc(loc) {}
    virtual ~ASTNode() = default;
    virtual NodeKind kind() const = 0;
};

class ExprNode : public ASTNode {
public:
    using ASTNode::ASTNode;
};

class DeclNode : public ASTNode {
public:
    using ASTNode::ASTNode;
};

class StmtNode : public ASTNode {
public:
    using ASTNode::ASTNode;
};

class CompUnit : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> elements;

    explicit CompUnit(SourceLocation loc = {}) : ASTNode(loc) {}
    NodeKind kind() const override { return NodeKind::CompUnit; }
};

class VarDecl : public DeclNode {
public:
    std::string name;
    std::unique_ptr<ExprNode> initExpr;
    bool isGlobal = false;

    VarDecl(SourceLocation loc, std::string name, std::unique_ptr<ExprNode> initExpr, bool isGlobal = false)
        : DeclNode(loc), name(std::move(name)), initExpr(std::move(initExpr)), isGlobal(isGlobal) {}
    NodeKind kind() const override { return NodeKind::VarDecl; }
};

class ConstDecl : public DeclNode {
public:
    std::string name;
    std::unique_ptr<ExprNode> initExpr;
    bool isGlobal = false;

    ConstDecl(SourceLocation loc, std::string name, std::unique_ptr<ExprNode> initExpr, bool isGlobal = false)
        : DeclNode(loc), name(std::move(name)), initExpr(std::move(initExpr)), isGlobal(isGlobal) {}
    NodeKind kind() const override { return NodeKind::ConstDecl; }
};

class Param : public ASTNode {
public:
    std::string name;

    Param(SourceLocation loc, std::string name) : ASTNode(loc), name(std::move(name)) {}
    NodeKind kind() const override { return NodeKind::Param; }
};

class BlockStmt : public StmtNode {
public:
    std::vector<std::unique_ptr<ASTNode>> stmts;

    explicit BlockStmt(SourceLocation loc = {}) : StmtNode(loc) {}
    NodeKind kind() const override { return NodeKind::BlockStmt; }
};

class FuncDef : public ASTNode {
public:
    std::string name;
    Type returnType = Type::INT;
    std::vector<std::unique_ptr<Param>> params;
    std::unique_ptr<BlockStmt> body;

    FuncDef(SourceLocation loc, std::string name, Type returnType,
            std::vector<std::unique_ptr<Param>> params, std::unique_ptr<BlockStmt> body)
        : ASTNode(loc),
          name(std::move(name)),
          returnType(returnType),
          params(std::move(params)),
          body(std::move(body)) {}
    NodeKind kind() const override { return NodeKind::FuncDef; }
};

class IfStmt : public StmtNode {
public:
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<ASTNode> thenStmt;
    std::unique_ptr<ASTNode> elseStmt;

    IfStmt(SourceLocation loc, std::unique_ptr<ExprNode> condition,
           std::unique_ptr<ASTNode> thenStmt, std::unique_ptr<ASTNode> elseStmt = nullptr)
        : StmtNode(loc),
          condition(std::move(condition)),
          thenStmt(std::move(thenStmt)),
          elseStmt(std::move(elseStmt)) {}
    NodeKind kind() const override { return NodeKind::IfStmt; }
};

class WhileStmt : public StmtNode {
public:
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<ASTNode> body;

    WhileStmt(SourceLocation loc, std::unique_ptr<ExprNode> condition, std::unique_ptr<ASTNode> body)
        : StmtNode(loc), condition(std::move(condition)), body(std::move(body)) {}
    NodeKind kind() const override { return NodeKind::WhileStmt; }
};

class ReturnStmt : public StmtNode {
public:
    std::unique_ptr<ExprNode> value;

    ReturnStmt(SourceLocation loc, std::unique_ptr<ExprNode> value = nullptr)
        : StmtNode(loc), value(std::move(value)) {}
    NodeKind kind() const override { return NodeKind::ReturnStmt; }
};

class BreakStmt : public StmtNode {
public:
    explicit BreakStmt(SourceLocation loc = {}) : StmtNode(loc) {}
    NodeKind kind() const override { return NodeKind::BreakStmt; }
};

class ContinueStmt : public StmtNode {
public:
    explicit ContinueStmt(SourceLocation loc = {}) : StmtNode(loc) {}
    NodeKind kind() const override { return NodeKind::ContinueStmt; }
};

class ExprStmt : public StmtNode {
public:
    std::unique_ptr<ExprNode> expr;

    ExprStmt(SourceLocation loc, std::unique_ptr<ExprNode> expr) : StmtNode(loc), expr(std::move(expr)) {}
    NodeKind kind() const override { return NodeKind::ExprStmt; }
};

class AssignStmt : public StmtNode {
public:
    std::string name;
    std::unique_ptr<ExprNode> value;

    AssignStmt(SourceLocation loc, std::string name, std::unique_ptr<ExprNode> value)
        : StmtNode(loc), name(std::move(name)), value(std::move(value)) {}
    NodeKind kind() const override { return NodeKind::AssignStmt; }
};

class NullStmt : public StmtNode {
public:
    explicit NullStmt(SourceLocation loc = {}) : StmtNode(loc) {}
    NodeKind kind() const override { return NodeKind::NullStmt; }
};

class DeclStmt : public StmtNode {
public:
    std::unique_ptr<ASTNode> decl;

    DeclStmt(SourceLocation loc, std::unique_ptr<ASTNode> decl) : StmtNode(loc), decl(std::move(decl)) {}
    NodeKind kind() const override { return NodeKind::DeclStmt; }
};

class BinaryExpr : public ExprNode {
public:
    BinaryOp op;
    std::unique_ptr<ExprNode> left;
    std::unique_ptr<ExprNode> right;

    BinaryExpr(SourceLocation loc, BinaryOp op, std::unique_ptr<ExprNode> left, std::unique_ptr<ExprNode> right)
        : ExprNode(loc), op(op), left(std::move(left)), right(std::move(right)) {}
    NodeKind kind() const override { return NodeKind::BinaryExpr; }
};

class UnaryExpr : public ExprNode {
public:
    UnaryOp op;
    std::unique_ptr<ExprNode> operand;

    UnaryExpr(SourceLocation loc, UnaryOp op, std::unique_ptr<ExprNode> operand)
        : ExprNode(loc), op(op), operand(std::move(operand)) {}
    NodeKind kind() const override { return NodeKind::UnaryExpr; }
};

class CallExpr : public ExprNode {
public:
    std::string funcName;
    std::vector<std::unique_ptr<ExprNode>> args;

    CallExpr(SourceLocation loc, std::string funcName, std::vector<std::unique_ptr<ExprNode>> args)
        : ExprNode(loc), funcName(std::move(funcName)), args(std::move(args)) {}
    NodeKind kind() const override { return NodeKind::CallExpr; }
};

class NumberExpr : public ExprNode {
public:
    int value;

    NumberExpr(SourceLocation loc, int value) : ExprNode(loc), value(value) {}
    NodeKind kind() const override { return NodeKind::NumberExpr; }
};

class IdExpr : public ExprNode {
public:
    std::string name;

    IdExpr(SourceLocation loc, std::string name) : ExprNode(loc), name(std::move(name)) {}
    NodeKind kind() const override { return NodeKind::IdExpr; }
};

}  // namespace toyc
