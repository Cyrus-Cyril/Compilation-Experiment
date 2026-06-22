#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "toyc/lexer/token.h"
#include "toyc/parser/ast.h"

namespace toyc {

/// 符号类型
enum class SymbolKind { Variable, Constant, Parameter, Function };

/// 符号定义
struct Symbol {
    std::string name;
    SymbolKind kind = SymbolKind::Variable;
    SourceLocation declLoc;

    // Variable
    bool isGlobalVar = false;
    int stackOffset = -1;

    // Constant
    bool isGlobalConst = false;
    int constValue = 0;

    // Parameter
    int paramIndex = -1;

    // Function
    Type returnType = Type::INT;
    std::vector<Type> paramTypes;
};

/// 符号表（嵌套作用域）
class SymbolTable {
public:
    void enterScope();
    void exitScope();
    bool isGlobalScope() const;

    bool insert(const Symbol& sym);
    Symbol* lookup(const std::string& name);
    Symbol* lookupCurrentScope(const std::string& name);
    Symbol* lookupGlobalScope(const std::string& name);

    int loopDepth() const;
    void enterLoop();
    void leaveLoop();

    Symbol* currentFunction() const;
    void setCurrentFunction(Symbol* func);

private:
    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
    int loopDepth_ = 0;
    Symbol* currentFunction_ = nullptr;
};

}  // namespace toyc