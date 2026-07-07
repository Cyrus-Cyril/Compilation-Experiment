// ToyC Optimizer 实现

#include "toyc/optimize/optimizer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

std::string stringValue(const IROperand& operand) {
    return std::get<std::string>(operand.value);
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

std::optional<uint32_t> destReg(const IRInstruction& inst) {
    switch (inst.opcode) {
        case IROpcode::ADD:
        case IROpcode::SUB:
        case IROpcode::MUL:
        case IROpcode::DIV:
        case IROpcode::MOD:
        case IROpcode::NEG:
        case IROpcode::EQ:
        case IROpcode::NE:
        case IROpcode::LT:
        case IROpcode::GT:
        case IROpcode::LE:
        case IROpcode::GE:
        case IROpcode::AND:
        case IROpcode::OR:
        case IROpcode::NOT:
        case IROpcode::LOAD_GLOBAL:
        case IROpcode::LOAD_LOCAL:
        case IROpcode::CALL:
            if (!inst.operands.empty() && inst.operands[0].kind == OperandKind::VirtualReg) {
                return regId(inst.operands[0]);
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

bool isPureValueOp(IROpcode op) {
    return isBinaryFoldable(op) || isUnaryFoldable(op);
}

bool isRemovableValueInst(const IRInstruction& inst) {
    return isPureValueOp(inst.opcode) || inst.opcode == IROpcode::LOAD_LOCAL ||
           inst.opcode == IROpcode::LOAD_GLOBAL;
}

std::string operandKey(const IROperand& operand) {
    std::ostringstream out;
    out << static_cast<int>(operand.kind) << ':';
    if (std::holds_alternative<uint32_t>(operand.value)) {
        out << std::get<uint32_t>(operand.value);
    } else if (std::holds_alternative<int32_t>(operand.value)) {
        out << std::get<int32_t>(operand.value);
    } else {
        out << std::get<std::string>(operand.value);
    }
    return out.str();
}

std::string expressionKey(const IRInstruction& inst) {
    std::ostringstream out;
    out << static_cast<int>(inst.opcode);
    for (size_t i = 1; i < inst.operands.size(); ++i) {
        out << '|' << operandKey(inst.operands[i]);
    }
    return out.str();
}

bool sameOperand(const IROperand& lhs, const IROperand& rhs) {
    return lhs.kind == rhs.kind && lhs.value == rhs.value;
}

bool isCopyAdd(const IRInstruction& inst, IROperand& source) {
    if (inst.opcode != IROpcode::ADD || inst.operands.size() != 3) return false;
    const auto& lhs = inst.operands[1];
    const auto& rhs = inst.operands[2];
    if (rhs.kind == OperandKind::Immediate && immValue(rhs) == 0 &&
        lhs.kind == OperandKind::VirtualReg) {
        source = lhs;
        return true;
    }
    if (lhs.kind == OperandKind::Immediate && immValue(lhs) == 0 &&
        rhs.kind == OperandKind::VirtualReg) {
        source = rhs;
        return true;
    }
    return false;
}

IROperand resolveCopy(
    const IROperand& operand,
    const std::unordered_map<uint32_t, IROperand>& copies) {
    if (operand.kind != OperandKind::VirtualReg) return operand;

    IROperand current = operand;
    std::unordered_set<uint32_t> seen;
    while (current.kind == OperandKind::VirtualReg) {
        uint32_t id = regId(current);
        if (!seen.insert(id).second) break;
        auto found = copies.find(id);
        if (found == copies.end()) break;
        current = found->second;
    }
    return current;
}

void replaceCopyUses(IRInstruction& inst, const std::unordered_map<uint32_t, IROperand>& copies) {
    auto replaceRange = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end && i < inst.operands.size(); ++i) {
            inst.operands[i] = resolveCopy(inst.operands[i], copies);
        }
    };

    switch (inst.opcode) {
        case IROpcode::ADD:
        case IROpcode::SUB:
        case IROpcode::MUL:
        case IROpcode::DIV:
        case IROpcode::MOD:
        case IROpcode::EQ:
        case IROpcode::NE:
        case IROpcode::LT:
        case IROpcode::GT:
        case IROpcode::LE:
        case IROpcode::GE:
        case IROpcode::AND:
        case IROpcode::OR:
            replaceRange(1, 3);
            break;
        case IROpcode::NEG:
        case IROpcode::NOT:
            replaceRange(1, 2);
            break;
        case IROpcode::STORE_LOCAL:
        case IROpcode::STORE_GLOBAL:
            replaceRange(1, 2);
            break;
        case IROpcode::BEQ:
        case IROpcode::BNE:
            replaceRange(0, 2);
            break;
        case IROpcode::PARAM:
        case IROpcode::RET:
            replaceRange(0, inst.operands.size());
            break;
        default:
            break;
    }
}

void eraseCopyAliases(
    std::unordered_map<uint32_t, IROperand>& copies,
    uint32_t dest) {
    copies.erase(dest);
    for (auto it = copies.begin(); it != copies.end();) {
        if (it->second.kind == OperandKind::VirtualReg && regId(it->second) == dest) {
            it = copies.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<IRInstruction> propagateCopiesAndCse(std::vector<IRInstruction> insts) {
    std::vector<IRInstruction> result;
    result.reserve(insts.size());

    std::unordered_map<uint32_t, IROperand> copies;
    std::unordered_map<std::string, uint32_t> expressions;
    std::unordered_map<int32_t, uint32_t> localLoads;

    auto clearBlockState = [&]() {
        copies.clear();
        expressions.clear();
        localLoads.clear();
    };

    for (auto inst : insts) {
        if (inst.opcode == IROpcode::LABEL) {
            clearBlockState();
            result.push_back(std::move(inst));
            continue;
        }

        replaceCopyUses(inst, copies);

        if (auto dest = destReg(inst)) {
            eraseCopyAliases(copies, *dest);
        }

        if (inst.opcode == IROpcode::LOAD_LOCAL && inst.operands.size() == 2 &&
            inst.operands[1].kind == OperandKind::Immediate) {
            int32_t offset = immValue(inst.operands[1]);
            if (auto found = localLoads.find(offset); found != localLoads.end()) {
                if (auto dest = destReg(inst)) {
                    IROperand source = resolveCopy(IROperand::reg(found->second), copies);
                    inst = {IROpcode::ADD, {IROperand::reg(*dest), source, IROperand::imm(0)}};
                    copies[*dest] = source;
                }
            } else if (auto dest = destReg(inst)) {
                localLoads[offset] = *dest;
            }
        } else if (isPureValueOp(inst.opcode)) {
            const std::string key = expressionKey(inst);
            if (auto found = expressions.find(key); found != expressions.end()) {
                IROperand source = resolveCopy(IROperand::reg(found->second), copies);
                if (auto dest = destReg(inst)) {
                    inst = {IROpcode::ADD, {IROperand::reg(*dest), source, IROperand::imm(0)}};
                    copies[*dest] = source;
                }
            } else if (auto dest = destReg(inst)) {
                expressions[key] = *dest;
                IROperand source;
                if (isCopyAdd(inst, source)) {
                    copies[*dest] = resolveCopy(source, copies);
                }
            }
        } else if (auto dest = destReg(inst)) {
            IROperand source;
            if (isCopyAdd(inst, source)) {
                copies[*dest] = resolveCopy(source, copies);
            }
        }

        if (inst.opcode == IROpcode::STORE_LOCAL && !inst.operands.empty() &&
            inst.operands[0].kind == OperandKind::Immediate) {
            localLoads.erase(immValue(inst.operands[0]));
            expressions.clear();
        } else if (inst.opcode == IROpcode::STORE_LOCAL || inst.opcode == IROpcode::STORE_GLOBAL ||
                   inst.opcode == IROpcode::LOAD_GLOBAL || inst.opcode == IROpcode::CALL) {
            expressions.clear();
        }

        if (inst.opcode == IROpcode::JMP || inst.opcode == IROpcode::BEQ ||
            inst.opcode == IROpcode::BNE || inst.opcode == IROpcode::RET) {
            clearBlockState();
        }

        result.push_back(std::move(inst));
    }

    return result;
}

std::vector<IRInstruction> removeDeadValueInsts(const std::vector<IRInstruction>& insts) {
    std::unordered_map<uint32_t, int> uses;

    auto countOperand = [&](const IROperand& operand) {
        if (operand.kind == OperandKind::VirtualReg) ++uses[regId(operand)];
    };

    for (const auto& inst : insts) {
        switch (inst.opcode) {
            case IROpcode::ADD:
            case IROpcode::SUB:
            case IROpcode::MUL:
            case IROpcode::DIV:
            case IROpcode::MOD:
            case IROpcode::EQ:
            case IROpcode::NE:
            case IROpcode::LT:
            case IROpcode::GT:
            case IROpcode::LE:
            case IROpcode::GE:
            case IROpcode::AND:
            case IROpcode::OR:
                if (inst.operands.size() >= 3) {
                    countOperand(inst.operands[1]);
                    countOperand(inst.operands[2]);
                }
                break;
            case IROpcode::NEG:
            case IROpcode::NOT:
                if (inst.operands.size() >= 2) countOperand(inst.operands[1]);
                break;
            case IROpcode::STORE_LOCAL:
            case IROpcode::STORE_GLOBAL:
                if (inst.operands.size() >= 2) countOperand(inst.operands[1]);
                break;
            case IROpcode::BEQ:
            case IROpcode::BNE:
                if (inst.operands.size() >= 2) {
                    countOperand(inst.operands[0]);
                    countOperand(inst.operands[1]);
                }
                break;
            case IROpcode::PARAM:
            case IROpcode::RET:
                if (!inst.operands.empty()) countOperand(inst.operands[0]);
                break;
            default:
                break;
        }
    }

    std::vector<IRInstruction> result;
    result.reserve(insts.size());
    for (const auto& inst : insts) {
        auto dest = destReg(inst);
        if (dest && isRemovableValueInst(inst) && uses[*dest] == 0) {
            continue;
        }
        result.push_back(inst);
    }
    return result;
}

uint32_t maxLabelId(const std::vector<IRInstruction>& insts) {
    uint32_t maxLabel = 0;
    for (const auto& inst : insts) {
        for (const auto& operand : inst.operands) {
            if (operand.kind == OperandKind::Label) {
                maxLabel = std::max(maxLabel, labelId(operand));
            }
        }
    }
    return maxLabel;
}

std::vector<IRInstruction> rewriteTailRecursion(const IRFunction& fn) {
    if (fn.paramCount <= 0) return fn.instructions;

    const uint32_t entryLabel = maxLabelId(fn.instructions) + 1;
    bool changed = false;
    std::vector<IROperand> pendingParams;
    std::vector<IRInstruction> result;
    result.reserve(fn.instructions.size());

    auto flushParams = [&]() {
        for (const auto& param : pendingParams) {
            result.push_back({IROpcode::PARAM, {param}});
        }
        pendingParams.clear();
    };

    for (size_t i = 0; i < fn.instructions.size(); ++i) {
        const IRInstruction& inst = fn.instructions[i];
        if (inst.opcode == IROpcode::PARAM && inst.operands.size() == 1) {
            pendingParams.push_back(inst.operands[0]);
            continue;
        }

        if (inst.opcode == IROpcode::CALL && inst.operands.size() == 2 &&
            inst.operands[1].kind == OperandKind::FuncName &&
            stringValue(inst.operands[1]) == fn.name &&
            pendingParams.size() == static_cast<size_t>(fn.paramCount) &&
            i + 1 < fn.instructions.size() &&
            fn.instructions[i + 1].opcode == IROpcode::RET &&
            fn.instructions[i + 1].operands.size() == 1 &&
            sameOperand(fn.instructions[i + 1].operands[0], inst.operands[0])) {
            for (size_t p = 0; p < pendingParams.size(); ++p) {
                result.push_back(
                    {IROpcode::STORE_LOCAL, {IROperand::imm(static_cast<int32_t>(p * 4)), pendingParams[p]}});
            }
            result.push_back({IROpcode::JMP, {IROperand::label(entryLabel)}});
            pendingParams.clear();
            changed = true;
            ++i;
            continue;
        }

        flushParams();
        result.push_back(inst);
    }
    flushParams();

    if (!changed) return fn.instructions;

    size_t insertAt = 0;
    while (insertAt < result.size() && insertAt < static_cast<size_t>(fn.paramCount) &&
           result[insertAt].opcode == IROpcode::STORE_LOCAL) {
        ++insertAt;
    }
    result.insert(result.begin() + static_cast<std::ptrdiff_t>(insertAt),
                  {IROpcode::LABEL, {IROperand::label(entryLabel)}});
    return result;
}

uint32_t resolveJumpTarget(
    uint32_t label,
    const std::unordered_map<uint32_t, uint32_t>& jumpAliases) {
    std::unordered_set<uint32_t> seen;
    uint32_t current = label;
    while (seen.insert(current).second) {
        auto found = jumpAliases.find(current);
        if (found == jumpAliases.end()) break;
        current = found->second;
    }
    return current;
}

std::vector<IRInstruction> rewriteJumpChains(std::vector<IRInstruction> insts) {
    std::unordered_map<uint32_t, uint32_t> jumpAliases;
    for (size_t i = 0; i + 1 < insts.size(); ++i) {
        if (insts[i].opcode == IROpcode::LABEL && insts[i].operands.size() == 1 &&
            insts[i + 1].opcode == IROpcode::JMP && insts[i + 1].operands.size() == 1) {
            jumpAliases[labelId(insts[i].operands[0])] = labelId(insts[i + 1].operands[0]);
        }
    }

    for (auto& inst : insts) {
        if ((inst.opcode == IROpcode::JMP || inst.opcode == IROpcode::BEQ ||
             inst.opcode == IROpcode::BNE) && !inst.operands.empty()) {
            IROperand& target = inst.operands.back();
            if (target.kind == OperandKind::Label) {
                target = IROperand::label(resolveJumpTarget(labelId(target), jumpAliases));
            }
        }
    }

    return removeFallthroughJumps(std::move(insts));
}

IROpcode invertedBranch(IROpcode op) {
    return op == IROpcode::BEQ ? IROpcode::BNE : IROpcode::BEQ;
}

std::vector<IRInstruction> invertBranchOverJump(std::vector<IRInstruction> insts) {
    std::vector<IRInstruction> result;
    result.reserve(insts.size());

    for (size_t i = 0; i < insts.size(); ++i) {
        if ((insts[i].opcode == IROpcode::BEQ || insts[i].opcode == IROpcode::BNE) &&
            insts[i].operands.size() == 3 &&
            i + 2 < insts.size() &&
            insts[i + 1].opcode == IROpcode::JMP &&
            insts[i + 1].operands.size() == 1 &&
            insts[i + 2].opcode == IROpcode::LABEL &&
            insts[i + 2].operands.size() == 1 &&
            labelId(insts[i].operands[2]) == labelId(insts[i + 2].operands[0])) {
            IRInstruction branch = insts[i];
            branch.opcode = invertedBranch(branch.opcode);
            branch.operands[2] = insts[i + 1].operands[0];
            result.push_back(std::move(branch));
            ++i;
            continue;
        }
        result.push_back(std::move(insts[i]));
    }

    return result;
}

}  // namespace

IRProgram Optimizer::optimize(const IRProgram& input) {
    IRProgram output = input;

    std::unordered_map<std::string, int32_t> globalConstants;
    for (const auto& global : output.globals) {
        if (global.isConst) {
            globalConstants[global.name] = global.initialValue;
        }
    }

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
                    case IROpcode::CALL:
                        rememberDest(inst, constants, std::nullopt);
                        break;
                    case IROpcode::LOAD_GLOBAL:
                        if (inst.operands.size() == 2 &&
                            inst.operands[1].kind == OperandKind::GlobalName) {
                            auto found = globalConstants.find(stringValue(inst.operands[1]));
                            if (found != globalConstants.end()) {
                                inst = {IROpcode::ADD,
                                        {inst.operands[0], IROperand::imm(0), IROperand::imm(found->second)}};
                                rememberDest(inst, constants, found->second);
                                break;
                            }
                        }
                        rememberDest(inst, constants, std::nullopt);
                        break;
                    case IROpcode::JMP:
                        constants.clear();
                        unreachable = true;
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

        fn.instructions = std::move(optimized);
        for (int pass = 0; pass < 3; ++pass) {
            size_t before = fn.instructions.size();
            fn.instructions = propagateCopiesAndCse(std::move(fn.instructions));
            fn.instructions = removeDeadValueInsts(fn.instructions);
            fn.instructions = invertBranchOverJump(std::move(fn.instructions));
            fn.instructions = rewriteJumpChains(std::move(fn.instructions));
            if (fn.instructions.size() == before) break;
        }
        fn.instructions = rewriteTailRecursion(fn);
        fn.instructions = invertBranchOverJump(std::move(fn.instructions));
        fn.instructions = rewriteJumpChains(std::move(fn.instructions));
    }

    return output;
}

}  // namespace toyc
