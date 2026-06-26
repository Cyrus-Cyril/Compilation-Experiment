// ToyC IR Builder 实现
// 遍历 Annotated AST + SymbolTable，生成三地址码 (TAC) IR

#include "toyc/ir/ir_builder.h"
#include "toyc/ir/ir.h"

#include "toyc/parser/ast.h"
#include "toyc/semantic/symbol_table.h"

#include <cstdint>
#include <memory>

namespace toyc {

// ============================================================
// IRBuilder 内部实现（静态方法的辅助类）
// ============================================================

class IRBuilderImpl {
public:
    /// 从 AST 和符号表构建完整 IR 程序
    IRProgram build(CompUnit* ast, SymbolTable* symTable);

private:
    SymbolTable* symTable_ = nullptr;
    IRProgram program_;
    IRFunction* currentFunc_ = nullptr;

    // 虚拟寄存器和标签计数器
    uint32_t nextReg_ = 0;
    uint32_t nextLabel_ = 0;

    // 局部变量栈帧偏移分配器（每个函数从 0 开始递增）
    int nextStackOffset_ = 0;

    // break/continue 标签栈（支持嵌套循环）
    struct LoopLabels {
        uint32_t breakLabel;
        uint32_t continueLabel;
    };
    std::vector<LoopLabels> loopStack_;

    // ---- 寄存器 / 标签分配 ----
    uint32_t newReg();
    uint32_t newLabel();

    // ---- 指令发射辅助 ----
    void emit(IROpcode op, std::vector<IROperand> operands);
    void emitLabel(uint32_t label);

    // ---- 表达式 IR 生成，返回存放结果的虚拟寄存器 ----
    uint32_t genExpr(ExprNode* expr);
    uint32_t genBinaryExpr(BinaryExpr* expr);
    uint32_t genUnaryExpr(UnaryExpr* expr);
    uint32_t genCallExpr(CallExpr* expr);
    uint32_t genIdExpr(IdExpr* expr);
    uint32_t genNumberExpr(NumberExpr* expr);

    /// 短路求值：为 && / || 生成条件跳转 IR
    void genCondExpr(ExprNode* expr, uint32_t trueLabel, uint32_t falseLabel);
    void genCondBinary(BinaryExpr* expr, uint32_t trueLabel, uint32_t falseLabel);

    // ---- 语句 IR 生成 ----
    void genStmt(ASTNode* stmt);
    void genBlockStmt(BlockStmt* stmt);
    void genIfStmt(IfStmt* stmt);
    void genWhileStmt(WhileStmt* stmt);
    void genReturnStmt(ReturnStmt* stmt);
    void genBreakStmt(BreakStmt* /*stmt*/);
    void genContinueStmt(ContinueStmt* /*stmt*/);
    void genExprStmt(ExprStmt* stmt);
    void genAssignStmt(AssignStmt* stmt);
    void genDeclStmt(DeclStmt* stmt);

    // ---- 声明 IR 生成 ----
    void genDecl(ASTNode* decl);
    void genVarDecl(VarDecl* decl);
    void genConstDecl(ConstDecl* decl);
    int32_t evalGlobalInit(ExprNode* expr);

    // ---- 函数 IR 生成 ----
    void genFunc(FuncDef* func);

    // ---- 辅助：获取变量的栈帧偏移或标记为全局 ----
    int getVarOffset(const std::string& name, bool& isGlobal);
    int allocStackOffset();

