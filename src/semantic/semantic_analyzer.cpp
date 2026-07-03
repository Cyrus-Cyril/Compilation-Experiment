// ToyC Semantic Analyzer 实现
// 遍历 AST，构建符号表，执行全部语义检查

#include "toyc/semantic/semantic_analyzer.h"
#include "toyc/semantic/symbol_table.h"

#include "toyc/parser/ast.h"
#include "toyc/parser/parser_api.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace toyc {

// ============================================================
// 公开接口
// ============================================================

bool SemanticAnalyzer::analyze(CompUnit* ast) {
    hasError_ = false;
    errors_.clear();
    symTable_.enterScope();  // 全局作用域

    for (const auto& elem : ast->elements) {
        switch (elem->kind()) {
            case NodeKind::VarDecl:
                visitVarDecl(static_cast<VarDecl*>(elem.get()));
                break;
            case NodeKind::ConstDecl:
                visitConstDecl(static_cast<ConstDecl*>(elem.get()));
                break;
            case NodeKind::FuncDef: {
                visitFuncDef(static_cast<FuncDef*>(elem.get()));
                break;
            }
            default:
                break;
        }
    }

    // 检查 main 函数
    Symbol* mainSym = symTable_.lookupGlobalScope("main");
    if (!mainSym || mainSym->kind != SymbolKind::Function) {
        error(SourceLocation{}, "program must contain a 'main' function");
    } else {
        if (!mainSym->paramTypes.empty()) {
            error(mainSym->declLoc, "'main' function must take no arguments");
        }
        if (mainSym->returnType != Type::INT) {
            error(mainSym->declLoc, "'main' function must return 'int'");
        }
    }

    return hasError_;
}

// ============================================================
// 错误报告
// ============================================================

void SemanticAnalyzer::error(const SourceLocation& loc, const char* msg) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "Error(%d:%d): %s", loc.line, loc.column, msg);
    errors_.push_back(buf);
    std::fprintf(stderr, "%s\n", buf);
    hasError_ = true;
}

void SemanticAnalyzer::error(const SourceLocation& loc, const std::string& msg) {
    error(loc, msg.c_str());
}

// ============================================================
// 函数定义处理
// ============================================================

void SemanticAnalyzer::visitFuncDef(void* func) {
    auto* f = static_cast<FuncDef*>(func);

    if (symTable_.lookupCurrentScope(f->name)) {
        error(f->loc, "redefinition of '" + f->name + "'");
        return;
    }

    Symbol funcSym;
    funcSym.name = f->name;
    funcSym.kind = SymbolKind::Function;
    funcSym.declLoc = f->loc;
    funcSym.returnType = f->returnType;
    for (const auto& p : f->params) {
        funcSym.paramTypes.push_back(Type::INT);
    }
    symTable_.insert(funcSym);

    symTable_.enterScope();
    symTable_.setCurrentFunction(symTable_.lookup(f->name));

    for (size_t i = 0; i < f->params.size(); ++i) {
        Param* p = f->params[i].get();
        if (symTable_.lookupCurrentScope(p->name)) {
            error(p->loc, "redefinition of parameter '" + p->name + "'");
            continue;
        }
        Symbol paramSym;
        paramSym.name = p->name;
        paramSym.kind = SymbolKind::Parameter;
        paramSym.declLoc = p->loc;
        paramSym.paramIndex = static_cast<int>(i);
        symTable_.insert(paramSym);
    }

    // 直接遍历函数体语句（不通过 visitBlockStmt，避免额外 enterScope
    // 导致参数和局部变量在不同作用域中）
    for (const auto& stmt : f->body->stmts) {
        visitStmt(stmt.get());
    }

    if (f->returnType == Type::INT && !checkReturnOnAllPaths(f->body.get())) {
        error(f->loc,
              "non-void function '" + f->name + "' does not return a value on all control paths");
    }

    symTable_.setCurrentFunction(nullptr);
    symTable_.exitScope();
}

// ============================================================
// 声明访问
// ============================================================

void SemanticAnalyzer::visitDecl(void* node) {
    auto* n = static_cast<ASTNode*>(node);
    if (n->kind() == NodeKind::VarDecl) {
        visitVarDecl(static_cast<VarDecl*>(n));
    } else if (n->kind() == NodeKind::ConstDecl) {
        visitConstDecl(static_cast<ConstDecl*>(n));
    }
}

