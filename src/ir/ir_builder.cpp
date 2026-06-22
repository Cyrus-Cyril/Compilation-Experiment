// ToyC IR Builder 实现

#include "toyc/ir/ir.h"

namespace toyc {

IROperand IROperand::reg(uint32_t id) { return {OperandKind::VirtualReg, id}; }

IROperand IROperand::imm(int32_t v) { return {OperandKind::Immediate, v}; }

IROperand IROperand::label(uint32_t id) { return {OperandKind::Label, id}; }

IROperand IROperand::global(const std::string& n) { return {OperandKind::GlobalName, n}; }

IROperand IROperand::func(const std::string& n) { return {OperandKind::FuncName, n}; }

}  // namespace toyc