    // ---- 作用域管理（与语义分析器同步） ----
    void enterScope();
    void exitScope();
};

// ============================================================
// 公开接口（静态方法委托给实现类）
// ============================================================

IRProgram IRBuilder::build(CompUnit* ast, SymbolTable* symTable) {
    return IRBuilderImpl{}.build(ast, symTable);
}

// ============================================================
// 实现类：公开接口
// ============================================================

IRProgram IRBuilderImpl::build(CompUnit* ast, SymbolTable* symTable) {
    program_ = IRProgram{};
    symTable_ = symTable;
    nextReg_ = 0;
    nextLabel_ = 0;

    // 第一遍：收集全局声明并分配全局变量名
    for (const auto& elem : ast->elements) {
        if (elem->kind() == NodeKind::VarDecl) {
            auto* vd = static_cast<VarDecl*>(elem.get());
            program_.globals.push_back({vd->name, false, evalGlobalInit(vd->initExpr.get())});
            program_.globalNames.push_back(vd->name);
        } else if (elem->kind() == NodeKind::ConstDecl) {
            auto* cd = static_cast<ConstDecl*>(elem.get());
            program_.globals.push_back({cd->name, true, evalGlobalInit(cd->initExpr.get())});
            program_.globalNames.push_back(cd->name);
        }
    }

    // 第二遍：生成各函数的 IR
    for (const auto& elem : ast->elements) {
        if (elem->kind() == NodeKind::FuncDef) {
            genFunc(static_cast<FuncDef*>(elem.get()));
        }
    }

    return program_;
}

// ============================================================
// 资源分配
// ============================================================

uint32_t IRBuilderImpl::newReg() { return nextReg_++; }

uint32_t IRBuilderImpl::newLabel() { return nextLabel_++; }

int IRBuilderImpl::allocStackOffset() {
    int offset = nextStackOffset_;
    nextStackOffset_ += 4;  // 每个 int 变量占 4 字节
    return offset;
}

// ============================================================
// 指令发射
// ============================================================

void IRBuilderImpl::emit(IROpcode op, std::vector<IROperand> operands) {
    currentFunc_->instructions.push_back({op, std::move(operands)});
}

void IRBuilderImpl::emitLabel(uint32_t label) {
    emit(IROpcode::LABEL, {IROperand::label(label)});
}

// ============================================================
// 表达式 IR 生成 —— 返回存放结果的虚拟寄存器编号
// ============================================================

uint32_t IRBuilderImpl::genExpr(ExprNode* expr) {
    switch (expr->kind()) {
        case NodeKind::BinaryExpr:  return genBinaryExpr(static_cast<BinaryExpr*>(expr));
        case NodeKind::UnaryExpr:   return genUnaryExpr(static_cast<UnaryExpr*>(expr));
        case NodeKind::CallExpr:    return genCallExpr(static_cast<CallExpr*>(expr));
        case NodeKind::IdExpr:      return genIdExpr(static_cast<IdExpr*>(expr));
        case NodeKind::NumberExpr:  return genNumberExpr(static_cast<NumberExpr*>(expr));
        default:                    return newReg();
    }
}

uint32_t IRBuilderImpl::genBinaryExpr(BinaryExpr* expr) {
    if (expr->op == BinaryOp::AND || expr->op == BinaryOp::OR) {
        uint32_t resultReg = newReg();
        uint32_t trueLbl = newLabel();
        uint32_t falseLbl = newLabel();
        uint32_t endLbl = newLabel();

        genCondBinary(expr, trueLbl, falseLbl);

        emitLabel(trueLbl);
        emit(IROpcode::ADD, {IROperand::reg(resultReg), IROperand::imm(0), IROperand::imm(1)});
        emit(IROpcode::JMP, {IROperand::label(endLbl)});

        emitLabel(falseLbl);
        emit(IROpcode::ADD, {IROperand::reg(resultReg), IROperand::imm(0), IROperand::imm(0)});

        emitLabel(endLbl);
        return resultReg;
    }

    uint32_t leftReg = genExpr(expr->left.get());
    uint32_t rightReg = genExpr(expr->right.get());
    uint32_t destReg = newReg();

    switch (expr->op) {
        case BinaryOp::ADD: emit(IROpcode::ADD, {IROperand::reg(destReg), IROperand::reg(leftReg), IROperand::reg(rightReg)}); break;
        case BinaryOp::SUB: emit(IROpcode::SUB, {IROperand::reg(destReg), IROperand::reg(leftReg), IROperand::reg(rightReg)}); break;
        case BinaryOp::MUL: emit(IROpcode::MUL, {IROperand::reg(destReg), IROperand::reg(leftReg), IROperand::reg(rightReg)}); break;
        case BinaryOp::DIV: emit(IROpcode::DIV, {IROperand::reg(destReg), IROperand::reg(leftReg), IROperand::reg(rightReg)}); break;
        case BinaryOp::MOD: emit(IROpcode::MOD, {IROperand::reg(destReg), IROperand::reg(leftReg), IROperand::reg(rightReg)}); break;
        case BinaryOp::LT:  emit(IROpcode::LT,  {IROperand::reg(destReg), IROperand::reg(leftReg), IROperand::reg(rightReg)}); break;
        case BinaryOp::GT:  emit(IROpcode::GT,  {IROperand::reg(destReg), IROperand::reg(leftReg), IROperand::reg(rightReg)}); break;
        case BinaryOp::LE:  emit(IROpcode::LE,  {IROperand::reg(destReg), IROperand::reg(leftReg), IROperand::reg(rightReg)}); break;
        case BinaryOp::GE:  emit(IROpcode::GE,  {IROperand::reg(destReg), IROperand::reg(leftReg), IROperand::reg(rightReg)}); break;
        case BinaryOp::EQ:  emit(IROpcode::EQ,  {IROperand::reg(destReg), IROperand::reg(leftReg), IROperand::reg(rightReg)}); break;
        case BinaryOp::NE:  emit(IROpcode::NE,  {IROperand::reg(destReg), IROperand::reg(leftReg), IROperand::reg(rightReg)}); break;
        default: break;
    }
    return destReg;
}

uint32_t IRBuilderImpl::genUnaryExpr(UnaryExpr* expr) {
    uint32_t operandReg = genExpr(expr->operand.get());
    uint32_t destReg = newReg();

    switch (expr->op) {
        case UnaryOp::POS:
            emit(IROpcode::ADD, {IROperand::reg(destReg), IROperand::reg(operandReg), IROperand::imm(0)});
            break;
        case UnaryOp::NEG:
            emit(IROpcode::NEG, {IROperand::reg(destReg), IROperand::reg(operandReg)});
            break;
        case UnaryOp::NOT:
            emit(IROpcode::NOT, {IROperand::reg(destReg), IROperand::reg(operandReg)});
            break;
    }
    return destReg;
}

uint32_t IRBuilderImpl::genCallExpr(CallExpr* expr) {
    for (const auto& arg : expr->args) {
        uint32_t argReg = genExpr(arg.get());
        emit(IROpcode::PARAM, {IROperand::reg(argReg)});
    }

    uint32_t destReg = newReg();
    emit(IROpcode::CALL, {IROperand::reg(destReg), IROperand::func(expr->funcName)});
    return destReg;
}

uint32_t IRBuilderImpl::genIdExpr(IdExpr* expr) {
    uint32_t destReg = newReg();
    bool isGlobal = false;
    int offset = getVarOffset(expr->name, isGlobal);

    if (isGlobal) {
        emit(IROpcode::LOAD_GLOBAL, {IROperand::reg(destReg), IROperand::global(expr->name)});
    } else {
        emit(IROpcode::LOAD_LOCAL, {IROperand::reg(destReg), IROperand::imm(offset)});
    }
    return destReg;
}

uint32_t IRBuilderImpl::genNumberExpr(NumberExpr* expr) {
    uint32_t destReg = newReg();
    emit(IROpcode::ADD, {IROperand::reg(destReg), IROperand::imm(0), IROperand::imm(expr->value)});
    return destReg;
}

// ============================================================
// 短路求值条件表达式生成
// ============================================================

void IRBuilderImpl::genCondExpr(ExprNode* expr, uint32_t trueLabel, uint32_t falseLabel) {
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        if (bin->op == BinaryOp::AND || bin->op == BinaryOp::OR) {
            genCondBinary(bin, trueLabel, falseLabel);
            return;
        }
    }

