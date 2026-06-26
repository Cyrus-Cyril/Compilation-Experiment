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

    static IROperand reg(uint32_t id) { return {OperandKind::VirtualReg, id}; }
    static IROperand imm(int32_t v) { return {OperandKind::Immediate, v}; }
    static IROperand label(uint32_t id) { return {OperandKind::Label, id}; }
    static IROperand global(const std::string& n) { return {OperandKind::GlobalName, n}; }
    static IROperand func(const std::string& n) { return {OperandKind::FuncName, n}; }
};

/// IR 指令
struct IRInstruction {
    IROpcode opcode;
    std::vector<IROperand> operands;
};

/// 单个函数的 IR 片段
struct IRFunction {
    std::string name;
    std::vector<IRInstruction> instructions;
};

/// 全局对象信息（用于后端生成 .data 段）
struct IRGlobal {
    std::string name;
    bool isConst = false;
    int32_t initialValue = 0;
};

/// 整个程序的 IR（按函数组织）
struct IRProgram {
    /// 全局变量/常量定义
    std::vector<IRGlobal> globals;

    std::vector<IRFunction> functions;

    /// 获取全局变量名列表（用于 .data 段生成）
    std::vector<std::string> globalNames;
};

}  // namespace toyc
