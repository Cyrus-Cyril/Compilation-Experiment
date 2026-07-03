// ToyC RISC-V32 Code Generator 实现

#include "toyc/backend/code_generator.h"

#include <cstdint>
#include <algorithm>
#include <sstream>
#include <string>
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

std::string stringValue(const IROperand& operand) {
    return std::get<std::string>(operand.value);
}

std::string labelName(const std::string& functionName, const IROperand& operand) {
    return ".L" + functionName + "_" + std::to_string(std::get<uint32_t>(operand.value));
}

int align16(int value) {
    return (value + 15) / 16 * 16;
}

struct FunctionLayout {
    int frameSize = 16;
    int localSize = 0;
    int vregBase = 0;
    int vregCount = 0;
    int raOffset = 12;
};

void scanOperand(const IROperand& operand, uint32_t& maxReg) {
    if (operand.kind == OperandKind::VirtualReg) {
        maxReg = std::max(maxReg, regId(operand));
    }
}

FunctionLayout buildLayout(const IRFunction& fn) {
    uint32_t maxReg = 0;
    bool hasReg = false;
    int maxLocalEnd = 0;

    for (const auto& inst : fn.instructions) {
        for (const auto& operand : inst.operands) {
            if (operand.kind == OperandKind::VirtualReg) {
                hasReg = true;
                scanOperand(operand, maxReg);
            }
        }

        if ((inst.opcode == IROpcode::LOAD_LOCAL || inst.opcode == IROpcode::STORE_LOCAL) &&
            !inst.operands.empty() &&
            inst.operands[0].kind == OperandKind::Immediate) {
            maxLocalEnd = std::max(maxLocalEnd, immValue(inst.operands[0]) + 4);
        }
    }

    FunctionLayout layout;
    layout.localSize = align16(maxLocalEnd);
    layout.vregBase = layout.localSize;
    layout.vregCount = hasReg ? static_cast<int>(maxReg + 1) : 0;
    int vregSize = layout.vregCount * 4;
    layout.frameSize = align16(layout.localSize + vregSize + 4);
    if (layout.frameSize < 16) layout.frameSize = 16;
    layout.raOffset = layout.frameSize - 4;
    return layout;
}

int localSlot(const FunctionLayout& layout, int irOffset) {
    (void)layout;
    return irOffset;
}

int vregSlot(const FunctionLayout& layout, uint32_t id) {
    return layout.vregBase + static_cast<int>(id) * 4;
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
        emitLoadSP(out, physReg, vregSlot(layout, regId(operand)));
    }
}

void storeVReg(std::ostringstream& out, const FunctionLayout& layout,
               const IROperand& operand, const char* physReg) {
    if (operand.kind == OperandKind::VirtualReg) {
        emitStoreSP(out, physReg, vregSlot(layout, regId(operand)));
    }
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
        FunctionLayout layout = buildLayout(fn);
        const std::string returnLabel = ".L" + fn.name + "_return";
        std::vector<IROperand> pendingParams;

        out << fn.name << ":\n";
        emitAdjustSP(out, layout.frameSize, true);
        emitStoreSP(out, "ra", layout.raOffset);
        for (int i = 0; i < layout.vregCount && i < 8; ++i) {
            std::string reg = "a" + std::to_string(i);
            emitStoreSP(out, reg.c_str(), vregSlot(layout, static_cast<uint32_t>(i)));
        }

        for (const auto& inst : fn.instructions) {
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
                    emitLoadSP(out, "t0", localSlot(layout, immValue(inst.operands[1])));
                    storeVReg(out, layout, inst.operands[0], "t0");
                    break;
                case IROpcode::STORE_LOCAL:
                    if (inst.operands.size() != 2) break;
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
                    for (size_t i = 0; i < pendingParams.size() && i < 8; ++i) {
                        std::string argReg = "a" + std::to_string(i);
                        loadOperand(out, layout, pendingParams[i], argReg.c_str());
                    }
                    out << "    call " << stringValue(inst.operands[1]) << "\n";
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
        emitAdjustSP(out, layout.frameSize, false);
        out << "    ret\n";
    }

    return out.str();
}

}  // namespace toyc