    uint32_t valReg = genExpr(expr);
    emit(IROpcode::BNE, {IROperand::reg(valReg), IROperand::imm(0), IROperand::label(trueLabel)});
    emit(IROpcode::JMP, {IROperand::label(falseLabel)});
}

void IRBuilderImpl::genCondBinary(BinaryExpr* expr, uint32_t trueLabel, uint32_t falseLabel) {
    if (expr->op == BinaryOp::AND) {
        uint32_t midFalse = newLabel();
        genCondExpr(expr->left.get(), trueLabel, midFalse);
        emitLabel(midFalse);
        genCondExpr(expr->right.get(), trueLabel, falseLabel);
    } else if (expr->op == BinaryOp::OR) {
        uint32_t midTrue = newLabel();
        genCondExpr(expr->left.get(), midTrue, falseLabel);
        emitLabel(midTrue);
        genCondExpr(expr->right.get(), trueLabel, falseLabel);
    }
}

// ============================================================
// 语句 IR 生成
// ============================================================

void IRBuilderImpl::genStmt(ASTNode* stmt) {
    switch (stmt->kind()) {
        case NodeKind::BlockStmt:     genBlockStmt(static_cast<BlockStmt*>(stmt)); break;
        case NodeKind::IfStmt:        genIfStmt(static_cast<IfStmt*>(stmt)); break;
        case NodeKind::WhileStmt:     genWhileStmt(static_cast<WhileStmt*>(stmt)); break;
        case NodeKind::ReturnStmt:    genReturnStmt(static_cast<ReturnStmt*>(stmt)); break;
        case NodeKind::BreakStmt:     genBreakStmt(static_cast<BreakStmt*>(stmt)); break;
        case NodeKind::ContinueStmt:  genContinueStmt(static_cast<ContinueStmt*>(stmt)); break;
        case NodeKind::ExprStmt:      genExprStmt(static_cast<ExprStmt*>(stmt)); break;
        case NodeKind::AssignStmt:    genAssignStmt(static_cast<AssignStmt*>(stmt)); break;
        case NodeKind::DeclStmt:      genDeclStmt(static_cast<DeclStmt*>(stmt)); break;
        case NodeKind::NullStmt:      break;
        default: break;
    }
}