void SemanticAnalyzer::visitVarDecl(void* decl) {
    auto* d = static_cast<VarDecl*>(decl);
    if (symTable_.lookupCurrentScope(d->name)) {
        error(d->loc, "redefinition of variable '" + d->name + "'");
        return;
    }
    visitExpr(d->initExpr.get());

    Symbol varSym;
    varSym.name = d->name;
    varSym.kind = SymbolKind::Variable;
    varSym.declLoc = d->loc;
    varSym.isGlobalVar = d->isGlobal;
    symTable_.insert(varSym);
}

void SemanticAnalyzer::visitConstDecl(void* decl) {
    auto* d = static_cast<ConstDecl*>(decl);
    if (symTable_.lookupCurrentScope(d->name)) {
        error(d->loc, "redefinition of constant '" + d->name + "'");
        return;
    }

    auto val = evalConstExpr(d->initExpr.get());
    if (!val.has_value()) {
        error(d->loc,
              "initializer for const '" + d->name + "' must be a compile-time constant expression");
    }

    Symbol constSym;
    constSym.name = d->name;
    constSym.kind = SymbolKind::Constant;
    constSym.declLoc = d->loc;
    constSym.isGlobalConst = d->isGlobal;
    constSym.constValue = val.value_or(0);
    symTable_.insert(constSym);
}

// ============================================================
// 语句访问
// ============================================================

void SemanticAnalyzer::visitStmt(void* stmt) {
    auto* s = static_cast<ASTNode*>(stmt);
    switch (s->kind()) {
        case NodeKind::BlockStmt:     visitBlockStmt(static_cast<BlockStmt*>(s)); break;
        case NodeKind::IfStmt:        visitIfStmt(static_cast<IfStmt*>(s)); break;
        case NodeKind::WhileStmt:     visitWhileStmt(static_cast<WhileStmt*>(s)); break;
        case NodeKind::ReturnStmt:    visitReturnStmt(static_cast<ReturnStmt*>(s)); break;
        case NodeKind::BreakStmt:     visitBreakStmt(static_cast<BreakStmt*>(s)); break;
        case NodeKind::ContinueStmt:  visitContinueStmt(static_cast<ContinueStmt*>(s)); break;
        case NodeKind::ExprStmt:      visitExprStmt(static_cast<ExprStmt*>(s)); break;
        case NodeKind::AssignStmt:    visitAssignStmt(static_cast<AssignStmt*>(s)); break;
        case NodeKind::DeclStmt:      visitDeclStmt(static_cast<DeclStmt*>(s)); break;
        case NodeKind::NullStmt:      break;
        default: break;
    }
}

void SemanticAnalyzer::visitBlockStmt(void* stmt) {
    auto* s = static_cast<BlockStmt*>(stmt);
    symTable_.enterScope();
    for (const auto& item : s->stmts) {
        visitStmt(item.get());
    }
    symTable_.exitScope();
}

void SemanticAnalyzer::visitIfStmt(void* stmt) {
    auto* s = static_cast<IfStmt*>(stmt);
    ExprType condType = visitExpr(s->condition.get());
    if (condType == ExprType::VOID) {
        error(s->condition->loc, "void function call cannot be used as condition");
    }
    visitStmt(s->thenStmt.get());
    if (s->elseStmt) {
        visitStmt(s->elseStmt.get());
    }
}

void SemanticAnalyzer::visitWhileStmt(void* stmt) {
    auto* s = static_cast<WhileStmt*>(stmt);
    ExprType condType = visitExpr(s->condition.get());
    if (condType == ExprType::VOID) {
        error(s->condition->loc, "void function call cannot be used as condition");
    }
    symTable_.enterLoop();
    visitStmt(s->body.get());
    symTable_.leaveLoop();
}

void SemanticAnalyzer::visitReturnStmt(void* stmt) {
    auto* s = static_cast<ReturnStmt*>(stmt);
    Symbol* func = symTable_.currentFunction();
    if (!func) return;

    if (s->value) {
        if (func->returnType == Type::VOID) {
            error(s->loc, "void function cannot return a value");
        }
        visitExpr(s->value.get());
    } else {
        if (func->returnType == Type::INT) {
            error(s->loc, "non-void function must return a value");
        }
    }
}

void SemanticAnalyzer::visitBreakStmt(void* stmt) {
    auto* s = static_cast<BreakStmt*>(stmt);
    if (symTable_.loopDepth() == 0) {
        error(s->loc, "'break' statement not within a loop");
    }
}

void SemanticAnalyzer::visitContinueStmt(void* stmt) {
    auto* s = static_cast<ContinueStmt*>(stmt);
    if (symTable_.loopDepth() == 0) {
        error(s->loc, "'continue' statement not within a loop");
    }
}

