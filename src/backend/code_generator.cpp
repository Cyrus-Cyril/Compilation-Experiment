// ToyC RISC-V32 Code Generator 实现

#include "toyc/backend/code_generator.h"

#include <cstdint>
#include <algorithm>
#include <sstream>
#include <string>
#include <optional>
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

std::string labelName(const std::string& functionName, const IROperand& operand) {
    return ".L" + functionName + "_" + std::to_string(std::get<uint32_t>(operand.value));
}

int align16(int value) {
    return (value + 15) / 16 * 16;
}

bool fits12(int32_t value) {
    return value >= -2048 && value <= 2047;
}

bool isPowerOfTwo(int32_t value) {
    return value > 1 && (value & (value - 1)) == 0;
}

int ilog2(int32_t value) {
    int result = 0;
    while (value > 1) {
        value >>= 1;
        ++result;
    }
    return result;
}

struct FunctionLayout {
    int frameSize = 16;
    int localSize = 0;
    int vregBase = 0;
    int vregCount = 0;
    int saveBase = 0;
    int raOffset = 12;
    bool hasCall = false;
    bool saveRa = true;
    std::unordered_map<int, std::string> localRegs;
    std::unordered_map<uint32_t, std::string> vregRegs;
    std::vector<std::string> savedRegs;
};

struct EmitState {
    std::unordered_map<uint32_t, std::string> vregAliases;
    std::unordered_map<uint32_t, int> localAliases;
    std::unordered_map<uint32_t, int> remainingUses;
};

void scanOperand(const IROperand& operand, uint32_t& maxReg) {
    if (operand.kind == OperandKind::VirtualReg) {
        maxReg = std::max(maxReg, regId(operand));
    }
}

void countUse(const IROperand& operand, std::unordered_map<uint32_t, int>& uses) {
    if (operand.kind == OperandKind::VirtualReg) {
        ++uses[regId(operand)];
    }
}

std::unordered_map<uint32_t, int> buildUseCounts(const IRFunction& fn) {
    std::unordered_map<uint32_t, int> uses;
    for (const auto& inst : fn.instructions) {
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
                    countUse(inst.operands[1], uses);
                    countUse(inst.operands[2], uses);
                }
                break;
            case IROpcode::NEG:
            case IROpcode::NOT:
                if (inst.operands.size() >= 2) countUse(inst.operands[1], uses);
                break;
            case IROpcode::PARAM:
            case IROpcode::RET:
                if (!inst.operands.empty()) countUse(inst.operands[0], uses);
                break;
            case IROpcode::STORE_LOCAL:
            case IROpcode::STORE_GLOBAL:
                if (inst.operands.size() >= 2) countUse(inst.operands[1], uses);
                break;
            case IROpcode::BEQ:
            case IROpcode::BNE:
                if (inst.operands.size() >= 2) {
                    countUse(inst.operands[0], uses);
                    countUse(inst.operands[1], uses);
                }
                break;
            default:
                break;
        }
    }
    return uses;
}

template <typename K>
std::vector<std::pair<K, int>> sortedHotEntries(const std::unordered_map<K, int>& counts) {
    std::vector<std::pair<K, int>> entries(counts.begin(), counts.end());
    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second != rhs.second) return lhs.second > rhs.second;
        return lhs.first < rhs.first;
    });
    return entries;
}