void IRBuilderImpl::genBlockStmt(BlockStmt* stmt) {
    enterScope();
    for (const auto& s : stmt->stmts) {
        genStmt(s.get());
    }
    exitScope();
}

void IRBuilderImpl::genIfStmt(IfStmt* stmt) {
    uint32_t thenLabel = newLabel();
    uint32_t elseLabel = newLabel();
    uint32_t endLabel = newLabel();

    genCondExpr(stmt->condition.get(), thenLabel, elseLabel);

    emitLabel(thenLabel);
    genStmt(stmt->thenStmt.get());
    emit(IROpcode::JMP, {IROperand::label(endLabel)});

    emitLabel(elseLabel);
    if (stmt->elseStmt) {
        genStmt(stmt->elseStmt.get());
    }

    emitLabel(endLabel);
}

void IRBuilderImpl::genWhileStmt(WhileStmt* stmt) {
    uint32_t condLabel = newLabel();
    uint32_t bodyLabel = newLabel();
    uint32_t endLabel = newLabel();

    loopStack_.push_back({endLabel, condLabel});

    emitLabel(condLabel);
    genCondExpr(stmt->condition.get(), bodyLabel, endLabel);

    emitLabel(bodyLabel);
    genStmt(stmt->body.get());

    emit(IROpcode::JMP, {IROperand::label(condLabel)});

    emitLabel(endLabel);
    loopStack_.pop_back();
}

