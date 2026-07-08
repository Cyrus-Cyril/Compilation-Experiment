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

bool sameOperand(const IROperand& lhs, const IROperand& rhs) {
    return lhs.kind == rhs.kind && lhs.value == rhs.value;
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

    const bool sameOperands = sameOperand(lhs, rhs);

    switch (inst.opcode) {
        case IROpcode::ADD:
            if (rhsImm && rhsVal == 0) return IRInstruction{IROpcode::ADD, {dest, lhs, IROperand::imm(0)}};
            if (lhsImm && lhsVal == 0) return IRInstruction{IROpcode::ADD, {dest, rhs, IROperand::imm(0)}};
            break;
        case IROpcode::SUB:
            if (rhsImm && rhsVal == 0) return IRInstruction{IROpcode::ADD, {dest, lhs, IROperand::imm(0)}};
            if (sameOperands) return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(0)}};
            break;
        case IROpcode::MUL:
            if ((rhsImm && rhsVal == 0) || (lhsImm && lhsVal == 0)) {
                return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(0)}};
            }
            if (rhsImm && rhsVal == 1) return IRInstruction{IROpcode::ADD, {dest, lhs, IROperand::imm(0)}};
            if (lhsImm && lhsVal == 1) return IRInstruction{IROpcode::ADD, {dest, rhs, IROperand::imm(0)}};
            if (rhsImm && rhsVal == -1) return IRInstruction{IROpcode::NEG, {dest, lhs}};
            if (lhsImm && lhsVal == -1) return IRInstruction{IROpcode::NEG, {dest, rhs}};
            break;
        case IROpcode::DIV:
            if (rhsImm && rhsVal == 1) return IRInstruction{IROpcode::ADD, {dest, lhs, IROperand::imm(0)}};
            if (rhsImm && rhsVal == -1) return IRInstruction{IROpcode::NEG, {dest, lhs}};
            if (sameOperands && lhs.kind == OperandKind::VirtualReg)
                return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(1)}};
            break;
        case IROpcode::MOD:
            if (rhsImm && (rhsVal == 1 || rhsVal == -1))
                return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(0)}};
            if (sameOperands && lhs.kind == OperandKind::VirtualReg)
                return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(0)}};
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
        case IROpcode::LT:
            if (sameOperands && lhs.kind == OperandKind::VirtualReg)
                return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(0)}};
            break;
        case IROpcode::LE:
            if (sameOperands && lhs.kind == OperandKind::VirtualReg)
                return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(1)}};
            break;
        case IROpcode::GT:
            if (sameOperands && lhs.kind == OperandKind::VirtualReg)
                return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(0)}};
            break;
        case IROpcode::GE:
            if (sameOperands && lhs.kind == OperandKind::VirtualReg)
                return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(1)}};
            break;
        case IROpcode::EQ:
            if (sameOperands && lhs.kind == OperandKind::VirtualReg)
                return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(1)}};
            break;
        case IROpcode::NE:
            if (sameOperands && lhs.kind == OperandKind::VirtualReg)
                return IRInstruction{IROpcode::ADD, {dest, IROperand::imm(0), IROperand::imm(0)}};
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

uint32_t maxRegId(const std::vector<IRInstruction>& insts) {
    uint32_t maxReg = 0;
    bool hasReg = false;
    for (const auto& inst : insts) {
        for (const auto& operand : inst.operands) {
            if (operand.kind == OperandKind::VirtualReg) {
                hasReg = true;
                maxReg = std::max(maxReg, regId(operand));
            }
        }
    }
    return hasReg ? maxReg : 0;
}

int maxLocalEnd(const std::vector<IRInstruction>& insts) {
    int maxEnd = 0;
    for (const auto& inst : insts) {
        if (inst.opcode == IROpcode::LOAD_LOCAL && inst.operands.size() >= 2 &&
            inst.operands[1].kind == OperandKind::Immediate) {
            maxEnd = std::max(maxEnd, immValue(inst.operands[1]) + 4);
        } else if (inst.opcode == IROpcode::STORE_LOCAL && !inst.operands.empty() &&
                   inst.operands[0].kind == OperandKind::Immediate) {
            maxEnd = std::max(maxEnd, immValue(inst.operands[0]) + 4);
        }
    }
    return maxEnd;
}

bool isInlineCandidate(const IRFunction& fn) {
    if (fn.name == "main" || fn.instructions.empty() || fn.instructions.size() > 80) {
        return false;
    }

    int retCount = 0;
    for (size_t i = 0; i < fn.instructions.size(); ++i) {
        const auto& inst = fn.instructions[i];
        switch (inst.opcode) {
            case IROpcode::LABEL:
            case IROpcode::JMP:
            case IROpcode::BEQ:
            case IROpcode::BNE:
            case IROpcode::PARAM:
            case IROpcode::CALL:
            case IROpcode::STORE_GLOBAL:
                return false;
            case IROpcode::RET:
                ++retCount;
                if (i + 1 != fn.instructions.size() || inst.operands.size() != 1) {
                    return false;
                }
                break;
            default:
                break;
        }
    }
    return retCount == 1;
}

IROperand remapInlineOperand(
    const IROperand& operand,
    std::unordered_map<uint32_t, uint32_t>& regMap,
    uint32_t& nextReg) {
    if (operand.kind != OperandKind::VirtualReg) return operand;
    uint32_t id = regId(operand);
    auto found = regMap.find(id);
    if (found == regMap.end()) {
        found = regMap.emplace(id, nextReg++).first;
    }
    return IROperand::reg(found->second);
}

int remapInlineLocal(
    int offset,
    std::unordered_map<int, int>& localMap,
    int& nextLocal) {
    auto found = localMap.find(offset);
    if (found == localMap.end()) {
        found = localMap.emplace(offset, nextLocal).first;
        nextLocal += 4;
    }
    return found->second;
}

