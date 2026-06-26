// ToyC RISC-V32 Code Generator 实现

#include "toyc/backend/code_generator.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>

namespace toyc {

namespace {

uint32_t regId(const IROperand& operand) {
    return std::get<uint32_t>(operand.value);
}

int32_t immValue(const IROperand& operand) {
    return std::get<int32_t>(operand.value);
}

}  // namespace

std::string CodeGenerator::generate(const IRProgram& program) {
    std::ostringstream out;

    out << ".text\n";
    out << ".globl main\n";

    for (const auto& fn : program.functions) {
        std::unordered_map<uint32_t, int32_t> constRegs;
        const std::string returnLabel = ".L" + fn.name + "_return";

        out << fn.name << ":\n";
        out << "    addi sp, sp, -16\n";
        out << "    sw ra, 12(sp)\n";

        for (const auto& inst : fn.instructions) {
            switch (inst.opcode) {
                case IROpcode::ADD:
                    if (inst.operands.size() == 3 &&
                        inst.operands[0].kind == OperandKind::VirtualReg &&
                        inst.operands[1].kind == OperandKind::Immediate &&
                        inst.operands[2].kind == OperandKind::Immediate) {
                        constRegs[regId(inst.operands[0])] =
                            immValue(inst.operands[1]) + immValue(inst.operands[2]);
                    }
                    break;
                case IROpcode::RET:
                    if (inst.operands.empty()) {
                        out << "    li a0, 0\n";
                    } else if (inst.operands[0].kind == OperandKind::Immediate) {
                        out << "    li a0, " << immValue(inst.operands[0]) << "\n";
                    } else if (inst.operands[0].kind == OperandKind::VirtualReg) {
                        uint32_t retReg = regId(inst.operands[0]);
                        auto found = constRegs.find(retReg);
                        out << "    li a0, " << (found != constRegs.end() ? found->second : 0) << "\n";
                    }
                    out << "    j " << returnLabel << "\n";
                    break;
                default:
                    break;
            }
        }

        out << returnLabel << ":\n";
        out << "    lw ra, 12(sp)\n";
        out << "    addi sp, sp, 16\n";
        out << "    ret\n";
    }

    return out.str();
}

}  // namespace toyc