void IRBuilderImpl::genReturnStmt(ReturnStmt* stmt) {
    if (stmt->value) {
        uint32_t valReg = genExpr(stmt->value.get());
        emit(IROpcode::RET, {IROperand::reg(valReg)});
    } else {
        emit(IROpcode::RET, {});
    }
}

void IRBuilderImpl::genBreakStmt(BreakStmt*) {
    if (!loopStack_.empty()) {
        emit(IROpcode::JMP, {IROperand::label(loopStack_.back().breakLabel)});
    }
}

void IRBuilderImpl::genContinueStmt(ContinueStmt*) {
    if (!loopStack_.empty()) {
        emit(IROpcode::JMP, {IROperand::label(loopStack_.back().continueLabel)});
    }
}

void IRBuilderImpl::genExprStmt(ExprStmt* stmt) {
    genExpr(stmt->expr.get());
}

void IRBuilderImpl::genAssignStmt(AssignStmt* stmt) {
    uint32_t rightReg = genExpr(stmt->value.get());
    bool isGlobal = false;
    int offset = getVarOffset(stmt->name, isGlobal);

    if (isGlobal) {
        emit(IROpcode::STORE_GLOBAL, {IROperand::global(stmt->name), IROperand::reg(rightReg)});
    } else {
        emit(IROpcode::STORE_LOCAL, {IROperand::imm(offset), IROperand::reg(rightReg)});
    }
}

void IRBuilderImpl::genDeclStmt(DeclStmt* stmt) {
    genDecl(stmt->decl.get());
}

// ============================================================
// 声明 IR 生成
// ============================================================

void IRBuilderImpl::genDecl(ASTNode* decl) {
    if (decl->kind() == NodeKind::VarDecl) {
        genVarDecl(static_cast<VarDecl*>(decl));
    } else if (decl->kind() == NodeKind::ConstDecl) {
        genConstDecl(static_cast<ConstDecl*>(decl));
    }
}

void IRBuilderImpl::genVarDecl(VarDecl* decl) {
    uint32_t initReg = genExpr(decl->initExpr.get());

    int offset = allocStackOffset();
    bool isGlobal = decl->isGlobal;

    // 将变量注册到当前作用域（因为 SA 的作用域已被弹出）
    Symbol varSym;
    varSym.name = decl->name;
    varSym.kind = SymbolKind::Variable;
    varSym.isGlobalVar = decl->isGlobal;
    varSym.stackOffset = offset;
    symTable_->insert(varSym);

    if (isGlobal) {
        emit(IROpcode::STORE_GLOBAL, {IROperand::global(decl->name), IROperand::reg(initReg)});
    } else {
        emit(IROpcode::STORE_LOCAL, {IROperand::imm(offset), IROperand::reg(initReg)});
    }
}

void IRBuilderImpl::genConstDecl(ConstDecl* decl) {
    uint32_t initReg = genExpr(decl->initExpr.get());

    int offset = allocStackOffset();
    bool isGlobal = decl->isGlobal;

    // 将常量注册到当前作用域
    Symbol constSym;
    constSym.name = decl->name;
    constSym.kind = SymbolKind::Constant;
    constSym.isGlobalConst = decl->isGlobal;
    constSym.stackOffset = offset;
    symTable_->insert(constSym);

    if (isGlobal) {
        emit(IROpcode::STORE_GLOBAL, {IROperand::global(decl->name), IROperand::reg(initReg)});
    } else {
        emit(IROpcode::STORE_LOCAL, {IROperand::imm(offset), IROperand::reg(initReg)});
    }
}