IRInstruction remapInlineInstruction(
    const IRInstruction& inst,
    std::unordered_map<uint32_t, uint32_t>& regMap,
    std::unordered_map<int, int>& localMap,
    uint32_t& nextReg,
    int& nextLocal) {
    IRInstruction remapped = inst;
    for (auto& operand : remapped.operands) {
        operand = remapInlineOperand(operand, regMap, nextReg);
    }

    if (remapped.opcode == IROpcode::LOAD_LOCAL && remapped.operands.size() >= 2 &&
        remapped.operands[1].kind == OperandKind::Immediate) {
        remapped.operands[1] =
            IROperand::imm(remapInlineLocal(immValue(remapped.operands[1]), localMap, nextLocal));
    } else if (remapped.opcode == IROpcode::STORE_LOCAL && !remapped.operands.empty() &&
               remapped.operands[0].kind == OperandKind::Immediate) {
        remapped.operands[0] =
            IROperand::imm(remapInlineLocal(immValue(remapped.operands[0]), localMap, nextLocal));
    }

    return remapped;
}

std::vector<IRInstruction> inlineCall(
    const IRFunction& callee,
    const std::vector<IROperand>& params,
    const IROperand& callDest,
    uint32_t& nextReg,
    int& nextLocal) {
    std::vector<IRInstruction> result;
    result.reserve(callee.instructions.size() + params.size() + 1);

    std::unordered_map<uint32_t, uint32_t> regMap;
    std::unordered_map<int, int> localMap;

    for (size_t i = 0; i < params.size(); ++i) {
        uint32_t copiedParam = nextReg++;
        regMap[static_cast<uint32_t>(i)] = copiedParam;
        result.push_back(
            {IROpcode::ADD, {IROperand::reg(copiedParam), params[i], IROperand::imm(0)}});
    }

    for (const auto& inst : callee.instructions) {
        if (inst.opcode == IROpcode::RET) {
            IROperand value = remapInlineOperand(inst.operands[0], regMap, nextReg);
            result.push_back({IROpcode::ADD, {callDest, value, IROperand::imm(0)}});
            continue;
        }
        result.push_back(remapInlineInstruction(inst, regMap, localMap, nextReg, nextLocal));
    }

    return result;
}

IRProgram inlineSmallFunctions(const IRProgram& input) {
    IRProgram output = input;
    std::unordered_map<std::string, const IRFunction*> candidates;
    for (const auto& fn : output.functions) {
        if (isInlineCandidate(fn)) {
            candidates[fn.name] = &fn;
        }
    }
    if (candidates.empty()) return output;

    for (auto& fn : output.functions) {
        std::vector<IRInstruction> result;
        result.reserve(fn.instructions.size());
        std::vector<IROperand> pendingParams;
        uint32_t nextReg = maxRegId(fn.instructions) + 1;
        int nextLocal = maxLocalEnd(fn.instructions);

        auto flushParams = [&]() {
            for (const auto& param : pendingParams) {
                result.push_back({IROpcode::PARAM, {param}});
            }
            pendingParams.clear();
        };

        for (const auto& inst : fn.instructions) {
            if (inst.opcode == IROpcode::PARAM && inst.operands.size() == 1) {
                pendingParams.push_back(inst.operands[0]);
                continue;
            }

            if (inst.opcode == IROpcode::CALL && inst.operands.size() == 2 &&
                inst.operands[0].kind == OperandKind::VirtualReg &&
                inst.operands[1].kind == OperandKind::FuncName) {
                auto found = candidates.find(stringValue(inst.operands[1]));
                if (found != candidates.end() &&
                    pendingParams.size() == static_cast<size_t>(found->second->paramCount)) {
                    std::vector<IRInstruction> inlined =
                        inlineCall(*found->second, pendingParams, inst.operands[0], nextReg, nextLocal);
                    result.insert(result.end(), inlined.begin(), inlined.end());
                    pendingParams.clear();
                    continue;
                }
            }

            if (inst.opcode == IROpcode::CALL || inst.opcode == IROpcode::LABEL ||
                inst.opcode == IROpcode::JMP || inst.opcode == IROpcode::BEQ ||
                inst.opcode == IROpcode::BNE || inst.opcode == IROpcode::RET) {
                flushParams();
            }
            result.push_back(inst);
        }
        flushParams();
        fn.instructions = std::move(result);
    }

    return output;
}

