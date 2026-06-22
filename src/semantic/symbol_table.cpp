// ToyC Semantic - SymbolTable 实现

#include "toyc/semantic/symbol_table.h"

namespace toyc {

void SymbolTable::enterScope() { scopes_.emplace_back(); }

void SymbolTable::exitScope() {
    if (!scopes_.empty()) scopes_.pop_back();
}

bool SymbolTable::isGlobalScope() const { return scopes_.size() <= 1; }

bool SymbolTable::insert(const Symbol& sym) {
    if (lookupCurrentScope(sym.name)) return false;
    scopes_.back()[sym.name] = sym;
    return true;
}

Symbol* SymbolTable::lookup(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return &found->second;
    }
    return nullptr;
}

Symbol* SymbolTable::lookupCurrentScope(const std::string& name) {
    if (scopes_.empty()) return nullptr;
    auto found = scopes_.back().find(name);
    return (found != scopes_.back().end()) ? &found->second : nullptr;
}

Symbol* SymbolTable::lookupGlobalScope(const std::string& name) {
    if (scopes_.empty()) return nullptr;
    auto found = scopes_.front().find(name);
    return (found != scopes_.front().end()) ? &found->second : nullptr;
}

int SymbolTable::loopDepth() const { return loopDepth_; }

void SymbolTable::enterLoop() { ++loopDepth_; }

void SymbolTable::leaveLoop() {
    if (loopDepth_ > 0) --loopDepth_;
}

Symbol* SymbolTable::currentFunction() const { return currentFunction_; }

void SymbolTable::setCurrentFunction(Symbol* func) { currentFunction_ = func; }

}  // namespace toyc