FunctionLayout buildLayout(const IRFunction& fn) {
    uint32_t maxReg = 0;
    bool hasReg = false;
    bool hasCall = false;
    int maxLocalEnd = 0;
    std::unordered_map<int, int> localHeat;
    std::unordered_map<uint32_t, int> vregHeat;

    // 循环深度估算：检测回边并标记循环体指令
    std::vector<int> loopDepths(fn.instructions.size(), 0);
    {
        std::unordered_map<uint32_t, size_t> labelPos;
        for (size_t i = 0; i < fn.instructions.size(); ++i) {
            if (fn.instructions[i].opcode == IROpcode::LABEL &&
                !fn.instructions[i].operands.empty() &&
                fn.instructions[i].operands[0].kind == OperandKind::Label) {
                labelPos[labelId(fn.instructions[i].operands[0])] = i;
            }
        }
        for (size_t i = 0; i < fn.instructions.size(); ++i) {
            const auto& inst = fn.instructions[i];
            if ((inst.opcode == IROpcode::BEQ || inst.opcode == IROpcode::BNE ||
                 inst.opcode == IROpcode::JMP) && !inst.operands.empty() &&
                inst.operands.back().kind == OperandKind::Label) {
                uint32_t tid = labelId(inst.operands.back());
                auto found = labelPos.find(tid);
                if (found != labelPos.end() && found->second < i) {
                    for (size_t j = found->second; j <= i && j < loopDepths.size(); ++j) {
                        loopDepths[j]++;
                    }
                }
            }
        }
    }

    for (size_t idx = 0; idx < fn.instructions.size(); ++idx) {
        const auto& inst = fn.instructions[idx];
        int weight = 1;
        if (loopDepths[idx] > 0) {
            weight = 1 + loopDepths[idx] * 10;
        }

        if (inst.opcode == IROpcode::CALL) {
            hasCall = true;
        }
        for (const auto& operand : inst.operands) {
            if (operand.kind == OperandKind::VirtualReg) {
                hasReg = true;
                scanOperand(operand, maxReg);
                vregHeat[regId(operand)] += weight;
            }
        }

        if (inst.opcode == IROpcode::STORE_LOCAL && !inst.operands.empty() &&
            inst.operands[0].kind == OperandKind::Immediate) {
            maxLocalEnd = std::max(maxLocalEnd, immValue(inst.operands[0]) + 4);
            localHeat[immValue(inst.operands[0])] += weight;
        } else if (inst.opcode == IROpcode::LOAD_LOCAL && inst.operands.size() >= 2 &&
                   inst.operands[1].kind == OperandKind::Immediate) {
            maxLocalEnd = std::max(maxLocalEnd, immValue(inst.operands[1]) + 4);
            localHeat[immValue(inst.operands[1])] += weight;
        }
    }

    FunctionLayout layout;
    layout.hasCall = hasCall;
    layout.saveRa = hasCall;
    layout.localSize = align16(maxLocalEnd);
    layout.vregBase = layout.localSize;
    layout.vregCount = hasReg ? static_cast<int>(maxReg + 1) : 0;
    int vregSize = layout.vregCount * 4;

    std::vector<std::string> savedAvailableRegs = {
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11"};
    std::vector<std::string> localAvailableRegs;
    if (!layout.hasCall) {
        // 叶函数：仅用调用者保存寄存器，避免 s-reg save/restore 开销
        // a0 保留给返回值，a1-a7/t4-t6 用作计算寄存器
        // t0-t3 由 emit 函数自动使用（loadOperand 等）
        localAvailableRegs.push_back("t4");
        localAvailableRegs.push_back("t5");
        localAvailableRegs.push_back("t6");
        localAvailableRegs.push_back("a1");
        localAvailableRegs.push_back("a2");
        localAvailableRegs.push_back("a3");
        localAvailableRegs.push_back("a4");
        localAvailableRegs.push_back("a5");
        localAvailableRegs.push_back("a6");
        localAvailableRegs.push_back("a7");
    } else {
        localAvailableRegs = savedAvailableRegs;
    }
    std::vector<std::string> vregAvailableRegs = layout.hasCall ? savedAvailableRegs : localAvailableRegs;
    size_t nextLocal = 0;
    std::unordered_set<std::string> usedSaved;
    std::unordered_set<std::string> allocatedRegs;

    for (const auto& [offset, count] : sortedHotEntries(localHeat)) {
        if (nextLocal >= localAvailableRegs.size()) break;
        if (count < 2) continue;
        const std::string& reg = localAvailableRegs[nextLocal++];
        layout.localRegs[offset] = reg;
        allocatedRegs.insert(reg);
        if (!reg.empty() && reg[0] == 's') usedSaved.insert(reg);
    }

    for (const auto& [id, count] : sortedHotEntries(vregHeat)) {
        if (count < 3) continue;
        auto regIt = std::find_if(vregAvailableRegs.begin(), vregAvailableRegs.end(),
                                  [&](const std::string& reg) {
                                      return !allocatedRegs.count(reg);
                                  });
        if (regIt == vregAvailableRegs.end()) break;
        layout.vregRegs[id] = *regIt;
        allocatedRegs.insert(*regIt);
        if (!regIt->empty() && (*regIt)[0] == 's') usedSaved.insert(*regIt);
    }

    for (const auto& reg : savedAvailableRegs) {
        if (usedSaved.count(reg)) layout.savedRegs.push_back(reg);
    }

    int savedSize = static_cast<int>(layout.savedRegs.size()) * 4;
    int raSize = layout.saveRa ? 4 : 0;
    int requiredSize = layout.localSize + vregSize + savedSize + raSize;
    layout.frameSize = requiredSize == 0 ? 0 : align16(requiredSize);
    if (requiredSize > 0 && layout.frameSize < 16) layout.frameSize = 16;
    layout.raOffset = layout.saveRa ? layout.frameSize - 4 : -1;
    layout.saveBase = layout.frameSize - raSize - savedSize;
    return layout;
}

int localSlot(const FunctionLayout& layout, int irOffset) {
    (void)layout;
    return irOffset;
}

int vregSlot(const FunctionLayout& layout, uint32_t id) {
    return layout.vregBase + static_cast<int>(id) * 4;
}

const std::string* localReg(const FunctionLayout& layout, int offset) {
    auto found = layout.localRegs.find(offset);
    return found == layout.localRegs.end() ? nullptr : &found->second;
}

const std::string* vregReg(const FunctionLayout& layout, uint32_t id) {
    auto found = layout.vregRegs.find(id);
    return found == layout.vregRegs.end() ? nullptr : &found->second;
}

std::string resultReg(const FunctionLayout& layout, const IROperand& operand, const char* fallback) {
    if (operand.kind == OperandKind::VirtualReg) {
        if (const std::string* cached = vregReg(layout, regId(operand))) {
            return *cached;
        }
    }
    return fallback;
}

void forgetAliasesUsing(EmitState& state, const std::string& physReg) {
    for (auto it = state.vregAliases.begin(); it != state.vregAliases.end();) {
        if (it->second == physReg) {
            state.localAliases.erase(it->first);
            it = state.vregAliases.erase(it);
        } else {
            ++it;
        }
    }
}

bool hasRemainingUse(const EmitState& state, uint32_t id) {
    auto found = state.remainingUses.find(id);
    return found != state.remainingUses.end() && found->second > 0;
}

bool hasRemainingUse(const EmitState& state, const IROperand& operand) {
    return operand.kind == OperandKind::VirtualReg && hasRemainingUse(state, regId(operand));
}

void emitStoreSP(std::ostringstream& out, const char* rs, int offset);

void spillAliasesUsing(std::ostringstream& out, const FunctionLayout& layout,
                       EmitState& state, const std::string& physReg) {
    std::vector<uint32_t> toErase;
    for (const auto& [id, reg] : state.vregAliases) {
        if (reg != physReg) continue;
        if (hasRemainingUse(state, id)) {
            emitStoreSP(out, physReg.c_str(), vregSlot(layout, id));
        }
        toErase.push_back(id);
    }
    for (uint32_t id : toErase) {
        state.vregAliases.erase(id);
        state.localAliases.erase(id);
    }
}

void forgetVReg(EmitState& state, const IROperand& operand) {
    if (operand.kind == OperandKind::VirtualReg) {
        uint32_t id = regId(operand);
        state.vregAliases.erase(id);
        state.localAliases.erase(id);
    }
}

void rememberAlias(EmitState& state, const IROperand& operand, const std::string& physReg) {
    if (operand.kind == OperandKind::VirtualReg) {
        state.vregAliases[regId(operand)] = physReg;
    }
}

void rememberLocalAlias(EmitState& state, const IROperand& operand, int offset) {
    if (operand.kind == OperandKind::VirtualReg) {
        state.localAliases[regId(operand)] = offset;
    }
}

const std::string* aliasReg(const EmitState& state, const IROperand& operand) {
    if (operand.kind != OperandKind::VirtualReg) return nullptr;
    auto found = state.vregAliases.find(regId(operand));
    return found == state.vregAliases.end() ? nullptr : &found->second;
}

std::optional<int> localAliasOffset(const EmitState& state, const IROperand& operand) {
    if (operand.kind != OperandKind::VirtualReg) return std::nullopt;
    auto found = state.localAliases.find(regId(operand));
    if (found == state.localAliases.end()) return std::nullopt;
    return found->second;
}

void consumeUse(EmitState& state, const IROperand& operand) {
    if (operand.kind != OperandKind::VirtualReg) return;
    auto found = state.remainingUses.find(regId(operand));
    if (found != state.remainingUses.end() && found->second > 0) {
        --found->second;
    }
}

bool isVolatilePhysReg(const std::string& reg) {
    return !reg.empty() && (reg[0] == 'a' || reg[0] == 't');
}

bool usesVReg(const IROperand& operand, uint32_t id) {
    return operand.kind == OperandKind::VirtualReg && regId(operand) == id;
}

bool isImmediateSafeConsumer(const IRInstruction& inst, uint32_t id) {
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
            return inst.operands.size() >= 3 &&
                   (usesVReg(inst.operands[1], id) || usesVReg(inst.operands[2], id));
        case IROpcode::NEG:
        case IROpcode::NOT:
            return inst.operands.size() >= 2 && usesVReg(inst.operands[1], id);
        case IROpcode::STORE_LOCAL:
        case IROpcode::STORE_GLOBAL:
            return inst.operands.size() >= 2 && usesVReg(inst.operands[1], id);
        case IROpcode::BEQ:
        case IROpcode::BNE:
            return inst.operands.size() >= 2 &&
                   (usesVReg(inst.operands[0], id) || usesVReg(inst.operands[1], id));
        case IROpcode::RET:
            return !inst.operands.empty() && usesVReg(inst.operands[0], id);
        default:
            return false;
    }
}