void SemanticAnalyzer::visitExprStmt(void* stmt) {
    auto* s = static_cast<ExprStmt*>(stmt);
    visitExpr(s->expr.get());
}

void SemanticAnalyzer::visitAssignStmt(void* stmt) {
    auto* s = static_cast<AssignStmt*>(stmt);
    Symbol* sym = symTable_.lookup(s->name);
    if (!sym) {
        error(s->loc, "undefined variable '" + s->name + "'");
        visitExpr(s->value.get());
        return;
    }
    if (sym->kind == SymbolKind::Constant) {
        error(s->loc, "cannot assign to constant '" + s->name + "'");
    }
    if (sym->kind == SymbolKind::Function) {
        error(s->loc, "cannot assign to function '" + s->name + "'");
    }
    ExprType rtype = visitExpr(s->value.get());
    if (rtype == ExprType::VOID) {
        error(s->value->loc, "void function call cannot be used as rvalue in assignment");
    }
}

void SemanticAnalyzer::visitDeclStmt(void* stmt) {
    auto* s = static_cast<DeclStmt*>(stmt);
    visitDecl(s->decl.get());
}

// ============================================================
// 表达式访问 —— 返回表达式类型
// ============================================================

ExprType SemanticAnalyzer::visitExpr(void* expr) {
    auto* e = static_cast<ExprNode*>(expr);
    switch (e->kind()) {
        case NodeKind::BinaryExpr:  return visitBinaryExpr(static_cast<BinaryExpr*>(e));
        case NodeKind::UnaryExpr:   return visitUnaryExpr(static_cast<UnaryExpr*>(e));
        case NodeKind::CallExpr:    return visitCallExpr(static_cast<CallExpr*>(e));
        case NodeKind::IdExpr:      return visitIdExpr(static_cast<IdExpr*>(e));
        case NodeKind::NumberExpr:  return visitNumberExpr(static_cast<NumberExpr*>(e));
        default:                    return ExprType::INT;
    }
}

ExprType SemanticAnalyzer::visitBinaryExpr(void* expr) {
    auto* e = static_cast<BinaryExpr*>(expr);
    ExprType leftType = visitExpr(e->left.get());
    ExprType rightType = visitExpr(e->right.get());

    if ((e->op == BinaryOp::DIV || e->op == BinaryOp::MOD)) {
        if (auto* numRight = dynamic_cast<NumberExpr*>(e->right.get())) {
            if (numRight->value == 0) {
                error(e->right->loc, "division by zero");
            }
        }
    }
    return ExprType::INT;
}

ExprType SemanticAnalyzer::visitUnaryExpr(void* expr) {
    auto* e = static_cast<UnaryExpr*>(expr);
    visitExpr(e->operand.get());
    return ExprType::INT;
}

ExprType SemanticAnalyzer::visitCallExpr(void* expr) {
    auto* e = static_cast<CallExpr*>(expr);
    Symbol* funcSym = symTable_.lookupGlobalScope(e->funcName);
    if (!funcSym) {
        error(e->loc, "undefined function '" + e->funcName + "'");
        return ExprType::INT;
    }
    if (funcSym->kind != SymbolKind::Function) {
        error(e->loc, "'" + e->funcName + "' is not a function");
        return ExprType::INT;
    }
    if (e->args.size() != funcSym->paramTypes.size()) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "function '%s' expects %zu argument(s), but %zu provided",
                      e->funcName.c_str(),
                      funcSym->paramTypes.size(),
                      e->args.size());
        error(e->loc, buf);
    }
    for (const auto& arg : e->args) {
        ExprType atype = visitExpr(arg.get());
        if (atype == ExprType::VOID) {
            error(arg->loc, "void function call cannot be used as function argument");
        }
    }
    return funcSym->returnType == Type::VOID ? ExprType::VOID : ExprType::INT;
}

ExprType SemanticAnalyzer::visitIdExpr(void* expr) {
    auto* e = static_cast<IdExpr*>(expr);
    Symbol* sym = symTable_.lookup(e->name);
    if (!sym) {
        error(e->loc, "undefined identifier '" + e->name + "'");
        return ExprType::INT;
    }
    if (sym->kind == SymbolKind::Function) {
        error(e->loc, "expected expression, found function name '" + e->name + "'");
        return ExprType::INT;
    }
    return ExprType::INT;
}

ExprType SemanticAnalyzer::visitNumberExpr(void*) {
    return ExprType::INT;
}

// ============================================================
// 辅助方法
// ============================================================