IRProgram removeUncalledInternalFunctions(const IRProgram& input) {
    IRProgram output = input;
    bool hasMain = false;
    for (const auto& fn : output.functions) {
        if (fn.name == "main") {
            hasMain = true;
            break;
        }
    }
    if (!hasMain) return output;

    std::unordered_set<std::string> called;
    for (const auto& fn : output.functions) {
        for (const auto& inst : fn.instructions) {
            if (inst.opcode == IROpcode::CALL && inst.operands.size() >= 2 &&
                inst.operands[1].kind == OperandKind::FuncName) {
                called.insert(stringValue(inst.operands[1]));
            }
        }
    }

    std::vector<IRFunction> kept;
    kept.reserve(output.functions.size());
    for (auto& fn : output.functions) {
        if (fn.name == "main" || called.count(fn.name)) {
            kept.push_back(std::move(fn));
        }
    }
    output.functions = std::move(kept);
    return output;
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

bool hasControlFlow(const std::vector<IRInstruction>& insts) {
    for (const auto& inst : insts) {
        if (inst.opcode == IROpcode::LABEL || inst.opcode == IROpcode::JMP ||
            inst.opcode == IROpcode::BEQ || inst.opcode == IROpcode::BNE) {
            return true;
        }
    }
    return false;
}

std::vector<IRInstruction> removeDeadLocalStores(const std::vector<IRInstruction>& insts) {
    if (hasControlFlow(insts)) return insts;

    std::unordered_set<int32_t> liveLocals;
    std::vector<IRInstruction> reversed;
    reversed.reserve(insts.size());

    for (auto it = insts.rbegin(); it != insts.rend(); ++it) {
        const IRInstruction& inst = *it;
        if (inst.opcode == IROpcode::LOAD_LOCAL && inst.operands.size() >= 2 &&
            inst.operands[1].kind == OperandKind::Immediate) {
            liveLocals.insert(immValue(inst.operands[1]));
            reversed.push_back(inst);
            continue;
        }

        if (inst.opcode == IROpcode::STORE_LOCAL && !inst.operands.empty() &&
            inst.operands[0].kind == OperandKind::Immediate) {
            int32_t offset = immValue(inst.operands[0]);
            if (!liveLocals.count(offset)) {
                continue;
            }
            liveLocals.erase(offset);
        }

        reversed.push_back(inst);
    }

    std::reverse(reversed.begin(), reversed.end());
    return reversed;
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

std::vector<IRInstruction> removeSelfCopies(std::vector<IRInstruction> insts) {
    std::vector<IRInstruction> result;
    result.reserve(insts.size());

    for (size_t i = 0; i < insts.size(); ++i) {
        auto& inst = insts[i];
        // 移除 ADD %dest, %dest, 0 自复制（no-op）
        if (inst.opcode == IROpcode::ADD && inst.operands.size() == 3 &&
            inst.operands[1].kind == OperandKind::VirtualReg &&
            inst.operands[2].kind == OperandKind::Immediate &&
            immValue(inst.operands[2]) == 0 &&
            sameOperand(inst.operands[0], inst.operands[1])) {
            continue;
        }
        // 移除连续 STORE_LOCAL 到同一偏移的冗余存储（只保留最后一个）
        if (inst.opcode == IROpcode::STORE_LOCAL && inst.operands.size() == 2 &&
            inst.operands[0].kind == OperandKind::Immediate) {
            int32_t offset = immValue(inst.operands[0]);
            size_t j = i + 1;
            while (j < insts.size() &&
                   insts[j].opcode == IROpcode::STORE_LOCAL &&
                   insts[j].operands.size() == 2 &&
                   insts[j].operands[0].kind == OperandKind::Immediate &&
                   immValue(insts[j].operands[0]) == offset) {
                ++j;
            }
            if (j > i + 1) {
                // 跳过中间的所有 STORE_LOCAL，只保留最后一个写入
                // 但需要确保它们之间没有 LOAD_LOCAL 读取该偏移
                bool hasInterveningLoad = false;
                for (size_t k = i + 1; k < j; ++k) {
                    if (insts[k].opcode == IROpcode::LOAD_LOCAL &&
                        insts[k].operands.size() == 2 &&
                        insts[k].operands[1].kind == OperandKind::Immediate &&
                        immValue(insts[k].operands[1]) == offset) {
                        hasInterveningLoad = true;
                        break;
                    }
                    if (insts[k].opcode == IROpcode::CALL) {
                        hasInterveningLoad = true;  // 保守
                        break;
                    }
                }
                if (!hasInterveningLoad) {
                    // 跳过前面的冗余存储，只 push 最后一个
                    for (size_t k = i; k < j - 1; ++k) {
                        // 跳过
                    }
                    i = j - 1;
                    result.push_back(std::move(insts[i]));
                    continue;
                }
            }
        }
        result.push_back(std::move(inst));
    }

    return result;
}

// ============================================================
// 基本块划分
// ============================================================

struct BasicBlock {
    size_t startIdx;       // 起始指令索引
    size_t endIdx;         // 结束指令索引（不包含）
    uint32_t labelId;      // 入口标签 id（0 表示无标签）
    bool hasLabel;
};

std::vector<BasicBlock> splitBasicBlocks(const std::vector<IRInstruction>& insts) {
    std::vector<BasicBlock> blocks;
    if (insts.empty()) return blocks;

    size_t start = 0;
    uint32_t entryLabel = 0;
    bool hasLabel = false;

    for (size_t i = 0; i < insts.size(); ++i) {
        if (i > start && insts[i].opcode == IROpcode::LABEL && insts[i].operands.size() == 1) {
            blocks.push_back({start, i, entryLabel, hasLabel});
            start = i;
            entryLabel = labelId(insts[i].operands[0]);
            hasLabel = true;
        } else if (insts[i].opcode == IROpcode::JMP ||
                   insts[i].opcode == IROpcode::BEQ ||
                   insts[i].opcode == IROpcode::BNE ||
                   insts[i].opcode == IROpcode::RET) {
            blocks.push_back({start, i + 1, entryLabel, hasLabel});
            start = i + 1;
            entryLabel = 0;
            hasLabel = false;
            if (i + 1 < insts.size() && insts[i + 1].opcode != IROpcode::LABEL) {
                hasLabel = false;
            }
        }
    }

    if (start < insts.size()) {
        blocks.push_back({start, insts.size(), entryLabel, hasLabel});
    }

    return blocks;
}

// ============================================================
// 按基本块消除死存储（支持控制流）
// ============================================================

std::vector<IRInstruction> removeDeadLocalStoresPerBlock(std::vector<IRInstruction> insts) {
    auto blocks = splitBasicBlocks(insts);
    if (blocks.size() <= 1) {
        // 单基本块：使用原有逻辑
        return removeDeadLocalStores(std::move(insts));
    }

    std::unordered_set<int32_t> allLoaded;
    for (const auto& inst : insts) {
        if (inst.opcode == IROpcode::LOAD_LOCAL && inst.operands.size() >= 2 &&
            inst.operands[1].kind == OperandKind::Immediate) {
            allLoaded.insert(immValue(inst.operands[1]));
        }
    }

    if (allLoaded.empty()) return insts;

    std::vector<IRInstruction> result;
    result.reserve(insts.size());

    for (const auto& block : blocks) {
        std::unordered_set<int32_t> liveLocals;
        std::vector<IRInstruction> blockInsts(
            insts.begin() + static_cast<ptrdiff_t>(block.startIdx),
            insts.begin() + static_cast<ptrdiff_t>(block.endIdx));

        // 反向遍历块内指令，标记活跃局部变量
        for (auto it = blockInsts.rbegin(); it != blockInsts.rend(); ++it) {
            const IRInstruction& inst = *it;

            if (inst.opcode == IROpcode::CALL || inst.opcode == IROpcode::STORE_GLOBAL) {
                // 函数调用/全局存储：保守地标记所有被加载过的局部变量为活跃
                for (int32_t offset : allLoaded) {
                    liveLocals.insert(offset);
                }
                continue;
            }

            if (inst.opcode == IROpcode::LOAD_LOCAL && inst.operands.size() >= 2 &&
                inst.operands[1].kind == OperandKind::Immediate) {
                liveLocals.insert(immValue(inst.operands[1]));
                continue;
            }
        }

        // 正向遍历，只保留活跃存储
        for (size_t i = 0; i < blockInsts.size(); ++i) {
            auto& inst = blockInsts[i];
            if (inst.opcode == IROpcode::STORE_LOCAL && !inst.operands.empty() &&
                inst.operands[0].kind == OperandKind::Immediate) {
                int32_t offset = immValue(inst.operands[0]);
                if (!liveLocals.count(offset) && !allLoaded.count(offset)) {
                    continue;
                }
            }
            result.push_back(std::move(inst));
        }
    }

    return result;
}

// ============================================================
//  循环检测
// ============================================================

struct LoopInfo {
    uint32_t headerLabel;        // 循环头标签
    size_t headerIdx;            // 循环头在指令列表中的位置
    size_t latchIdx;             // 回边指令的位置
    size_t bodyStart;            // 循环体起始（通常在 header 之后）
    size_t bodyEnd;              // 循环体结束（回边之后）
    std::unordered_set<size_t> bodyIndices;  // 循环体内所有指令下标
};

std::vector<LoopInfo> detectLoops(const std::vector<IRInstruction>& insts) {
    std::vector<LoopInfo> loops;
    std::unordered_map<uint32_t, size_t> labelPositions;

    for (size_t i = 0; i < insts.size(); ++i) {
        if (insts[i].opcode == IROpcode::LABEL && insts[i].operands.size() == 1) {
            labelPositions[labelId(insts[i].operands[0])] = i;
        }
    }

    for (size_t i = 0; i < insts.size(); ++i) {
        if (insts[i].opcode != IROpcode::JMP) continue;
        if (insts[i].operands.size() != 1) continue;

        uint32_t target = labelId(insts[i].operands[0]);
        auto found = labelPositions.find(target);
        if (found == labelPositions.end() || found->second >= i) continue;

        // 找到回边
        LoopInfo loop;
        loop.headerLabel = target;
        loop.headerIdx = found->second;
        loop.latchIdx = i;

        // 找到 bodyStart（header 之后第一个非 label 指令）
        size_t bodyStart = loop.headerIdx + 1;
        loop.bodyStart = bodyStart;
        loop.bodyEnd = i + 1;

        // 收集循环体指令
        for (size_t j = bodyStart; j <= i; ++j) {
            loop.bodyIndices.insert(j);
        }

        loops.push_back(std::move(loop));
    }

    return loops;
}

// ============================================================
// 循环不变量外提 (LICM)
// ============================================================

bool isLoopInvariant(
    const IROperand& operand,
    const std::unordered_set<size_t>& loopBody,
    const std::unordered_map<uint32_t, size_t>& defPositions) {
    if (operand.kind == OperandKind::Immediate) return true;
    if (operand.kind != OperandKind::VirtualReg) return false;

    uint32_t id = regId(operand);
    auto found = defPositions.find(id);
    if (found == defPositions.end()) return true;  // 未定义（参数等）→ 不变
    return !loopBody.count(found->second);         // 定义在循环外 → 不变
}

bool instIsLoopInvariant(
    const IRInstruction& inst, size_t instIdx,
    const std::unordered_set<size_t>& loopBody,
    const std::unordered_map<uint32_t, size_t>& defPositions,
    const std::unordered_set<uint32_t>& invariantRegs) {
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
            if (inst.operands.size() != 3) return false;
            return (isLoopInvariant(inst.operands[1], loopBody, defPositions) ||
                    invariantRegs.count(inst.operands[1].kind == OperandKind::VirtualReg ?
                        regId(inst.operands[1]) : 0)) &&
                   (isLoopInvariant(inst.operands[2], loopBody, defPositions) ||
                    invariantRegs.count(inst.operands[2].kind == OperandKind::VirtualReg ?
                        regId(inst.operands[2]) : 0));
        case IROpcode::NEG:
        case IROpcode::NOT:
            if (inst.operands.size() != 2) return false;
            return isLoopInvariant(inst.operands[1], loopBody, defPositions) ||
                   invariantRegs.count(inst.operands[1].kind == OperandKind::VirtualReg ?
                        regId(inst.operands[1]) : 0);
        case IROpcode::LOAD_LOCAL:
            if (inst.operands.size() != 2) return false;
            return isLoopInvariant(inst.operands[1], loopBody, defPositions);
        case IROpcode::LOAD_GLOBAL:
            return true;  // 全局变量的地址在循环中不变（没有 STORE_GLOBAL 时）
        default:
            return false;
    }
}

std::vector<IRInstruction> hoistLoopInvariants(std::vector<IRInstruction> insts) {
    // 迭代处理：每次只处理最内层一个循环，然后重新检测
    // 避免多循环时索引失效的问题
    bool changed = true;
    while (changed) {
        changed = false;
        auto loops = detectLoops(insts);
        if (loops.empty()) break;

        // 从最内层循环开始：只取第一个（最小体）
        std::sort(loops.begin(), loops.end(), [](const LoopInfo& a, const LoopInfo& b) {
            return (a.bodyEnd - a.bodyStart) < (b.bodyEnd - b.bodyStart);
        });
        const auto& loop = loops[0];

        // 构建定义位置映射
        std::unordered_map<uint32_t, size_t> defPositions;
        for (size_t i = 0; i < insts.size(); ++i) {
            auto dest = destReg(insts[i]);
            if (dest) defPositions[*dest] = i;
        }

        // 检查循环中是否有 STORE_GLOBAL 或 CALL（有副作用的不安全）
        bool hasSideEffect = false;
        for (size_t i = loop.bodyStart; i < loop.bodyEnd; ++i) {
            const auto& inst = insts[i];
            if (inst.opcode == IROpcode::STORE_GLOBAL ||
                (inst.opcode == IROpcode::CALL && i < loop.bodyEnd - 1)) {
                hasSideEffect = true;
                break;
            }
        }
        if (hasSideEffect) continue;

        // 迭代计算不变量集合
        std::unordered_set<uint32_t> invariantRegs;
        bool invChanged = true;
        while (invChanged) {
            invChanged = false;
            for (size_t i = loop.bodyStart; i < loop.bodyEnd; ++i) {
                const auto& inst = insts[i];
                auto dest = destReg(inst);
                if (!dest || invariantRegs.count(*dest)) continue;

                if (instIsLoopInvariant(inst, i, loop.bodyIndices, defPositions, invariantRegs)) {
                    invariantRegs.insert(*dest);
                    invChanged = true;
                }
            }
        }

        if (invariantRegs.empty()) continue;

        // 收集循环体中 STORE_LOCAL 的偏移量，用于安全的 LOAD_LOCAL 外提判断
        std::unordered_set<int32_t> storedLocalsInLoop;
        for (size_t i = loop.bodyStart; i < loop.bodyEnd; ++i) {
            if (insts[i].opcode == IROpcode::STORE_LOCAL && insts[i].operands.size() >= 2 &&
                insts[i].operands[0].kind == OperandKind::Immediate) {
                storedLocalsInLoop.insert(immValue(insts[i].operands[0]));
            }
        }

        // 收集要外提的指令（保持原有顺序）
        std::vector<IRInstruction> hoisted;
        std::unordered_set<size_t> hoistedIndices;

        for (size_t i = loop.bodyStart; i < loop.bodyEnd; ++i) {
            auto dest = destReg(insts[i]);
            if (!dest || !invariantRegs.count(*dest)) continue;
            if (!isRemovableValueInst(insts[i])) continue;

            // LOAD_LOCAL: 只有当循环体内没有 STORE_LOCAL 到同一偏移时才安全外提
            if (insts[i].opcode == IROpcode::LOAD_LOCAL) {
                if (insts[i].operands.size() >= 2 &&
                    insts[i].operands[1].kind == OperandKind::Immediate) {
                    int32_t offset = immValue(insts[i].operands[1]);
                    if (storedLocalsInLoop.count(offset)) continue;
                } else {
                    continue;
                }
            }

            hoisted.push_back(insts[i]);
            hoistedIndices.insert(i);
        }

        if (hoisted.empty()) continue;

        // 创建 preheader 标签并重组指令
        uint32_t preheaderLabel = maxLabelId(insts) + 1;

        std::vector<IRInstruction> result;
        result.reserve(insts.size() + hoisted.size() + 2);

        // 复制 loop header 之前的指令
        for (size_t i = 0; i < loop.headerIdx; ++i) {
            result.push_back(std::move(insts[i]));
        }

        // 插入 preheader
        result.push_back({IROpcode::LABEL, {IROperand::label(preheaderLabel)}});
        for (auto& inst : hoisted) {
            result.push_back(std::move(inst));
        }
        result.push_back({IROpcode::JMP, {IROperand::label(loop.headerLabel)}});

        // 复制循环体（跳过外提的指令）
        for (size_t i = loop.headerIdx; i < loop.bodyEnd; ++i) {
            if (!hoistedIndices.count(i)) {
                result.push_back(std::move(insts[i]));
            }
        }

        // 复制循环之后的指令
        for (size_t i = loop.bodyEnd; i < insts.size(); ++i) {
            result.push_back(std::move(insts[i]));
        }

        // 修改循环外的跳转目标：header → preheader
        // preheader 结构：[LABEL preheader] [hoisted...] [JMP header]
        // preheader 自身的 JMP 不应被重定向（它应该跳回原始循环头）
        size_t preheaderEnd = loop.headerIdx + 1 + hoisted.size() + 1;
        size_t preheaderJmpIdx = preheaderEnd - 1;  // preheader 末尾的 JMP
        for (size_t i = 0; i < result.size(); ++i) {
            auto& inst = result[i];
            if (i >= preheaderEnd) break;  // preheader 之后的都是循环体/后置指令，不用改
            if (i == preheaderJmpIdx) continue;  // 跳过 preheader 自身的 JMP
            if ((inst.opcode == IROpcode::JMP || inst.opcode == IROpcode::BEQ ||
                 inst.opcode == IROpcode::BNE) && !inst.operands.empty()) {
                IROperand& target = inst.operands.back();
                if (target.kind == OperandKind::Label &&
                    labelId(target) == loop.headerLabel) {
                    target = IROperand::label(preheaderLabel);
                }
            }
        }

        insts = std::move(result);
        changed = true;
    }

    return insts;
}

// ============================================================
// 循环展开
// ============================================================

struct InductionInfo {
    uint32_t loadReg;    // 加载归纳变量的 vreg
    uint32_t incReg;     // 递增后的 vreg
    int32_t localOffset; // 局部变量偏移
    int32_t increment;   // 递增量
    int32_t limit;       // 上限（如果是已知常量）
    bool hasLimit;       // 是否已知上限
    size_t condIdx;      // 比较指令位置
    size_t branchIdx;    // 分支指令位置
    size_t incIdx;       // 递增指令位置
    size_t storeIdx;     // 存储指令位置
};

std::optional<InductionInfo> detectInductionVar(
    const std::vector<IRInstruction>& insts, const LoopInfo& loop) {
    InductionInfo info{};
    info.hasLimit = false;
    info.limit = 0;
    bool foundInc = false;

    // 查找 STORE_LOCAL %offset, %inc 模式（循环体末尾）
    for (size_t i = loop.bodyEnd - 1; i >= loop.bodyStart && i != static_cast<size_t>(-1); --i) {
        const auto& inst = insts[i];
        if (inst.opcode == IROpcode::STORE_LOCAL && inst.operands.size() == 2 &&
            inst.operands[0].kind == OperandKind::Immediate &&
            inst.operands[1].kind == OperandKind::VirtualReg) {

            int32_t offset = immValue(inst.operands[0]);
            uint32_t incReg = regId(inst.operands[1]);

            // 查找 ADD %incReg, %loadReg, %increment
            for (size_t j = i; j >= loop.bodyStart && j != static_cast<size_t>(-1); --j) {
                const auto& prevInst = insts[j];
                if (prevInst.opcode == IROpcode::ADD && prevInst.operands.size() == 3 &&
                    prevInst.operands[0].kind == OperandKind::VirtualReg &&
                    regId(prevInst.operands[0]) == incReg &&
                    prevInst.operands[2].kind == OperandKind::Immediate) {
                    info.loadReg = regId(prevInst.operands[1]);
                    info.increment = immValue(prevInst.operands[2]);
                    info.incReg = incReg;
                    info.localOffset = offset;
                    info.incIdx = j;
                    info.storeIdx = i;
                    foundInc = true;
                    break;
                }
                // 也匹配 lhs 为立即数的情况
                if (prevInst.opcode == IROpcode::ADD && prevInst.operands.size() == 3 &&
                    prevInst.operands[0].kind == OperandKind::VirtualReg &&
                    regId(prevInst.operands[0]) == incReg &&
                    prevInst.operands[1].kind == OperandKind::Immediate) {
                    info.loadReg = regId(prevInst.operands[2]);
                    info.increment = immValue(prevInst.operands[1]);
                    info.incReg = incReg;
                    info.localOffset = offset;
                    info.incIdx = j;
                    info.storeIdx = i;
                    foundInc = true;
                    break;
                }
            }
            if (foundInc) break;
        }
    }

    if (!foundInc) return std::nullopt;

    // 查找比较与分支
    for (size_t i = loop.bodyStart; i < loop.bodyEnd; ++i) {
        const auto& inst = insts[i];
        if ((inst.opcode == IROpcode::LT || inst.opcode == IROpcode::LE) &&
            inst.operands.size() == 3 &&
            inst.operands[0].kind == OperandKind::VirtualReg) {
            uint32_t dest = regId(inst.operands[0]);

            // 查找使用此 cmpReg 的分支
            for (size_t j = i + 1; j < loop.bodyEnd; ++j) {
                if ((insts[j].opcode == IROpcode::BEQ || insts[j].opcode == IROpcode::BNE) &&
                    insts[j].operands.size() == 3 &&
                    insts[j].operands[0].kind == OperandKind::VirtualReg &&
                    regId(insts[j].operands[0]) == dest) {
                    info.condIdx = i;
                    info.branchIdx = j;

                    // 检查上限
                    if (inst.operands[2].kind == OperandKind::Immediate) {
                        info.limit = immValue(inst.operands[2]);
                        info.hasLimit = true;
                    }
                    // 检查是否比较的是归纳变量
                    bool isInduction = false;
                    if (inst.operands[1].kind == OperandKind::VirtualReg &&
                        regId(inst.operands[1]) == info.loadReg) {
                        isInduction = true;
                    }
                    if (isInduction) return info;
                    break;
                }
            }
        }
    }

    return std::nullopt;
}

std::vector<IRInstruction> unrollLoops(std::vector<IRInstruction> insts) {
    // 迭代处理：每次只展开最内层一个循环，然后重新检测
    bool changed = true;
    while (changed) {
        changed = false;
        auto loops = detectLoops(insts);
        if (loops.empty()) break;

        // 从内层到外层，只取第一个（最内层）
        std::sort(loops.begin(), loops.end(), [](const LoopInfo& a, const LoopInfo& b) {
            return (a.bodyEnd - a.bodyStart) < (b.bodyEnd - b.bodyStart);
        });
        const auto& loop = loops[0];

        auto indVar = detectInductionVar(insts, loop);
        if (!indVar) continue;
        if (!indVar->hasLimit) continue;

        int32_t limit = indVar->limit;
        int32_t increment = indVar->increment;
        if (increment <= 0) continue;
        int64_t tripCount = (limit + increment - 1) / increment;

        // 只有迭代次数 >= 4 的循环值得展开
        if (tripCount < 4) continue;
        if (tripCount > 10000) continue;

        // 检查循环体中没有 CALL 或嵌套循环
        bool hasCall = false;
        for (size_t i = loop.bodyStart; i < loop.bodyEnd; ++i) {
            if (insts[i].opcode == IROpcode::CALL) { hasCall = true; break; }
        }
        if (hasCall) continue;

        // 展开因子
        int unrollFactor = 4;
        if (tripCount < 8) unrollFactor = 2;

        int64_t unrolledCount = tripCount / unrollFactor;
        if (unrolledCount < 1) continue;

        // 收集循环体指令（不包括末尾的 JMP）
        size_t bodyRealEnd = loop.bodyEnd;
        if (bodyRealEnd > 0 && insts[bodyRealEnd - 1].opcode == IROpcode::JMP) {
            bodyRealEnd--;
        }
        std::vector<IRInstruction> bodyCopy(
            insts.begin() + static_cast<ptrdiff_t>(loop.bodyStart),
            insts.begin() + static_cast<ptrdiff_t>(bodyRealEnd));

        // 分配新的 vreg 和 label ID
        uint32_t freshVReg = maxRegId(insts) + 1;
        uint32_t freshLabel = maxLabelId(insts) + 1;

        auto cloneBody = [&](std::unordered_map<uint32_t, uint32_t>& localMap) -> std::vector<IRInstruction> {
            std::vector<IRInstruction> cloned;
            for (auto inst : bodyCopy) {
                for (auto& op : inst.operands) {
                    if (op.kind == OperandKind::VirtualReg) {
                        uint32_t id = regId(op);
                        auto found = localMap.find(id);
                        if (found == localMap.end()) {
                            found = localMap.emplace(id, freshVReg++).first;
                        }
                        op = IROperand::reg(found->second);
                    }
                }
                cloned.push_back(std::move(inst));
            }
            return cloned;
        };

        uint32_t newHeader = freshLabel++;
        uint32_t newBodyLabel = freshLabel++;
        uint32_t newExit = freshLabel++;

        std::vector<IRInstruction> result;

        // header 之前的指令
        for (size_t i = 0; i < loop.headerIdx; ++i) {
            result.push_back(std::move(insts[i]));
        }

        // 新 header：直接跳入展开体
        result.push_back({IROpcode::LABEL, {IROperand::label(newHeader)}});
        result.push_back({IROpcode::JMP, {IROperand::label(newBodyLabel)}});

        // 展开的循环体
        result.push_back({IROpcode::LABEL, {IROperand::label(newBodyLabel)}});

        // 生成 unrolledCount 次 x unrollFactor 展开体
        for (int64_t uc = 0; uc < unrolledCount; ++uc) {
            for (int u = 0; u < unrollFactor; ++u) {
                std::unordered_map<uint32_t, uint32_t> localRegMap;
                auto cloned = cloneBody(localRegMap);
                for (auto& inst : cloned) {
                    result.push_back(std::move(inst));
                }
            }

            // 中间检查：如果还有更多展开迭代，检查是否继续
            if (uc + 1 < unrolledCount) {
                result.push_back(
                    {IROpcode::LOAD_LOCAL,
                     {IROperand::reg(freshVReg), IROperand::imm(indVar->localOffset)}});
                int32_t unrollStep = increment * unrollFactor;
                result.push_back(
                    {IROpcode::ADD,
                     {IROperand::reg(freshVReg + 1),
                      IROperand::reg(freshVReg),
                      IROperand::imm(unrollStep)}});
                result.push_back(
                    {IROpcode::LT,
                     {IROperand::reg(freshVReg + 2),
                      IROperand::reg(freshVReg + 1),
                      IROperand::imm(limit)}});
                result.push_back(
                    {IROpcode::BNE,
                     {IROperand::reg(freshVReg + 2), IROperand::imm(0),
                      IROperand::label(newBodyLabel)}});
                freshVReg += 3;
            }
        }

        // 跳转到原始循环处理剩余迭代
        result.push_back({IROpcode::JMP, {IROperand::label(loop.headerLabel)}});

        // 新 exit label
        result.push_back({IROpcode::LABEL, {IROperand::label(newExit)}});

        // 复制循环之后的指令
        for (size_t i = loop.bodyEnd; i < insts.size(); ++i) {
            result.push_back(std::move(insts[i]));
        }

        // 修改跳转目标：原来跳出循环的 → newExit
        uint32_t exitLabel = 0;
        for (size_t i = loop.headerIdx; i < loop.bodyEnd; ++i) {
            const auto& inst = insts[i];
            if ((inst.opcode == IROpcode::BEQ || inst.opcode == IROpcode::BNE) &&
                inst.operands.size() == 3) {
                uint32_t tgt = labelId(inst.operands[2]);
                bool inLoop = false;
                for (size_t j = loop.headerIdx; j < loop.bodyEnd; ++j) {
                    if (insts[j].opcode == IROpcode::LABEL && insts[j].operands.size() == 1 &&
                        labelId(insts[j].operands[0]) == tgt) {
                        inLoop = true;
                        break;
                    }
                }
                if (!inLoop) { exitLabel = tgt; break; }
            }
        }

        for (size_t i = 0; i < result.size(); ++i) {
            auto& inst = result[i];
            if ((inst.opcode == IROpcode::JMP || inst.opcode == IROpcode::BEQ ||
                 inst.opcode == IROpcode::BNE) && !inst.operands.empty()) {
                IROperand& target = inst.operands.back();
                if (target.kind == OperandKind::Label) {
                    uint32_t tgt = labelId(target);
                    if (exitLabel != 0 && tgt == exitLabel) {
                        target = IROperand::label(newExit);
                    } else if (tgt == loop.headerLabel) {
                        // 跳过 epilogue JMP：它紧邻 newExit 标签之前，应保持跳向原始循环头
                        if (i + 1 < result.size() &&
                            result[i + 1].opcode == IROpcode::LABEL &&
                            result[i + 1].operands.size() == 1 &&
                            labelId(result[i + 1].operands[0]) == newExit) {
                            continue;
                        }
                        target = IROperand::label(newHeader);
                    }
                }
            }
        }

        insts = std::move(result);
        changed = true;
    }

    return insts;
}

}  // namespace

