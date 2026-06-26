#include "toyc/ir/ir.h"
#include "toyc/optimize/optimizer.h"

#include <cstdio>
#include <variant>

using namespace toyc;

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* msg) {
    ++test_count;
    if (condition) {
        ++pass_count;
        std::printf("  PASS: %s\n", msg);
    } else {
        std::printf("  FAIL: %s\n", msg);
    }
}

static int immAt(const IRInstruction& inst, size_t index) {
    return std::get<int32_t>(inst.operands[index].value);
}

static void test_constant_folding() {
    std::printf("-- Constant Folding --\n");

    IRProgram program;
    IRFunction fn;
    fn.name = "main";
    fn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(0), IROperand::imm(1), IROperand::imm(2)}});
    fn.instructions.push_back(
        {IROpcode::MUL, {IROperand::reg(1), IROperand::reg(0), IROperand::imm(3)}});
    fn.instructions.push_back({IROpcode::RET, {IROperand::reg(1)}});
    program.functions.push_back(std::move(fn));

    IRProgram optimized = Optimizer{}.optimize(program);
    const auto& insts = optimized.functions[0].instructions;

    check(insts[0].opcode == IROpcode::ADD, "keeps folded constants as ADD pseudo-load");
    check(insts[0].operands[1].kind == OperandKind::Immediate, "first folded lhs is immediate");
    check(immAt(insts[0], 2) == 3, "folds 1+2 to 3");
    check(immAt(insts[1], 2) == 9, "propagates and folds 3*3 to 9");
    check(insts[2].operands[0].kind == OperandKind::Immediate, "propagates return operand");
    check(immAt(insts[2], 0) == 9, "return uses folded value");
}

static void test_unreachable_after_return() {
    std::printf("\n-- Unreachable After Return --\n");

    IRProgram program;
    IRFunction fn;
    fn.name = "main";
    fn.instructions.push_back({IROpcode::RET, {IROperand::imm(0)}});
    fn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(0), IROperand::imm(1), IROperand::imm(2)}});
    fn.instructions.push_back({IROpcode::LABEL, {IROperand::label(1)}});
    fn.instructions.push_back({IROpcode::RET, {IROperand::imm(1)}});
    program.functions.push_back(std::move(fn));

    IRProgram optimized = Optimizer{}.optimize(program);
    const auto& insts = optimized.functions[0].instructions;

    check(insts.size() == 3, "removes instruction after RET before next LABEL");
    check(insts[0].opcode == IROpcode::RET, "keeps first return");
    check(insts[1].opcode == IROpcode::LABEL, "keeps reachable label boundary");
    check(insts[2].opcode == IROpcode::RET, "keeps code after label");
}

int main() {
    std::printf("=== ToyC Optimizer Unit Tests ===\n\n");

    test_constant_folding();
    test_unreachable_after_return();

    std::printf("\n==============================\n");
    std::printf("  %d / %d tests passed\n", pass_count, test_count);
    std::printf("==============================\n");

    return (pass_count == test_count) ? 0 : 1;
}
