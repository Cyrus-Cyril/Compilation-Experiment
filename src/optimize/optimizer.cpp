// ToyC Optimizer 实现

#include "toyc/optimize/optimizer.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace toyc {

namespace {

uint32_t regId(const IROperand& operand) {
    return std::get<uint32_t>(operand.value);
}

int32_t immValue(const IROperand& operand) {
    return std::get<int32_t>(operand.value);
}

uint32_t labelId(const IROperand& operand) {
    return std::get<uint32_t>(operand.value);
}

std::optional<int32_t> constValue(
    const IROperand& operand,
    const std::unordered_map<uint32_t, int32_t>& constants) {
    if (operand.kind == OperandKind::Immediate) {
        return immValue(operand);
    }
    if (operand.kind == OperandKind::VirtualReg) {
        auto found = constants.find(regId(operand));
        if (found != constants.end()) return found->second;
    }
    return std::nullopt;
}

void replaceKnownOperands(
    IRInstruction& inst,
    const std::unordered_map<uint32_t, int32_t>& constants) {
    for (auto& operand : inst.operands) {
        if (operand.kind != OperandKind::VirtualReg) continue;
        auto found = constants.find(regId(operand));
        if (found != constants.end()) {
            operand = IROperand::imm(found->second);
        }
    }
}

std::optional<int32_t> foldBinary(IROpcode op, int32_t lhs, int32_t rhs) {
    switch (op) {
        case IROpcode::ADD: return lhs + rhs;
        case IROpcode::SUB: return lhs - rhs;
        case IROpcode::MUL: return lhs * rhs;
        case IROpcode::DIV: return rhs != 0 ? std::optional<int32_t>(lhs / rhs) : std::nullopt;
        case IROpcode::MOD: return rhs != 0 ? std::optional<int32_t>(lhs % rhs) : std::nullopt;
        case IROpcode::LT:  return lhs < rhs ? 1 : 0;
        case IROpcode::GT:  return lhs > rhs ? 1 : 0;
        case IROpcode::LE:  return lhs <= rhs ? 1 : 0;
        case IROpcode::GE:  return lhs >= rhs ? 1 : 0;
        case IROpcode::EQ:  return lhs == rhs ? 1 : 0;
        case IROpcode::NE:  return lhs != rhs ? 1 : 0;
        case IROpcode::AND: return (lhs && rhs) ? 1 : 0;
        case IROpcode::OR:  return (lhs || rhs) ? 1 : 0;
        default:            return std::nullopt;
    }
}

std::optional<int32_t> foldUnary(IROpcode op, int32_t value) {
    switch (op) {
        case IROpcode::NEG: return -value;
        case IROpcode::NOT: return value ? 0 : 1;
        default:            return std::nullopt;
    }
}

std::optional<IRInstruction> simplifyAlgebraic(const IRInstruction& inst) {
    if (inst.operands.size() != 3) return std::nullopt;

    const IROperand& dest = inst.operands[0];
    const IROperand& lhs = inst.operands[1];
    const IROperand& rhs = inst.operands[2];
    const bool lhsImm = lhs.kind == OperandKind::Immediate;
    const bool rhsImm = rhs.kind == OperandKind::Immediate;
    const int32_t lhsVal = lhsImm ? immValue(lhs) : 0;
    const int32_t rhsVal = rhsImm ? immValue(rhs) : 0;

    switch (inst.opcode) {
        case IROpcode::ADD:
            if (rhsImm && rhsVal == 0) return IRInstruction{IROpcode::ADD, {dest, lhs, IROperand::imm(0)}};
            if (lhsImm && lhsVal == 0) return IRInstruction{IROpcode::ADD, {dest, rhs, IROperand::imm(0)}};
            break;
        case IROpcode::SUB:
            if (rhsImm && rhsVal == 0) return IRInstruction{IROpcode::ADD, {dest, lhs, IROperand::imm(0)}};
            break;
        case IROpcode::MUL:
            if ((rhsImm && rhsVal == 0) || (lhsImm && lhsVal == 0)) {
                return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(0)}};
            }
            if (rhsImm && rhsVal == 1) return IRInstruction{IROpcode::ADD, {dest, lhs, IROperand::imm(0)}};
            if (lhsImm && lhsVal == 1) return IRInstruction{IROpcode::ADD, {dest, rhs, IROperand::imm(0)}};
            break;
        case IROpcode::DIV:
            if (rhsImm && rhsVal == 1) return IRInstruction{IROpcode::ADD, {dest, lhs, IROperand::imm(0)}};
            break;
        case IROpcode::AND:
            if ((rhsImm && rhsVal == 0) || (lhsImm && lhsVal == 0)) {
                return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(0)}};
            }
            break;
        case IROpcode::OR:
            if (rhsImm && rhsVal == 0) return IRInstruction{IROpcode::ADD, {dest, lhs, IROperand::imm(0)}};
            if (lhsImm && lhsVal == 0) return IRInstruction{IROpcode::ADD, {dest, rhs, IROperand::imm(0)}};
            break;
        default:
            break;
    }

    return std::nullopt;
}