IRProgram Optimizer::optimize(const IRProgram& input) {
    IRProgram output = input;

    for (int pass = 0; pass < 2; ++pass) {
        output = inlineSmallFunctions(output);
    }

    std::unordered_map<std::string, int32_t> globalConstants;
    for (const auto& global : output.globals) {
        if (global.isConst) {
            globalConstants[global.name] = global.initialValue;
        }
    }

    for (auto& fn : output.functions) {
        std::unordered_map<uint32_t, int32_t> constants;
        std::unordered_map<int32_t, int32_t> localConstants;
        std::vector<IRInstruction> optimized;
        bool unreachable = false;

        for (auto inst : fn.instructions) {
            if (unreachable && inst.opcode != IROpcode::LABEL) {
                continue;
            }
            if (inst.opcode == IROpcode::LABEL) {
                unreachable = false;
                constants.clear();
                localConstants.clear();
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
                        if (inst.operands.size() == 2 &&
                            inst.operands[1].kind == OperandKind::Immediate) {
                            auto found = localConstants.find(immValue(inst.operands[1]));
                            if (found != localConstants.end()) {
                                inst = {IROpcode::ADD,
                                        {inst.operands[0], IROperand::imm(0), IROperand::imm(found->second)}};
                                rememberDest(inst, constants, found->second);
                                break;
                            }
                        }
                        rememberDest(inst, constants, std::nullopt);
                        break;
                    case IROpcode::STORE_LOCAL:
                        if (inst.operands.size() >= 2 &&
                            inst.operands[0].kind == OperandKind::Immediate) {
                            auto value = constValue(inst.operands[1], constants);
                            if (value) {
                                localConstants[immValue(inst.operands[0])] = *value;
                            } else {
                                localConstants.erase(immValue(inst.operands[0]));
                            }
                        }
                        break;
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
                        localConstants.clear();
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
                        // 相同操作数: BEQ x,x → 始终跳转; BNE x,x → 始终不跳转
                        if (inst.operands.size() == 3 &&
                            sameOperand(inst.operands[0], inst.operands[1])) {
                            if (inst.opcode == IROpcode::BEQ) {
                                inst = {IROpcode::JMP, {inst.operands[2]}};
                            } else {
                                constants.clear();
                                continue;
                            }
                        }
                        constants.clear();
                        localConstants.clear();
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

        // 循环优化：LICM + 循环展开（在 copy/CSE/DCE 之前）
        fn.instructions = hoistLoopInvariants(std::move(fn.instructions));
        fn.instructions = unrollLoops(std::move(fn.instructions));

        for (int pass = 0; pass < 3; ++pass) {
            size_t before = fn.instructions.size();
            fn.instructions = propagateCopiesAndCse(std::move(fn.instructions));
            fn.instructions = removeDeadValueInsts(fn.instructions);
            fn.instructions = removeDeadLocalStoresPerBlock(fn.instructions);
            fn.instructions = removeSelfCopies(std::move(fn.instructions));
            fn.instructions = invertBranchOverJump(std::move(fn.instructions));
            fn.instructions = rewriteJumpChains(std::move(fn.instructions));
            if (fn.instructions.size() == before) break;
        }

        // 复制传播/CSE/DCE 后重新运行常量折叠，利用新揭示的常量
        {
            std::unordered_map<uint32_t, int32_t> constants;
            std::unordered_map<int32_t, int32_t> localConstants;
            std::vector<IRInstruction> reoptimized;
            bool unreachable = false;

            for (auto inst : fn.instructions) {
                if (unreachable && inst.opcode != IROpcode::LABEL) {
                    continue;
                }
                if (inst.opcode == IROpcode::LABEL) {
                    unreachable = false;
                    constants.clear();
                    localConstants.clear();
                    reoptimized.push_back(std::move(inst));
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
                            if (inst.operands.size() == 2 &&
                                inst.operands[1].kind == OperandKind::Immediate) {
                                auto found = localConstants.find(immValue(inst.operands[1]));
                                if (found != localConstants.end()) {
                                    inst = {IROpcode::ADD,
                                            {inst.operands[0], IROperand::imm(0), IROperand::imm(found->second)}};
                                    rememberDest(inst, constants, found->second);
                                    break;
                                }
                            }
                            rememberDest(inst, constants, std::nullopt);
                            break;
                        case IROpcode::STORE_LOCAL:
                            if (inst.operands.size() >= 2 &&
                                inst.operands[0].kind == OperandKind::Immediate) {
                                auto value = constValue(inst.operands[1], constants);
                                if (value) {
                                    localConstants[immValue(inst.operands[0])] = *value;
                                } else {
                                    localConstants.erase(immValue(inst.operands[0]));
                                }
                            }
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
                            localConstants.clear();
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
                            // 相同操作数: BEQ x,x → 始终跳转; BNE x,x → 始终不跳转
                            if (inst.operands.size() == 3 &&
                                sameOperand(inst.operands[0], inst.operands[1])) {
                                if (inst.opcode == IROpcode::BEQ) {
                                    inst = {IROpcode::JMP, {inst.operands[2]}};
                                } else {
                                    constants.clear();
                                    continue;
                                }
                            }
                            constants.clear();
                            localConstants.clear();
                            break;
                        }
                        default:
                            break;
                    }
                }

                if (inst.opcode == IROpcode::RET) {
                    unreachable = true;
                }
                reoptimized.push_back(std::move(inst));
            }
            fn.instructions = std::move(reoptimized);
        }

        fn.instructions = rewriteTailRecursion(fn);
        fn.instructions = invertBranchOverJump(std::move(fn.instructions));
        fn.instructions = rewriteJumpChains(std::move(fn.instructions));

        // 尾递归转换后重新清理死代码
        fn.instructions = removeDeadValueInsts(fn.instructions);
        fn.instructions = removeDeadLocalStoresPerBlock(fn.instructions);
        fn.instructions = removeSelfCopies(std::move(fn.instructions));
        fn.instructions = invertBranchOverJump(std::move(fn.instructions));
        fn.instructions = rewriteJumpChains(std::move(fn.instructions));
    }

    output = removeUncalledInternalFunctions(output);
    return output;
}

}  // namespace toyc
