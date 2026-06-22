#pragma once

#include <memory>
#include <string>
#include <vector>

#include "toyc/lexer/token.h"

namespace toyc {

/// AST 节点类型枚举
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

/// ToyC 类型系统
enum class Type { INT, VOID };

/// AST 节点基类
class ASTNode {
public:
    SourceLocation loc;

    explicit ASTNode(SourceLocation loc) : loc(loc) {}
    virtual ~ASTNode() = default;
    virtual NodeKind kind() const = 0;
};

/// 表达式节点基类
class ExprNode : public ASTNode {
public:
    using ASTNode::ASTNode;
};

/// 数字字面量
class NumberExpr : public ExprNode {
public:
    int value;

    NumberExpr(SourceLocation loc, int value) : ExprNode(loc), value(value) {}
    NodeKind kind() const override { return NodeKind::NumberExpr; }
};

/// 标识符引用
class IdExpr : public ExprNode {
public:
    std::string name;

    IdExpr(SourceLocation loc, std::string name) : ExprNode(loc), name(std::move(name)) {}
    NodeKind kind() const override { return NodeKind::IdExpr; }
};

}  // namespace toyc