bool instructionUsesVReg(const IRInstruction& inst, uint32_t id) {
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
            return inst.operands.size() >= 3 &&
                   (usesVReg(inst.operands[1], id) || usesVReg(inst.operands[2], id));
        case IROpcode::NEG:
        case IROpcode::NOT:
            return inst.operands.size() >= 2 && usesVReg(inst.operands[1], id);
        case IROpcode::STORE_LOCAL:
        case IROpcode::STORE_GLOBAL:
            return inst.operands.size() >= 2 && usesVReg(inst.operands[1], id);
        case IROpcode::BEQ:
        case IROpcode::BNE:
            return inst.operands.size() >= 2 &&
                   (usesVReg(inst.operands[0], id) || usesVReg(inst.operands[1], id));
        case IROpcode::PARAM:
        case IROpcode::RET:
            return !inst.operands.empty() && usesVReg(inst.operands[0], id);
        default:
            return false;
    }
}

std::optional<uint32_t> definedVReg(const IRInstruction& inst) {
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
        case IROpcode::LOAD_LOCAL:
        case IROpcode::LOAD_GLOBAL:
        case IROpcode::CALL:
            if (!inst.operands.empty() && inst.operands[0].kind == OperandKind::VirtualReg) {
                return regId(inst.operands[0]);
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

bool isSkippableDeadValue(const IRInstruction& inst) {
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
        case IROpcode::LOAD_LOCAL:
        case IROpcode::LOAD_GLOBAL:
            return true;
        default:
            return false;
    }
}

void consumeInstructionUses(EmitState& state, const IRInstruction& inst) {
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
                consumeUse(state, inst.operands[1]);
                consumeUse(state, inst.operands[2]);
            }
            break;
        case IROpcode::NEG:
        case IROpcode::NOT:
            if (inst.operands.size() >= 2) consumeUse(state, inst.operands[1]);
            break;
        default:
            break;
    }
}

void flushLiveVolatileAliases(std::ostringstream& out, const FunctionLayout& layout, EmitState& state) {
    std::vector<uint32_t> toErase;
    for (const auto& [id, reg] : state.vregAliases) {
        if (!isVolatilePhysReg(reg)) continue;
        auto found = state.remainingUses.find(id);
        if (found != state.remainingUses.end() && found->second > 0) {
            emitStoreSP(out, reg.c_str(), vregSlot(layout, id));
        }
        toErase.push_back(id);
    }
    for (uint32_t id : toErase) {
        state.vregAliases.erase(id);
        state.localAliases.erase(id);
    }
}

void emitLoadSP(std::ostringstream& out, const char* rd, int offset) {
    if (offset >= -2048 && offset <= 2047) {
        out << "    lw " << rd << ", " << offset << "(sp)\n";
    } else {
        out << "    li t3, " << offset << "\n";
        out << "    add t3, sp, t3\n";
        out << "    lw " << rd << ", 0(t3)\n";
    }
}

void emitStoreSP(std::ostringstream& out, const char* rs, int offset) {
    if (offset >= -2048 && offset <= 2047) {
        out << "    sw " << rs << ", " << offset << "(sp)\n";
    } else {
        out << "    li t3, " << offset << "\n";
        out << "    add t3, sp, t3\n";
        out << "    sw " << rs << ", 0(t3)\n";
    }
}

void emitAdjustSP(std::ostringstream& out, int amount, bool isSub) {
    if (amount <= 2047) {
        out << "    addi sp, sp, " << (isSub ? -amount : amount) << "\n";
    } else {
        out << "    li t2, " << amount << "\n";
        out << "    " << (isSub ? "sub" : "add") << " sp, sp, t2\n";
    }
}

void loadOperand(std::ostringstream& out, const FunctionLayout& layout, EmitState& state,
                 const IROperand& operand, const char* physReg) {
    if (operand.kind == OperandKind::Immediate) {
        spillAliasesUsing(out, layout, state, physReg);
        out << "    li " << physReg << ", " << immValue(operand) << "\n";
    } else if (operand.kind == OperandKind::VirtualReg) {
        if (const std::string* aliased = aliasReg(state, operand)) {
            if (*aliased != physReg) {
                spillAliasesUsing(out, layout, state, physReg);
                out << "    mv " << physReg << ", " << *aliased << "\n";
            }
            return;
        }
        if (const std::string* cached = vregReg(layout, regId(operand))) {
            if (*cached != physReg) {
                spillAliasesUsing(out, layout, state, physReg);
                out << "    mv " << physReg << ", " << *cached << "\n";
            }
            return;
        }
        spillAliasesUsing(out, layout, state, physReg);
        emitLoadSP(out, physReg, vregSlot(layout, regId(operand)));
    }
}

