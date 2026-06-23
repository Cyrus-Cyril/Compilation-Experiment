#pragma once

#include "toyc/ir/ir.h"

namespace toyc {

class CompUnit;
class SymbolTable;

/// IR 生成器：遍历 Annotated AST + SymbolTable，生成三地址码 (TAC) IR
class IRBuilder {
public:
    /// 从 AST 和符号表构建完整 IR 程序
    static IRProgram build(CompUnit* ast, SymbolTable* symTable);
};

}  // namespace toyc
