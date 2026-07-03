#pragma once

#include <optional>
#include <string>
#include <vector>

#include "toyc/semantic/symbol_table.h"

namespace toyc {

struct SourceLocation;
class CompUnit;

/// 表达式类型（用于语义检查）
enum class ExprType { INT, VOID };

/// 语义分析器：遍历 AST，构建符号表，执行全部语义检查
class SemanticAnalyzer {
public:
    /// 执行语义分析，返回是否有错误
    bool analyze(CompUnit* ast);

    /// 获取分析后的符号表（供 IR Builder 使用）
    SymbolTable& symbolTable() { return symTable_; }

    /// 获取所有错误信息
    const std::vector<std::string>& errors() const { return errors_; }

private:
    SymbolTable symTable_;
    std::vector<std::string> errors_;
    bool hasError_ = false;

    void error(const SourceLocation& loc, const char* msg);
    void error(const SourceLocation& loc, const std::string& msg);

    // ---- 声明访问 ----
    void visitDecl(void* node);
    void visitVarDecl(void* decl);
    void visitConstDecl(void* decl);
    void visitFuncDef(void* func);

    // ---- 语句访问 ----
    void visitStmt(void* stmt);
    void visitBlockStmt(void* stmt);
    void visitIfStmt(void* stmt);
    void visitWhileStmt(void* stmt);
    void visitReturnStmt(void* stmt);
    void visitBreakStmt(void* stmt);
    void visitContinueStmt(void* stmt);
    void visitExprStmt(void* stmt);
    void visitAssignStmt(void* stmt);
    void visitDeclStmt(void* stmt);

    // ---- 表达式访问（返回表达式类型）----
    ExprType visitExpr(void* expr);
    ExprType visitBinaryExpr(void* expr);
    ExprType visitUnaryExpr(void* expr);
    ExprType visitCallExpr(void* expr);
    ExprType visitIdExpr(void* expr);
    ExprType visitNumberExpr(void* expr);

    // ---- 辅助方法 ----
    bool checkReturnOnAllPaths(void* block);
    bool checkIfReturnsOnAllPaths(void* ifStmt);
    std::optional<int> evalConstExpr(void* expr);
};

}  // namespace toyc