bool SemanticAnalyzer::checkStmtReturns(void* stmt) {
    auto* s = static_cast<ASTNode*>(stmt);

    if (s->kind() == NodeKind::ReturnStmt) {
        return true;
    }

    if (s->kind() == NodeKind::BlockStmt) {
        return checkReturnOnAllPaths(static_cast<BlockStmt*>(s));
    }

    if (s->kind() == NodeKind::IfStmt) {
        return checkIfReturnsOnAllPaths(s);
    }

    return false;
}

bool SemanticAnalyzer::checkReturnOnAllPaths(void* block) {
    auto* b = static_cast<BlockStmt*>(block);
    if (b->stmts.empty()) return false;

    ASTNode* last = b->stmts.back().get();
    return checkStmtReturns(last);
}

bool SemanticAnalyzer::checkIfReturnsOnAllPaths(void* ifStmt) {
    auto* s = static_cast<IfStmt*>(ifStmt);
    if (!s->elseStmt) return false;

    bool thenReturns = false;
    bool elseReturns = false;

    // 显式处理 thenStmt 的所有情况
    if (s->thenStmt->kind() == NodeKind::BlockStmt) {
        thenReturns = checkReturnOnAllPaths(static_cast<BlockStmt*>(s->thenStmt.get()));
    } else if (s->thenStmt->kind() == NodeKind::ReturnStmt) {
        thenReturns = true;
    } else if (s->thenStmt->kind() == NodeKind::IfStmt) {
        thenReturns = checkIfReturnsOnAllPaths(s->thenStmt.get());
    } else {
        thenReturns = checkStmtReturns(s->thenStmt.get());
    }

    // 显式处理 elseStmt 的所有情况
    if (s->elseStmt->kind() == NodeKind::BlockStmt) {
        elseReturns = checkReturnOnAllPaths(static_cast<BlockStmt*>(s->elseStmt.get()));
    } else if (s->elseStmt->kind() == NodeKind::ReturnStmt) {
        elseReturns = true;
    } else if (s->elseStmt->kind() == NodeKind::IfStmt) {
        elseReturns = checkIfReturnsOnAllPaths(s->elseStmt.get());
    } else {
        elseReturns = checkStmtReturns(s->elseStmt.get());
    }

    return thenReturns && elseReturns;
}

std::optional<int> SemanticAnalyzer::evalConstExpr(void* expr) {
    auto* e = static_cast<ExprNode*>(expr);

    if (auto* num = dynamic_cast<NumberExpr*>(e)) {
        return num->value;
    }

    if (auto* id = dynamic_cast<IdExpr*>(e)) {
        Symbol* sym = symTable_.lookup(id->name);
        if (!sym || sym->kind != SymbolKind::Constant) return std::nullopt;
        return sym->constValue;
    }

    if (auto* bin = dynamic_cast<BinaryExpr*>(e)) {
        auto left = evalConstExpr(bin->left.get());
        auto right = evalConstExpr(bin->right.get());
        if (!left || !right) return std::nullopt;

        switch (bin->op) {
            case BinaryOp::ADD: return *left + *right;
            case BinaryOp::SUB: return *left - *right;
            case BinaryOp::MUL: return *left * *right;
            case BinaryOp::DIV:  return (*right != 0) ? std::optional<int>(*left / *right) : std::nullopt;
            case BinaryOp::MOD:  return (*right != 0) ? std::optional<int>(*left % *right) : std::nullopt;
            case BinaryOp::LT:   return *left < *right ? 1 : 0;
            case BinaryOp::GT:   return *left > *right ? 1 : 0;
            case BinaryOp::LE:   return *left <= *right ? 1 : 0;
            case BinaryOp::GE:   return *left >= *right ? 1 : 0;
            case BinaryOp::EQ:   return *left == *right ? 1 : 0;
            case BinaryOp::NE:   return *left != *right ? 1 : 0;
            case BinaryOp::AND:  return (*left && *right) ? 1 : 0;
            case BinaryOp::OR:   return (*left || *right) ? 1 : 0;
            default:             return std::nullopt;
        }
    }

    if (auto* unary = dynamic_cast<UnaryExpr*>(e)) {
        auto operand = evalConstExpr(unary->operand.get());
        if (!operand) return std::nullopt;
        switch (unary->op) {
            case UnaryOp::POS: return *operand;
            case UnaryOp::NEG: return -*operand;
            case UnaryOp::NOT: return *operand ? 0 : 1;
        }
    }

    return std::nullopt;
}

}  // namespace toyc