std::string operandReg(std::ostringstream& out, const FunctionLayout& layout, EmitState& state,
                       const IROperand& operand, const char* scratchReg) {
    if (operand.kind == OperandKind::VirtualReg) {
        if (const std::string* aliased = aliasReg(state, operand)) {
            return *aliased;
        }
        if (const std::string* cached = vregReg(layout, regId(operand))) {
            return *cached;
        }
    }
    loadOperand(out, layout, state, operand, scratchReg);
    return scratchReg;
}

void storeVReg(std::ostringstream& out, const FunctionLayout& layout,
               EmitState& state, const IROperand& operand, const char* physReg,
               bool aliasOnlyIfUncached = false) {
    if (operand.kind == OperandKind::VirtualReg) {
        forgetVReg(state, operand);
        if (const std::string* cached = vregReg(layout, regId(operand))) {
            if (*cached != physReg) {
                out << "    mv " << *cached << ", " << physReg << "\n";
            }
            rememberAlias(state, operand, *cached);
            return;
        }
        if (!aliasOnlyIfUncached) {
            emitStoreSP(out, physReg, vregSlot(layout, regId(operand)));
        }
        rememberAlias(state, operand, physReg);
    }
}

void aliasVReg(std::ostringstream& out, const FunctionLayout& layout, EmitState& state,
               const IROperand& operand, const std::string& physReg) {
    forgetVReg(state, operand);
    if (operand.kind != OperandKind::VirtualReg) return;
    if (const std::string* cached = vregReg(layout, regId(operand))) {
        if (*cached != physReg) {
            out << "    mv " << *cached << ", " << physReg << "\n";
        }
        rememberAlias(state, operand, *cached);
    } else {
        rememberAlias(state, operand, physReg);
    }
}

void clearVolatileAliases(EmitState& state) {
    for (auto it = state.vregAliases.begin(); it != state.vregAliases.end();) {
        const std::string& reg = it->second;
        if (!reg.empty() && (reg[0] == 'a' || reg[0] == 't')) {
            state.localAliases.erase(it->first);
            it = state.vregAliases.erase(it);
        } else {
            ++it;
        }
    }
}

void clearAliases(EmitState& state) {
    state.vregAliases.clear();
    state.localAliases.clear();
}

bool sameOperand(const IROperand& lhs, const IROperand& rhs) {
    return lhs.kind == rhs.kind && lhs.value == rhs.value;
}

std::optional<int> nextStoreLocalOffset(const IRInstruction* nextInst, const IROperand& source) {
    if (!nextInst || nextInst->opcode != IROpcode::STORE_LOCAL || nextInst->operands.size() != 2 ||
        nextInst->operands[0].kind != OperandKind::Immediate ||
        !sameOperand(nextInst->operands[1], source)) {
        return std::nullopt;
    }
    return immValue(nextInst->operands[0]);
}

std::vector<IRInstruction> removeFallthroughJumps(const std::vector<IRInstruction>& insts) {
    std::vector<IRInstruction> result;
    result.reserve(insts.size());

    for (size_t i = 0; i < insts.size(); ++i) {
        if (insts[i].opcode == IROpcode::JMP && insts[i].operands.size() == 1 &&
            i + 1 < insts.size() && insts[i + 1].opcode == IROpcode::LABEL &&
            insts[i + 1].operands.size() == 1 &&
            labelId(insts[i].operands[0]) == labelId(insts[i + 1].operands[0])) {
            continue;
        }
        result.push_back(insts[i]);
    }

    return result;
}

}  // namespace

