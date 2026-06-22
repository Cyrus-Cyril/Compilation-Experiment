#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace toyc {

/// IR 操作码
enum class IROpcode {
    ADD, SUB, MUL, DIV, MOD, NEG,
    EQ, NE, LT, GT, LE, GE,
    AND, OR, NOT,
    LOAD_GLOBAL, STORE_GLOBAL,
    LOAD_LOCAL, STORE_LOCAL,
    JMP, BEQ, BNE,
    PARAM, CALL, RET,
    LABEL,
};

/// IR 操作数类型
enum class OperandKind { VirtualReg, Immediate, Label, GlobalName, FuncName };

/// IR 操作数
struct IROperand {
    OperandKind kind;
    std::variant<uint32_t, int32_t, std::string> value;

    static IROperand reg(uint32_t id);
    static IROperand imm(int32_t v);
    static IROperand label(uint32_t id);
    static IROperand global(const std::string& n);
    static IROperand func(const std::string& n);
};

/// IR 指令
struct IRInstruction {
    IROpcode opcode;
    std::vector<IROperand> operands;
};

/// 整个程序的 IR
using IRProgram = std::vector<IRInstruction>;

}  // namespace toyc