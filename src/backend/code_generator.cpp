// ToyC RISC-V32 Code Generator 实现

#include "toyc/backend/code_generator.h"

#include <cstdint>
#include <algorithm>
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

std::string labelName(const std::string& functionName, const IROperand& operand) {
    return ".L" + functionName + "_" + std::to_string(std::get<uint32_t>(operand.value));
}

int align16(int value) {
    return (value + 15) / 16 * 16;
}

bool fits12(int32_t value) {
    return value >= -2048 && value <= 2047;
}

struct FunctionLayout {
    int frameSize = 16;
    int localSize = 0;
    int vregBase = 0;
    int vregCount = 0;
    int saveBase = 0;
    int raOffset = 12;
    std::unordered_map<int, std::string> localRegs;
    std::unordered_map<uint32_t, std::string> vregRegs;
    std::vector<std::string> savedRegs;
};

void scanOperand(const IROperand& operand, uint32_t& maxReg) {
    if (operand.kind == OperandKind::VirtualReg) {
        maxReg = std::max(maxReg, regId(operand));
    }
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
    int maxLocalEnd = 0;
    std::unordered_map<int, int> localHeat;
    std::unordered_map<uint32_t, int> vregHeat;

    for (const auto& inst : fn.instructions) {
        for (const auto& operand : inst.operands) {
            if (operand.kind == OperandKind::VirtualReg) {
                hasReg = true;
                scanOperand(operand, maxReg);
                ++vregHeat[regId(operand)];
            }
        }

        if (inst.opcode == IROpcode::STORE_LOCAL && !inst.operands.empty() &&
            inst.operands[0].kind == OperandKind::Immediate) {
            maxLocalEnd = std::max(maxLocalEnd, immValue(inst.operands[0]) + 4);
            ++localHeat[immValue(inst.operands[0])];
        } else if (inst.opcode == IROpcode::LOAD_LOCAL && inst.operands.size() >= 2 &&
                   inst.operands[1].kind == OperandKind::Immediate) {
            maxLocalEnd = std::max(maxLocalEnd, immValue(inst.operands[1]) + 4);
            ++localHeat[immValue(inst.operands[1])];
        }
    }

    FunctionLayout layout;
    layout.localSize = align16(maxLocalEnd);
    layout.vregBase = layout.localSize;
    layout.vregCount = hasReg ? static_cast<int>(maxReg + 1) : 0;
    int vregSize = layout.vregCount * 4;

    std::vector<std::string> availableRegs = {
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11"};
    size_t nextSaved = 0;
    std::unordered_set<std::string> usedSaved;

    for (const auto& [offset, count] : sortedHotEntries(localHeat)) {
        if (nextSaved >= availableRegs.size()) break;
        if (count < 2) continue;
        const std::string& reg = availableRegs[nextSaved++];
        layout.localRegs[offset] = reg;
        usedSaved.insert(reg);
    }

    for (const auto& [id, count] : sortedHotEntries(vregHeat)) {
        if (nextSaved >= availableRegs.size()) break;
        if (count < 3) continue;
        const std::string& reg = availableRegs[nextSaved++];
        layout.vregRegs[id] = reg;
        usedSaved.insert(reg);
    }

    for (const auto& reg : availableRegs) {
        if (usedSaved.count(reg)) layout.savedRegs.push_back(reg);
    }

    int requiredSize = layout.localSize + vregSize + static_cast<int>(layout.savedRegs.size()) * 4 + 4;
    layout.frameSize = align16(requiredSize);
    if (layout.frameSize < 16) layout.frameSize = 16;
    layout.raOffset = layout.frameSize - 4;
    layout.saveBase = layout.raOffset - static_cast<int>(layout.savedRegs.size()) * 4;
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

void loadOperand(std::ostringstream& out, const FunctionLayout& layout,
                 const IROperand& operand, const char* physReg) {
    if (operand.kind == OperandKind::Immediate) {
        out << "    li " << physReg << ", " << immValue(operand) << "\n";
    } else if (operand.kind == OperandKind::VirtualReg) {
        if (const std::string* cached = vregReg(layout, regId(operand))) {
            if (*cached != physReg) {
                out << "    mv " << physReg << ", " << *cached << "\n";
            }
            return;
        }
        emitLoadSP(out, physReg, vregSlot(layout, regId(operand)));
    }
}

void storeVReg(std::ostringstream& out, const FunctionLayout& layout,
               const IROperand& operand, const char* physReg) {
    if (operand.kind == OperandKind::VirtualReg) {
        if (const std::string* cached = vregReg(layout, regId(operand))) {
            if (*cached != physReg) {
                out << "    mv " << *cached << ", " << physReg << "\n";
            }
            return;
        }
        emitStoreSP(out, physReg, vregSlot(layout, regId(operand)));
    }
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

        out << fn.name << ":\n";
        emitAdjustSP(out, layout.frameSize, true);
        for (size_t i = 0; i < layout.savedRegs.size(); ++i) {
            emitStoreSP(out, layout.savedRegs[i].c_str(), layout.saveBase + static_cast<int>(i) * 4);
        }
        emitStoreSP(out, "ra", layout.raOffset);
        for (int i = 0; i < layout.vregCount && i < 8; ++i) {
            std::string reg = "a" + std::to_string(i);
            storeVReg(out, layout, IROperand::reg(static_cast<uint32_t>(i)), reg.c_str());
        }
        // 超过 8 个的参数从调用者栈帧中加载
        for (int i = 8; i < fn.paramCount && i < layout.vregCount; ++i) {
            int stackOffset = layout.frameSize + (i - 8) * 4;
            emitLoadSP(out, "t0", stackOffset);
            storeVReg(out, layout, IROperand::reg(static_cast<uint32_t>(i)), "t0");
        }

        for (const auto& inst : codeFn.instructions) {
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
                    if (inst.opcode == IROpcode::ADD &&
                        inst.operands[1].kind != OperandKind::Immediate &&
                        inst.operands[2].kind == OperandKind::Immediate &&
                        fits12(immValue(inst.operands[2]))) {
                        loadOperand(out, layout, inst.operands[1], "t0");
                        out << "    addi t2, t0, " << immValue(inst.operands[2]) << "\n";
                        storeVReg(out, layout, inst.operands[0], "t2");
                        break;
                    }
                    if (inst.opcode == IROpcode::ADD &&
                        inst.operands[1].kind == OperandKind::Immediate &&
                        inst.operands[2].kind != OperandKind::Immediate &&
                        fits12(immValue(inst.operands[1]))) {
                        loadOperand(out, layout, inst.operands[2], "t0");
                        out << "    addi t2, t0, " << immValue(inst.operands[1]) << "\n";
                        storeVReg(out, layout, inst.operands[0], "t2");
                        break;
                    }
                    if (inst.opcode == IROpcode::SUB &&
                        inst.operands[1].kind != OperandKind::Immediate &&
                        inst.operands[2].kind == OperandKind::Immediate &&
                        fits12(-immValue(inst.operands[2]))) {
                        loadOperand(out, layout, inst.operands[1], "t0");
                        out << "    addi t2, t0, " << -immValue(inst.operands[2]) << "\n";
                        storeVReg(out, layout, inst.operands[0], "t2");
                        break;
                    }
                    if (inst.opcode == IROpcode::LT &&
                        inst.operands[1].kind != OperandKind::Immediate &&
                        inst.operands[2].kind == OperandKind::Immediate &&
                        fits12(immValue(inst.operands[2]))) {
                        loadOperand(out, layout, inst.operands[1], "t0");
                        out << "    slti t2, t0, " << immValue(inst.operands[2]) << "\n";
                        storeVReg(out, layout, inst.operands[0], "t2");
                        break;
                    }
                    loadOperand(out, layout, inst.operands[1], "t0");
                    loadOperand(out, layout, inst.operands[2], "t1");
                    switch (inst.opcode) {
                        case IROpcode::ADD: out << "    add t2, t0, t1\n"; break;
                        case IROpcode::SUB: out << "    sub t2, t0, t1\n"; break;
                        case IROpcode::MUL: out << "    mul t2, t0, t1\n"; break;
                        case IROpcode::DIV: out << "    div t2, t0, t1\n"; break;
                        case IROpcode::MOD: out << "    rem t2, t0, t1\n"; break;
                        case IROpcode::LT:  out << "    slt t2, t0, t1\n"; break;
                        case IROpcode::GT:  out << "    slt t2, t1, t0\n"; break;
                        case IROpcode::LE:
                            out << "    slt t2, t1, t0\n";
                            out << "    xori t2, t2, 1\n";
                            break;
                        case IROpcode::GE:
                            out << "    slt t2, t0, t1\n";
                            out << "    xori t2, t2, 1\n";
                            break;
                        case IROpcode::EQ:
                            out << "    sub t2, t0, t1\n";
                            out << "    seqz t2, t2\n";
                            break;
                        case IROpcode::NE:
                            out << "    sub t2, t0, t1\n";
                            out << "    snez t2, t2\n";
                            break;
                        case IROpcode::AND:
                            out << "    snez t0, t0\n";
                            out << "    snez t1, t1\n";
                            out << "    and t2, t0, t1\n";
                            break;
                        case IROpcode::OR:
                            out << "    or t2, t0, t1\n";
                            out << "    snez t2, t2\n";
                            break;
                        default: break;
                    }
                    storeVReg(out, layout, inst.operands[0], "t2");
                    break;
                case IROpcode::NEG:
                    if (inst.operands.size() != 2) break;
                    loadOperand(out, layout, inst.operands[1], "t0");
                    out << "    neg t1, t0\n";
                    storeVReg(out, layout, inst.operands[0], "t1");
                    break;
                case IROpcode::NOT:
                    if (inst.operands.size() != 2) break;
                    loadOperand(out, layout, inst.operands[1], "t0");
                    out << "    seqz t1, t0\n";
                    storeVReg(out, layout, inst.operands[0], "t1");
                    break;
                case IROpcode::LOAD_LOCAL:
                    if (inst.operands.size() != 2) break;
                    if (const std::string* cached = localReg(layout, localSlot(layout, immValue(inst.operands[1])))) {
                        storeVReg(out, layout, inst.operands[0], cached->c_str());
                        break;
                    }
                    emitLoadSP(out, "t0", localSlot(layout, immValue(inst.operands[1])));
                    storeVReg(out, layout, inst.operands[0], "t0");
                    break;
                case IROpcode::STORE_LOCAL:
                    if (inst.operands.size() != 2) break;
                    if (const std::string* cached = localReg(layout, localSlot(layout, immValue(inst.operands[0])))) {
                        loadOperand(out, layout, inst.operands[1], cached->c_str());
                        break;
                    }
                    loadOperand(out, layout, inst.operands[1], "t0");
                    emitStoreSP(out, "t0", localSlot(layout, immValue(inst.operands[0])));
                    break;
                case IROpcode::LOAD_GLOBAL:
                    if (inst.operands.size() != 2) break;
                    out << "    la t0, " << stringValue(inst.operands[1]) << "\n";
                    out << "    lw t1, 0(t0)\n";
                    storeVReg(out, layout, inst.operands[0], "t1");
                    break;
                case IROpcode::STORE_GLOBAL:
                    if (inst.operands.size() != 2) break;
                    loadOperand(out, layout, inst.operands[1], "t0");
                    out << "    la t1, " << stringValue(inst.operands[0]) << "\n";
                    out << "    sw t0, 0(t1)\n";
                    break;
                case IROpcode::LABEL:
                    if (inst.operands.size() != 1) break;
                    out << labelName(fn.name, inst.operands[0]) << ":\n";
                    break;
                case IROpcode::JMP:
                    if (inst.operands.size() != 1) break;
                    out << "    j " << labelName(fn.name, inst.operands[0]) << "\n";
                    break;
                case IROpcode::BEQ:
                case IROpcode::BNE:
                    if (inst.operands.size() != 3) break;
                    if (inst.operands[1].kind == OperandKind::Immediate && immValue(inst.operands[1]) == 0) {
                        loadOperand(out, layout, inst.operands[0], "t0");
                        out << "    " << (inst.opcode == IROpcode::BEQ ? "beq" : "bne")
                            << " t0, zero, " << labelName(fn.name, inst.operands[2]) << "\n";
                        break;
                    }
                    if (inst.operands[0].kind == OperandKind::Immediate && immValue(inst.operands[0]) == 0) {
                        loadOperand(out, layout, inst.operands[1], "t1");
                        out << "    " << (inst.opcode == IROpcode::BEQ ? "beq" : "bne")
                            << " zero, t1, " << labelName(fn.name, inst.operands[2]) << "\n";
                        break;
                    }
                    loadOperand(out, layout, inst.operands[0], "t0");
                    loadOperand(out, layout, inst.operands[1], "t1");
                    out << "    " << (inst.opcode == IROpcode::BEQ ? "beq" : "bne")
                        << " t0, t1, " << labelName(fn.name, inst.operands[2]) << "\n";
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
                        loadOperand(out, layout, pendingParams[i], argReg.c_str());
                    }
                    // 处理超过 8 个的参数：通过栈传递
                    if (pendingParams.size() > 8) {
                        int stackArgSize = static_cast<int>(pendingParams.size() - 8) * 4;
                        emitAdjustSP(out, stackArgSize, true);
                        // 此时 sp 已调整，vreg 实际位置在 sp + vregSlot + stackArgSize
                        for (size_t i = 8; i < pendingParams.size(); ++i) {
                            int storeOffset = static_cast<int>(i - 8) * 4;
                            if (pendingParams[i].kind == OperandKind::Immediate) {
                                out << "    li t0, " << immValue(pendingParams[i]) << "\n";
                            } else if (pendingParams[i].kind == OperandKind::VirtualReg) {
                                if (const std::string* cached = vregReg(layout, regId(pendingParams[i]))) {
                                    out << "    mv t0, " << *cached << "\n";
                                } else {
                                    int vregAdjOffset = vregSlot(layout, regId(pendingParams[i])) + stackArgSize;
                                    emitLoadSP(out, "t0", vregAdjOffset);
                                }
                            }
                            emitStoreSP(out, "t0", storeOffset);
                        }
                    }
                    out << "    call " << stringValue(inst.operands[1]) << "\n";
                    // 恢复栈指针
                    if (pendingParams.size() > 8) {
                        int stackArgSize = static_cast<int>(pendingParams.size() - 8) * 4;
                        emitAdjustSP(out, stackArgSize, false);
                    }
                    storeVReg(out, layout, inst.operands[0], "a0");
                    pendingParams.clear();
                    break;
                case IROpcode::RET:
                    if (inst.operands.empty()) {
                        out << "    li a0, 0\n";
                    } else if (inst.operands[0].kind == OperandKind::Immediate) {
                        out << "    li a0, " << immValue(inst.operands[0]) << "\n";
                    } else if (inst.operands[0].kind == OperandKind::VirtualReg) {
                        emitLoadSP(out, "a0", vregSlot(layout, regId(inst.operands[0])));
                    }
                    out << "    j " << returnLabel << "\n";
                    break;
                default:
                    break;
            }
        }

        out << returnLabel << ":\n";
        emitLoadSP(out, "ra", layout.raOffset);
        for (int i = static_cast<int>(layout.savedRegs.size()) - 1; i >= 0; --i) {
            emitLoadSP(out, layout.savedRegs[static_cast<size_t>(i)].c_str(), layout.saveBase + i * 4);
        }
        emitAdjustSP(out, layout.frameSize, false);
        out << "    ret\n";
    }

    return out.str();
}

}  // namespace toyc