std::string CodeGenerator::generate(const IRProgram& program) {
    std::ostringstream out;

    if (!program.globals.empty() || !program.globalNames.empty()) {
        out << ".data\n";
        if (!program.globals.empty()) {
            for (const auto& global : program.globals) {
                out << global.name << ":\n";
                out << "    .word " << global.initialValue << "\n";
            }
        } else {
            for (const auto& name : program.globalNames) {
                out << name << ":\n";
                out << "    .word 0\n";
            }
        }
    }

    out << ".text\n";
    out << ".globl main\n";

    for (const auto& fn : program.functions) {
        IRFunction codeFn = fn;
        codeFn.instructions = removeFallthroughJumps(fn.instructions);
        FunctionLayout layout = buildLayout(codeFn);
        const std::string returnLabel = ".L" + fn.name + "_return";
        std::vector<IROperand> pendingParams;
        EmitState state;
        state.remainingUses = buildUseCounts(codeFn);

        out << fn.name << ":\n";
        if (layout.frameSize > 0) {
            emitAdjustSP(out, layout.frameSize, true);
        }
        for (size_t i = 0; i < layout.savedRegs.size(); ++i) {
            emitStoreSP(out, layout.savedRegs[i].c_str(), layout.saveBase + static_cast<int>(i) * 4);
        }
        if (layout.saveRa) {
            emitStoreSP(out, "ra", layout.raOffset);
        }
        for (int i = 0; i < fn.paramCount && i < layout.vregCount && i < 8; ++i) {
            std::string reg = "a" + std::to_string(i);
            aliasVReg(out, layout, state, IROperand::reg(static_cast<uint32_t>(i)), reg);
        }
        // 超过 8 个的参数从调用者栈帧中加载
        for (int i = 8; i < fn.paramCount && i < layout.vregCount; ++i) {
            int stackOffset = layout.frameSize + (i - 8) * 4;
            forgetAliasesUsing(state, "t0");
            emitLoadSP(out, "t0", stackOffset);
            storeVReg(out, layout, state, IROperand::reg(static_cast<uint32_t>(i)), "t0");
            if (!vregReg(layout, static_cast<uint32_t>(i))) {
                state.vregAliases.erase(static_cast<uint32_t>(i));
            }
        }

        for (size_t instIndex = 0; instIndex < codeFn.instructions.size(); ++instIndex) {
            const auto& inst = codeFn.instructions[instIndex];
            const IRInstruction* nextInst =
                (instIndex + 1 < codeFn.instructions.size()) ? &codeFn.instructions[instIndex + 1] : nullptr;
            auto canUseAliasOnly = [&](const IROperand& dest) {
                if (dest.kind != OperandKind::VirtualReg || !nextInst) return false;
                uint32_t id = regId(dest);
                if (!isImmediateSafeConsumer(*nextInst, id)) return false;
                for (size_t later = instIndex + 2; later < codeFn.instructions.size(); ++later) {
                    if (instructionUsesVReg(codeFn.instructions[later], id)) {
                        return false;
                    }
                }
                return true;
            };

            if (auto dest = definedVReg(inst);
                dest && isSkippableDeadValue(inst) && !hasRemainingUse(state, *dest)) {
                consumeInstructionUses(state, inst);
                forgetVReg(state, IROperand::reg(*dest));
                continue;
            }

            switch (inst.opcode) {
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
                    if (inst.operands.size() != 3) break;
                    {
                    if (auto storeOffset = nextStoreLocalOffset(nextInst, inst.operands[0])) {
                        if (const std::string* target = localReg(layout, localSlot(layout, *storeOffset))) {
                            auto lhsLocal = localAliasOffset(state, inst.operands[1]);
                            auto rhsLocal = localAliasOffset(state, inst.operands[2]);
                            bool updatedInPlace = false;

                            if (inst.opcode == IROpcode::ADD && lhsLocal && *lhsLocal == *storeOffset &&
                                inst.operands[2].kind == OperandKind::Immediate &&
                                fits12(immValue(inst.operands[2]))) {
                                consumeUse(state, inst.operands[1]);
                                consumeUse(state, inst.operands[2]);
                                spillAliasesUsing(out, layout, state, *target);
                                out << "    addi " << *target << ", " << *target << ", "
                                    << immValue(inst.operands[2]) << "\n";
                                updatedInPlace = true;
                            } else if (inst.opcode == IROpcode::ADD && rhsLocal && *rhsLocal == *storeOffset &&
                                       inst.operands[1].kind == OperandKind::Immediate &&
                                       fits12(immValue(inst.operands[1]))) {
                                consumeUse(state, inst.operands[1]);
                                consumeUse(state, inst.operands[2]);
                                spillAliasesUsing(out, layout, state, *target);
                                out << "    addi " << *target << ", " << *target << ", "
                                    << immValue(inst.operands[1]) << "\n";
                                updatedInPlace = true;
                            } else if (inst.opcode == IROpcode::SUB && lhsLocal && *lhsLocal == *storeOffset &&
                                       inst.operands[2].kind == OperandKind::Immediate &&
                                       fits12(-immValue(inst.operands[2]))) {
                                consumeUse(state, inst.operands[1]);
                                consumeUse(state, inst.operands[2]);
                                spillAliasesUsing(out, layout, state, *target);
                                out << "    addi " << *target << ", " << *target << ", "
                                    << -immValue(inst.operands[2]) << "\n";
                                updatedInPlace = true;
                            } else if (inst.opcode == IROpcode::ADD && lhsLocal && *lhsLocal == *storeOffset) {
                                const std::string rhs = operandReg(out, layout, state, inst.operands[2], "t0");
                                consumeUse(state, inst.operands[1]);
                                consumeUse(state, inst.operands[2]);
                                spillAliasesUsing(out, layout, state, *target);
                                out << "    add " << *target << ", " << *target << ", " << rhs << "\n";
                                updatedInPlace = true;
                            } else if (inst.opcode == IROpcode::ADD && rhsLocal && *rhsLocal == *storeOffset) {
                                const std::string lhs = operandReg(out, layout, state, inst.operands[1], "t0");
                                consumeUse(state, inst.operands[1]);
                                consumeUse(state, inst.operands[2]);
                                spillAliasesUsing(out, layout, state, *target);
                                out << "    add " << *target << ", " << *target << ", " << lhs << "\n";
                                updatedInPlace = true;
                            } else if (inst.opcode == IROpcode::SUB && lhsLocal && *lhsLocal == *storeOffset) {
                                const std::string rhs = operandReg(out, layout, state, inst.operands[2], "t0");
                                consumeUse(state, inst.operands[1]);
                                consumeUse(state, inst.operands[2]);
                                spillAliasesUsing(out, layout, state, *target);
                                out << "    sub " << *target << ", " << *target << ", " << rhs << "\n";
                                updatedInPlace = true;
                            }

                            if (updatedInPlace) {
                                rememberAlias(state, inst.operands[0], *target);
                                rememberLocalAlias(state, inst.operands[0], *storeOffset);
                                break;
                            }
                        }
                    }

                    const std::string rd = resultReg(layout, inst.operands[0], "t2");
                    if (inst.opcode == IROpcode::ADD &&
                        inst.operands[1].kind != OperandKind::Immediate &&
                        inst.operands[2].kind == OperandKind::Immediate &&
                        fits12(immValue(inst.operands[2]))) {
                        const std::string lhs = operandReg(out, layout, state, inst.operands[1], "t0");
                        consumeUse(state, inst.operands[1]);
                        consumeUse(state, inst.operands[2]);
                        spillAliasesUsing(out, layout, state, rd);
                        out << "    addi " << rd << ", " << lhs << ", " << immValue(inst.operands[2]) << "\n";
                        storeVReg(out, layout, state, inst.operands[0], rd.c_str(),
                                  canUseAliasOnly(inst.operands[0]));
                        break;
                    }
                    if (inst.opcode == IROpcode::ADD &&
                        inst.operands[1].kind == OperandKind::Immediate &&
                        inst.operands[2].kind != OperandKind::Immediate &&
                        fits12(immValue(inst.operands[1]))) {
                        const std::string rhs = operandReg(out, layout, state, inst.operands[2], "t0");
                        consumeUse(state, inst.operands[1]);
                        consumeUse(state, inst.operands[2]);
                        spillAliasesUsing(out, layout, state, rd);
                        out << "    addi " << rd << ", " << rhs << ", " << immValue(inst.operands[1]) << "\n";
                        storeVReg(out, layout, state, inst.operands[0], rd.c_str(),
                                  canUseAliasOnly(inst.operands[0]));
                        break;
                    }
                    if (inst.opcode == IROpcode::SUB &&
                        inst.operands[1].kind != OperandKind::Immediate &&
                        inst.operands[2].kind == OperandKind::Immediate &&
                        fits12(-immValue(inst.operands[2]))) {
                        const std::string lhs = operandReg(out, layout, state, inst.operands[1], "t0");
                        consumeUse(state, inst.operands[1]);
                        consumeUse(state, inst.operands[2]);
                        spillAliasesUsing(out, layout, state, rd);
                        out << "    addi " << rd << ", " << lhs << ", " << -immValue(inst.operands[2]) << "\n";
                        storeVReg(out, layout, state, inst.operands[0], rd.c_str(),
                                  canUseAliasOnly(inst.operands[0]));
                        break;
                    }
                    if (inst.opcode == IROpcode::LT &&
                        inst.operands[1].kind != OperandKind::Immediate &&
                        inst.operands[2].kind == OperandKind::Immediate &&
                        fits12(immValue(inst.operands[2]))) {
                        const std::string lhs = operandReg(out, layout, state, inst.operands[1], "t0");
                        consumeUse(state, inst.operands[1]);
                        consumeUse(state, inst.operands[2]);
                        spillAliasesUsing(out, layout, state, rd);
                        out << "    slti " << rd << ", " << lhs << ", " << immValue(inst.operands[2]) << "\n";
                        storeVReg(out, layout, state, inst.operands[0], rd.c_str(),
                                  canUseAliasOnly(inst.operands[0]));
                        break;
                    }
                    // 强度削减: MUL × 2的幂 → slli
                    if (inst.opcode == IROpcode::MUL &&
                        inst.operands[1].kind != OperandKind::Immediate &&
                        inst.operands[2].kind == OperandKind::Immediate &&
                        isPowerOfTwo(immValue(inst.operands[2]))) {
                        const std::string lhs = operandReg(out, layout, state, inst.operands[1], "t0");
                        consumeUse(state, inst.operands[1]);
                        consumeUse(state, inst.operands[2]);
                        spillAliasesUsing(out, layout, state, rd);
                        out << "    slli " << rd << ", " << lhs << ", " << ilog2(immValue(inst.operands[2])) << "\n";
                        storeVReg(out, layout, state, inst.operands[0], rd.c_str(),
                                  canUseAliasOnly(inst.operands[0]));
                        break;
                    }
                    if (inst.opcode == IROpcode::MUL &&
                        inst.operands[1].kind == OperandKind::Immediate &&
                        inst.operands[2].kind != OperandKind::Immediate &&
                        isPowerOfTwo(immValue(inst.operands[1]))) {
                        const std::string rhs = operandReg(out, layout, state, inst.operands[2], "t0");
                        consumeUse(state, inst.operands[1]);
                        consumeUse(state, inst.operands[2]);
                        spillAliasesUsing(out, layout, state, rd);
                        out << "    slli " << rd << ", " << rhs << ", " << ilog2(immValue(inst.operands[1])) << "\n";
                        storeVReg(out, layout, state, inst.operands[0], rd.c_str(),
                                  canUseAliasOnly(inst.operands[0]));
                        break;
                    }
                    // 强度削减: MUL × (2^n+1) → shift+add (如 x*3, x*5, x*9)
                    if (inst.opcode == IROpcode::MUL &&
                        inst.operands[1].kind != OperandKind::Immediate &&
                        inst.operands[2].kind == OperandKind::Immediate) {
                        int32_t muli = immValue(inst.operands[2]);
                        if (muli > 1 && !isPowerOfTwo(muli) &&
                            (muli - 1) > 0 && ((muli - 1) & (muli - 2)) == 0) {
                            int n = ilog2(muli - 1);
                            const std::string lhs = operandReg(out, layout, state, inst.operands[1], "t0");
                            consumeUse(state, inst.operands[1]);
                            consumeUse(state, inst.operands[2]);
                            spillAliasesUsing(out, layout, state, rd);
                            spillAliasesUsing(out, layout, state, "t0");
                            out << "    slli t0, " << lhs << ", " << n << "\n";
                            out << "    add " << rd << ", t0, " << lhs << "\n";
                            storeVReg(out, layout, state, inst.operands[0], rd.c_str(),
                                      canUseAliasOnly(inst.operands[0]));
                            break;
                         }
                    }
                    // 强度削减: DIV ÷ 2的幂 → srai（含符号修正）
                    if (inst.opcode == IROpcode::DIV &&
                        inst.operands[1].kind != OperandKind::Immediate &&
                        inst.operands[2].kind == OperandKind::Immediate &&
                        isPowerOfTwo(immValue(inst.operands[2]))) {
                        const std::string lhs = operandReg(out, layout, state, inst.operands[1], "t0");
                        int n = ilog2(immValue(inst.operands[2]));
                        consumeUse(state, inst.operands[1]);
                        consumeUse(state, inst.operands[2]);
                        spillAliasesUsing(out, layout, state, rd);
                        spillAliasesUsing(out, layout, state, "t0");
                        // 有符号除法: 截断向零，用 srai + 符号修正
                        out << "    srai t0, " << lhs << ", 31\n";
                        out << "    srli t0, t0, " << (32 - n) << "\n";
                        out << "    add t0, " << lhs << ", t0\n";
                        out << "    srai " << rd << ", t0, " << n << "\n";
                        storeVReg(out, layout, state, inst.operands[0], rd.c_str(),
                                  canUseAliasOnly(inst.operands[0]));
                        break;
                    }
                    const std::string lhs = operandReg(out, layout, state, inst.operands[1], "t0");
                    const std::string rhs = operandReg(out, layout, state, inst.operands[2], "t1");
                    consumeUse(state, inst.operands[1]);
                    consumeUse(state, inst.operands[2]);
                    spillAliasesUsing(out, layout, state, rd);
                    switch (inst.opcode) {
                        case IROpcode::ADD: out << "    add " << rd << ", " << lhs << ", " << rhs << "\n"; break;
                        case IROpcode::SUB: out << "    sub " << rd << ", " << lhs << ", " << rhs << "\n"; break;
                        case IROpcode::MUL: out << "    mul " << rd << ", " << lhs << ", " << rhs << "\n"; break;
                        case IROpcode::DIV: out << "    div " << rd << ", " << lhs << ", " << rhs << "\n"; break;
                        case IROpcode::MOD: out << "    rem " << rd << ", " << lhs << ", " << rhs << "\n"; break;
                        case IROpcode::LT:  out << "    slt " << rd << ", " << lhs << ", " << rhs << "\n"; break;
                        case IROpcode::GT:  out << "    slt " << rd << ", " << rhs << ", " << lhs << "\n"; break;
                        case IROpcode::LE:
                            out << "    slt " << rd << ", " << rhs << ", " << lhs << "\n";
                            out << "    xori " << rd << ", " << rd << ", 1\n";
                            break;
                        case IROpcode::GE:
                            out << "    slt " << rd << ", " << lhs << ", " << rhs << "\n";
                            out << "    xori " << rd << ", " << rd << ", 1\n";
                            break;
                        case IROpcode::EQ:
                            out << "    sub " << rd << ", " << lhs << ", " << rhs << "\n";
                            out << "    seqz " << rd << ", " << rd << "\n";
                            break;
                        case IROpcode::NE:
                            out << "    sub " << rd << ", " << lhs << ", " << rhs << "\n";
                            out << "    snez " << rd << ", " << rd << "\n";
                            break;
                        case IROpcode::AND:
                            out << "    snez t0, " << lhs << "\n";
                            out << "    snez t1, " << rhs << "\n";
                            out << "    and " << rd << ", t0, t1\n";
                            break;
                        case IROpcode::OR:
                            out << "    or " << rd << ", " << lhs << ", " << rhs << "\n";
                            out << "    snez " << rd << ", " << rd << "\n";
                            break;
                        default: break;
                    }
                    storeVReg(out, layout, state, inst.operands[0], rd.c_str(),
                              canUseAliasOnly(inst.operands[0]));
                    }
                    break;
                case IROpcode::NEG:
                    if (inst.operands.size() != 2) break;
                    {
                    const std::string rd = resultReg(layout, inst.operands[0], "t1");
                    const std::string src = operandReg(out, layout, state, inst.operands[1], "t0");
                    consumeUse(state, inst.operands[1]);
                    spillAliasesUsing(out, layout, state, rd);
                    out << "    neg " << rd << ", " << src << "\n";
                    storeVReg(out, layout, state, inst.operands[0], rd.c_str(),
                              canUseAliasOnly(inst.operands[0]));
                    }
                    break;
                case IROpcode::NOT:
                    if (inst.operands.size() != 2) break;
                    {
                    const std::string rd = resultReg(layout, inst.operands[0], "t1");
                    const std::string src = operandReg(out, layout, state, inst.operands[1], "t0");
                    consumeUse(state, inst.operands[1]);
                    spillAliasesUsing(out, layout, state, rd);
                    out << "    seqz " << rd << ", " << src << "\n";
                    storeVReg(out, layout, state, inst.operands[0], rd.c_str(),
                              canUseAliasOnly(inst.operands[0]));
                    }
                    break;
                case IROpcode::LOAD_LOCAL:
                    if (inst.operands.size() != 2) break;
                    if (const std::string* cached = localReg(layout, localSlot(layout, immValue(inst.operands[1])))) {
                        aliasVReg(out, layout, state, inst.operands[0], *cached);
                        rememberLocalAlias(state, inst.operands[0], localSlot(layout, immValue(inst.operands[1])));
                        break;
                    }
                    spillAliasesUsing(out, layout, state, "t0");
                    emitLoadSP(out, "t0", localSlot(layout, immValue(inst.operands[1])));
                    storeVReg(out, layout, state, inst.operands[0], "t0",
                              canUseAliasOnly(inst.operands[0]));
                    break;
                case IROpcode::STORE_LOCAL:
                    if (inst.operands.size() != 2) break;
                    if (const std::string* cached = localReg(layout, localSlot(layout, immValue(inst.operands[0])))) {
                        loadOperand(out, layout, state, inst.operands[1], cached->c_str());
                        consumeUse(state, inst.operands[1]);
                        if (!hasRemainingUse(state, inst.operands[1])) {
                            forgetVReg(state, inst.operands[1]);
                        }
                        break;
                    }
                    loadOperand(out, layout, state, inst.operands[1], "t0");
                    consumeUse(state, inst.operands[1]);
                    emitStoreSP(out, "t0", localSlot(layout, immValue(inst.operands[0])));
                    if (!hasRemainingUse(state, inst.operands[1])) {
                        forgetVReg(state, inst.operands[1]);
                    }
                    break;
                case IROpcode::LOAD_GLOBAL:
                    if (inst.operands.size() != 2) break;
                    spillAliasesUsing(out, layout, state, "t0");
                    spillAliasesUsing(out, layout, state, "t1");
                    out << "    la t0, " << stringValue(inst.operands[1]) << "\n";
                    out << "    lw t1, 0(t0)\n";
                    storeVReg(out, layout, state, inst.operands[0], "t1",
                              canUseAliasOnly(inst.operands[0]));
                    break;
                case IROpcode::STORE_GLOBAL:
                    if (inst.operands.size() != 2) break;
                    loadOperand(out, layout, state, inst.operands[1], "t0");
                    consumeUse(state, inst.operands[1]);
                    spillAliasesUsing(out, layout, state, "t1");
                    out << "    la t1, " << stringValue(inst.operands[0]) << "\n";
                    out << "    sw t0, 0(t1)\n";
                    if (!hasRemainingUse(state, inst.operands[1])) {
                        forgetVReg(state, inst.operands[1]);
                    }
                    break;
                case IROpcode::LABEL:
                    if (inst.operands.size() != 1) break;
                    flushLiveVolatileAliases(out, layout, state);
                    clearAliases(state);
                    out << labelName(fn.name, inst.operands[0]) << ":\n";
                    break;
                case IROpcode::JMP:
                    if (inst.operands.size() != 1) break;
                    flushLiveVolatileAliases(out, layout, state);
                    out << "    j " << labelName(fn.name, inst.operands[0]) << "\n";
                    break;
                case IROpcode::BEQ:
                case IROpcode::BNE:
                    if (inst.operands.size() != 3) break;
                    if (inst.operands[1].kind == OperandKind::Immediate && immValue(inst.operands[1]) == 0) {
                        const std::string lhs = operandReg(out, layout, state, inst.operands[0], "t0");
                        consumeUse(state, inst.operands[0]);
                        consumeUse(state, inst.operands[1]);
                        flushLiveVolatileAliases(out, layout, state);
                        out << "    " << (inst.opcode == IROpcode::BEQ ? "beq" : "bne")
                            << " " << lhs << ", zero, " << labelName(fn.name, inst.operands[2]) << "\n";
                        break;
                    }
                    if (inst.operands[0].kind == OperandKind::Immediate && immValue(inst.operands[0]) == 0) {
                        const std::string rhs = operandReg(out, layout, state, inst.operands[1], "t1");
                        consumeUse(state, inst.operands[0]);
                        consumeUse(state, inst.operands[1]);
                        flushLiveVolatileAliases(out, layout, state);
                        out << "    " << (inst.opcode == IROpcode::BEQ ? "beq" : "bne")
                            << " zero, " << rhs << ", " << labelName(fn.name, inst.operands[2]) << "\n";
                        break;
                    }
                    {
                    const std::string lhs = operandReg(out, layout, state, inst.operands[0], "t0");
                    const std::string rhs = operandReg(out, layout, state, inst.operands[1], "t1");
                    consumeUse(state, inst.operands[0]);
                    consumeUse(state, inst.operands[1]);
                    flushLiveVolatileAliases(out, layout, state);
                    out << "    " << (inst.opcode == IROpcode::BEQ ? "beq" : "bne")
                        << " " << lhs << ", " << rhs << ", " << labelName(fn.name, inst.operands[2]) << "\n";
                    }
                    break;
                case IROpcode::PARAM:
                    if (inst.operands.size() == 1) {
                        pendingParams.push_back(inst.operands[0]);
                    }
                    break;
                case IROpcode::CALL:
                    if (inst.operands.size() != 2) break;
                    // 先将前 8 个参数加载到 a0-a7（此时 sp 未调整，vreg 偏移量正确）
                    for (size_t i = 0; i < pendingParams.size() && i < 8; ++i) {
                        std::string argReg = "a" + std::to_string(i);
                        loadOperand(out, layout, state, pendingParams[i], argReg.c_str());
                        consumeUse(state, pendingParams[i]);
                    }
                    // 处理超过 8 个的参数：通过栈传递
                    if (pendingParams.size() > 8) {
                        flushLiveVolatileAliases(out, layout, state);
                        int stackArgSize = static_cast<int>(pendingParams.size() - 8) * 4;
                        emitAdjustSP(out, stackArgSize, true);
                        // 此时 sp 已调整，vreg 实际位置在 sp + vregSlot + stackArgSize
                        for (size_t i = 8; i < pendingParams.size(); ++i) {
                            int storeOffset = static_cast<int>(i - 8) * 4;
                            forgetAliasesUsing(state, "t0");
                            if (pendingParams[i].kind == OperandKind::Immediate) {
                                out << "    li t0, " << immValue(pendingParams[i]) << "\n";
                            } else if (pendingParams[i].kind == OperandKind::VirtualReg) {
                                if (const std::string* aliased = aliasReg(state, pendingParams[i])) {
                                    if (*aliased != "t0") {
                                        out << "    mv t0, " << *aliased << "\n";
                                    }
                                } else if (const std::string* cached = vregReg(layout, regId(pendingParams[i]))) {
                                    out << "    mv t0, " << *cached << "\n";
                                } else {
                                    int vregAdjOffset = vregSlot(layout, regId(pendingParams[i])) + stackArgSize;
                                    emitLoadSP(out, "t0", vregAdjOffset);
                                }
                            }
                            emitStoreSP(out, "t0", storeOffset);
                            consumeUse(state, pendingParams[i]);
                        }
                    } else {
                        flushLiveVolatileAliases(out, layout, state);
                    }
                    out << "    call " << stringValue(inst.operands[1]) << "\n";
                    // 恢复栈指针
                    if (pendingParams.size() > 8) {
                        int stackArgSize = static_cast<int>(pendingParams.size() - 8) * 4;
                        emitAdjustSP(out, stackArgSize, false);
                    }
                    clearVolatileAliases(state);
                    storeVReg(out, layout, state, inst.operands[0], "a0",
                              canUseAliasOnly(inst.operands[0]));
                    pendingParams.clear();
                    break;
                case IROpcode::RET:
                    if (inst.operands.empty()) {
                        out << "    li a0, 0\n";
                    } else if (inst.operands[0].kind == OperandKind::Immediate) {
                        out << "    li a0, " << immValue(inst.operands[0]) << "\n";
                    } else if (inst.operands[0].kind == OperandKind::VirtualReg) {
                        loadOperand(out, layout, state, inst.operands[0], "a0");
                        consumeUse(state, inst.operands[0]);
                    }
                    if (nextInst) {
                        out << "    j " << returnLabel << "\n";
                    }
                    break;
                default:
                    break;
            }
        }

        out << returnLabel << ":\n";
        if (layout.saveRa) {
            emitLoadSP(out, "ra", layout.raOffset);
        }
        for (int i = static_cast<int>(layout.savedRegs.size()) - 1; i >= 0; --i) {
            emitLoadSP(out, layout.savedRegs[static_cast<size_t>(i)].c_str(), layout.saveBase + i * 4);
        }
        if (layout.frameSize > 0) {
            emitAdjustSP(out, layout.frameSize, false);
        }
        out << "    ret\n";
    }

    return out.str();
}

}  // namespace toyc