int32_t IRBuilderImpl::evalGlobalInit(ExprNode* expr) {
    if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
        return static_cast<int32_t>(num->value);
    }

    if (auto* id = dynamic_cast<IdExpr*>(expr)) {
        Symbol* sym = symTable_->lookup(id->name);
        if (sym && sym->kind == SymbolKind::Constant) {
            return static_cast<int32_t>(sym->constValue);
        }
        return 0;
    }

    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        int32_t operand = evalGlobalInit(unary->operand.get());
        switch (unary->op) {
            case UnaryOp::POS: return operand;
            case UnaryOp::NEG: return -operand;
            case UnaryOp::NOT: return operand ? 0 : 1;
        }
    }

    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        int32_t left = evalGlobalInit(bin->left.get());
        int32_t right = evalGlobalInit(bin->right.get());
        switch (bin->op) {
            case BinaryOp::ADD: return left + right;
            case BinaryOp::SUB: return left - right;
            case BinaryOp::MUL: return left * right;
            case BinaryOp::DIV: return right != 0 ? left / right : 0;
            case BinaryOp::MOD: return right != 0 ? left % right : 0;
            case BinaryOp::LT:  return left < right ? 1 : 0;
            case BinaryOp::GT:  return left > right ? 1 : 0;
            case BinaryOp::LE:  return left <= right ? 1 : 0;
            case BinaryOp::GE:  return left >= right ? 1 : 0;
            case BinaryOp::EQ:  return left == right ? 1 : 0;
            case BinaryOp::NE:  return left != right ? 1 : 0;
            case BinaryOp::AND: return (left && right) ? 1 : 0;
            case BinaryOp::OR:  return (left || right) ? 1 : 0;
        }
    }

    return 0;
}

// ============================================================
// 函数 IR 生成
// ============================================================

void IRBuilderImpl::genFunc(FuncDef* func) {
    program_.functions.emplace_back();
    currentFunc_ = &program_.functions.back();
    currentFunc_->name = func->name;

    nextStackOffset_ = 0;

    // 进入函数作用域（与语义分析器同步）
    enterScope();

    for (size_t i = 0; i < func->params.size(); ++i) {
        int offset = allocStackOffset();
        Symbol paramSym;
        paramSym.name = func->params[i]->name;
        paramSym.kind = SymbolKind::Parameter;
        paramSym.paramIndex = static_cast<int>(i);
        paramSym.stackOffset = offset;
        symTable_->insert(paramSym);

        emit(IROpcode::STORE_LOCAL,
             {IROperand::imm(offset), IROperand::reg(static_cast<uint32_t>(i))});
    }

    // 直接遍历函数体语句（匹配 SA 作用域，函数体本身不再额外 enterScope）
    for (const auto& s : func->body->stmts) {
        genStmt(s.get());
    }

    if (func->returnType == Type::INT) {
        bool hasTrailingRet = false;
        if (!currentFunc_->instructions.empty()) {
            hasTrailingRet = (currentFunc_->instructions.back().opcode == IROpcode::RET);
        }
        if (!hasTrailingRet) {
            emit(IROpcode::RET, {IROperand::imm(0)});
        }
    } else {
        bool hasTrailingRet = false;
        if (!currentFunc_->instructions.empty()) {
            hasTrailingRet = (currentFunc_->instructions.back().opcode == IROpcode::RET);
        }
        if (!hasTrailingRet) {
            emit(IROpcode::RET, {});
        }
    }

    exitScope();
    currentFunc_ = nullptr;
}

// ============================================================
// 辅助方法
// ============================================================

int IRBuilderImpl::getVarOffset(const std::string& name, bool& isGlobal) {
    Symbol* sym = symTable_->lookup(name);
    if (!sym) return -1;

    if (sym->kind == SymbolKind::Variable || sym->kind == SymbolKind::Constant) {
        if (sym->isGlobalVar || sym->isGlobalConst) {
            isGlobal = true;
            return -1;
        }
        isGlobal = false;
        return sym->stackOffset;
    }

    if (sym->kind == SymbolKind::Parameter) {
        isGlobal = false;
        return sym->stackOffset;
    }

    return -1;
}

void IRBuilderImpl::enterScope() { symTable_->enterScope(); }

void IRBuilderImpl::exitScope() { symTable_->exitScope(); }

}  // namespace toyc