bool isBinaryFoldable(IROpcode op) {
    switch (op) {
        case IROpcode::ADD:
        case IROpcode::SUB:
        case IROpcode::MUL:
        case IROpcode::DIV:
        case IROpcode::MOD:
        case IROpcode::LT:
        case IROpcode::GT:
        case IROpcode::LE:
        case IROpcode::GE:
        case IROpcode::EQ:
        case IROpcode::NE:
        case IROpcode::AND:
        case IROpcode::OR:
            return true;
        default:
            return false;
    }
}

bool isUnaryFoldable(IROpcode op) {
    return op == IROpcode::NEG || op == IROpcode::NOT;
}

void rememberDest(
    const IRInstruction& inst,
    std::unordered_map<uint32_t, int32_t>& constants,
    std::optional<int32_t> value) {
    if (inst.operands.empty() || inst.operands[0].kind != OperandKind::VirtualReg) return;
    uint32_t dest = regId(inst.operands[0]);
    if (value) {
        constants[dest] = *value;
    } else {
        constants.erase(dest);
    }
}

std::vector<IRInstruction> removeFallthroughJumps(std::vector<IRInstruction> insts) {
    std::vector<IRInstruction> result;
    result.reserve(insts.size());

    for (size_t i = 0; i < insts.size(); ++i) {
        if (insts[i].opcode == IROpcode::JMP && insts[i].operands.size() == 1 &&
            i + 1 < insts.size() && insts[i + 1].opcode == IROpcode::LABEL &&
            insts[i + 1].operands.size() == 1 &&
            labelId(insts[i].operands[0]) == labelId(insts[i + 1].operands[0])) {
            continue;
        }
        result.push_back(std::move(insts[i]));
    }

    return result;
}

}  // namespace

IRProgram Optimizer::optimize(const IRProgram& input) {
    IRProgram output = input;

    for (auto& fn : output.functions) {
        std::unordered_map<uint32_t, int32_t> constants;
        std::vector<IRInstruction> optimized;
        bool unreachable = false;

        for (auto inst : fn.instructions) {
            if (unreachable && inst.opcode != IROpcode::LABEL) {
                continue;
            }
            if (inst.opcode == IROpcode::LABEL) {
                unreachable = false;
                constants.clear();
                optimized.push_back(std::move(inst));
                continue;
            }

            replaceKnownOperands(inst, constants);

            if (isBinaryFoldable(inst.opcode) && inst.operands.size() == 3) {
                auto lhs = constValue(inst.operands[1], constants);
                auto rhs = constValue(inst.operands[2], constants);
                auto folded = (lhs && rhs) ? foldBinary(inst.opcode, *lhs, *rhs) : std::nullopt;
                if (folded) {
                    inst = {IROpcode::ADD,
                            {inst.operands[0], IROperand::imm(0), IROperand::imm(*folded)}};
                } else if (auto simplified = simplifyAlgebraic(inst)) {
                    inst = *simplified;
                    lhs = constValue(inst.operands[1], constants);
                    rhs = constValue(inst.operands[2], constants);
                    folded = (lhs && rhs) ? foldBinary(inst.opcode, *lhs, *rhs) : std::nullopt;
                }
                rememberDest(inst, constants, folded);
            } else if (isUnaryFoldable(inst.opcode) && inst.operands.size() == 2) {
                auto operand = constValue(inst.operands[1], constants);
                auto folded = operand ? foldUnary(inst.opcode, *operand) : std::nullopt;
                if (folded) {
                    inst = {IROpcode::ADD,
                            {inst.operands[0], IROperand::imm(0), IROperand::imm(*folded)}};
                }
                rememberDest(inst, constants, folded);
            } else {
                switch (inst.opcode) {
                    case IROpcode::LOAD_LOCAL:
                    case IROpcode::LOAD_GLOBAL:
                    case IROpcode::CALL:
                        rememberDest(inst, constants, std::nullopt);
                        break;
                    case IROpcode::JMP:
                        constants.clear();
                        break;
                    case IROpcode::BEQ:
                    case IROpcode::BNE: {
                        auto lhs = constValue(inst.operands[0], constants);
                        auto rhs = constValue(inst.operands[1], constants);
                        if (lhs && rhs) {
                            bool taken = inst.opcode == IROpcode::BEQ ? (*lhs == *rhs) : (*lhs != *rhs);
                            if (taken) {
                                inst = {IROpcode::JMP, {inst.operands[2]}};
                            } else {
                                constants.clear();
                                continue;
                            }
                        }
                        constants.clear();
                        break;
                    }
                    default:
                        break;
                }
            }

            if (inst.opcode == IROpcode::RET) {
                unreachable = true;
            }
            optimized.push_back(std::move(inst));
        }

        fn.instructions = removeFallthroughJumps(std::move(optimized));
    }

    return output;
}

}  // namespace toyc
