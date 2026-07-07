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

    check(insts.size() == 1, "removes dead folded temporaries");
    check(insts[0].opcode == IROpcode::RET, "keeps final return");
    check(insts[0].operands[0].kind == OperandKind::Immediate, "propagates return operand");
    check(immAt(insts[0], 0) == 9, "return uses folded value");
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

static void test_algebraic_simplification() {
    std::printf("\n-- Algebraic Simplification --\n");

    IRProgram program;
    IRFunction fn;
    fn.name = "main";
    fn.instructions.push_back(
        {IROpcode::LOAD_LOCAL, {IROperand::reg(0), IROperand::imm(0)}});
    fn.instructions.push_back(
        {IROpcode::MUL, {IROperand::reg(1), IROperand::reg(0), IROperand::imm(1)}});
    fn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(2), IROperand::reg(1), IROperand::imm(0)}});
    fn.instructions.push_back({IROpcode::RET, {IROperand::reg(2)}});
    program.functions.push_back(std::move(fn));

    IRProgram optimized = Optimizer{}.optimize(program);
    const auto& insts = optimized.functions[0].instructions;

    check(insts.size() == 2, "removes dead algebraic copy chain");
    check(insts[0].opcode == IROpcode::LOAD_LOCAL, "keeps live local load");
    check(insts[1].opcode == IROpcode::RET, "keeps return");
    check(insts[1].operands[0].kind == OperandKind::VirtualReg, "return uses copied source register");
    check(std::get<uint32_t>(insts[1].operands[0].value) == 0, "return resolves to original register");
}

static void test_constant_branch_and_jump_cleanup() {
    std::printf("\n-- Branch Folding And Jump Cleanup --\n");

    IRProgram program;
    IRFunction fn;
    fn.name = "main";
    fn.instructions.push_back(
        {IROpcode::BEQ, {IROperand::imm(1), IROperand::imm(1), IROperand::label(1)}});
    fn.instructions.push_back({IROpcode::LABEL, {IROperand::label(1)}});
    fn.instructions.push_back(
        {IROpcode::BNE, {IROperand::imm(2), IROperand::imm(2), IROperand::label(2)}});
    fn.instructions.push_back({IROpcode::RET, {IROperand::imm(0)}});
    program.functions.push_back(std::move(fn));

    IRProgram optimized = Optimizer{}.optimize(program);
    const auto& insts = optimized.functions[0].instructions;

    check(insts.size() == 2, "removes jump to immediately following label and false branch");
    check(insts[0].opcode == IROpcode::LABEL, "keeps target label");
    check(insts[1].opcode == IROpcode::RET, "keeps return after folded branches");
}

static void test_copy_cse_and_dead_code() {
    std::printf("\n-- Copy Propagation CSE And DCE --\n");

    IRProgram program;
    IRFunction fn;
    fn.name = "main";
    fn.instructions.push_back(
        {IROpcode::LOAD_LOCAL, {IROperand::reg(0), IROperand::imm(0)}});
    fn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(1), IROperand::reg(0), IROperand::imm(1)}});
    fn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(2), IROperand::reg(0), IROperand::imm(1)}});
    fn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(3), IROperand::reg(1), IROperand::reg(2)}});
    fn.instructions.push_back(
        {IROpcode::MUL, {IROperand::reg(4), IROperand::imm(9), IROperand::imm(9)}});
    fn.instructions.push_back({IROpcode::RET, {IROperand::reg(3)}});
    program.functions.push_back(std::move(fn));

    IRProgram optimized = Optimizer{}.optimize(program);
    const auto& insts = optimized.functions[0].instructions;

    int repeatedAddCount = 0;
    bool hasDeadMul = false;
    for (const auto& inst : insts) {
        if (inst.opcode == IROpcode::ADD && inst.operands.size() == 3 &&
            inst.operands[1].kind == OperandKind::VirtualReg &&
            std::get<uint32_t>(inst.operands[1].value) == 0 &&
            inst.operands[2].kind == OperandKind::Immediate &&
            immAt(inst, 2) == 1) {
            ++repeatedAddCount;
        }
        if (inst.opcode == IROpcode::MUL) hasDeadMul = true;
    }

    check(repeatedAddCount == 1, "eliminates repeated basic-block expression");
    check(!hasDeadMul, "removes unused pure computation");
}

static void test_global_const_propagation() {
    std::printf("\n-- Global Const Propagation --\n");

    IRProgram program;
    program.globals.push_back({"N", true, 5});
    IRFunction fn;
    fn.name = "main";
    fn.instructions.push_back(
        {IROpcode::LOAD_GLOBAL, {IROperand::reg(0), IROperand::global("N")}});
    fn.instructions.push_back({IROpcode::RET, {IROperand::reg(0)}});
    program.functions.push_back(std::move(fn));

    IRProgram optimized = Optimizer{}.optimize(program);
    const auto& insts = optimized.functions[0].instructions;

    check(insts.size() == 1, "removes const global load");
    check(insts[0].opcode == IROpcode::RET, "keeps return");
    check(insts[0].operands[0].kind == OperandKind::Immediate, "returns propagated const");
    check(immAt(insts[0], 0) == 5, "global const value is propagated");
}

static void test_tail_recursion_rewrite() {
    std::printf("\n-- Tail Recursion Rewrite --\n");

    IRProgram program;
    IRFunction fn;
    fn.name = "fact";
    fn.paramCount = 1;
    fn.instructions.push_back(
        {IROpcode::STORE_LOCAL, {IROperand::imm(0), IROperand::reg(0)}});
    fn.instructions.push_back(
        {IROpcode::LOAD_LOCAL, {IROperand::reg(1), IROperand::imm(0)}});
    fn.instructions.push_back(
        {IROpcode::SUB, {IROperand::reg(2), IROperand::reg(1), IROperand::imm(1)}});
    fn.instructions.push_back({IROpcode::PARAM, {IROperand::reg(2)}});
    fn.instructions.push_back({IROpcode::CALL, {IROperand::reg(3), IROperand::func("fact")}});
    fn.instructions.push_back({IROpcode::RET, {IROperand::reg(3)}});
    program.functions.push_back(std::move(fn));

    IRProgram optimized = Optimizer{}.optimize(program);
    const auto& insts = optimized.functions[0].instructions;

    bool hasSelfCall = false;
    bool hasBackJump = false;
    for (const auto& inst : insts) {
        if (inst.opcode == IROpcode::CALL) hasSelfCall = true;
        if (inst.opcode == IROpcode::JMP) hasBackJump = true;
    }

    check(!hasSelfCall, "removes tail self call");
    check(hasBackJump, "rewrites tail recursion to loop jump");
}

static void test_small_function_inlining() {
    std::printf("\n-- Small Function Inlining --\n");

    IRProgram program;

    IRFunction addFn;
    addFn.name = "add";
    addFn.paramCount = 2;
    addFn.instructions.push_back(
        {IROpcode::STORE_LOCAL, {IROperand::imm(0), IROperand::reg(0)}});
    addFn.instructions.push_back(
        {IROpcode::STORE_LOCAL, {IROperand::imm(4), IROperand::reg(1)}});
    addFn.instructions.push_back(
        {IROpcode::LOAD_LOCAL, {IROperand::reg(2), IROperand::imm(0)}});
    addFn.instructions.push_back(
        {IROpcode::LOAD_LOCAL, {IROperand::reg(3), IROperand::imm(4)}});
    addFn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(4), IROperand::reg(2), IROperand::reg(3)}});
    addFn.instructions.push_back({IROpcode::RET, {IROperand::reg(4)}});
    program.functions.push_back(std::move(addFn));

    IRFunction mainFn;
    mainFn.name = "main";
    mainFn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(0), IROperand::imm(0), IROperand::imm(4)}});
    mainFn.instructions.push_back({IROpcode::PARAM, {IROperand::reg(0)}});
    mainFn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(1), IROperand::imm(0), IROperand::imm(5)}});
    mainFn.instructions.push_back({IROpcode::PARAM, {IROperand::reg(1)}});
    mainFn.instructions.push_back({IROpcode::CALL, {IROperand::reg(2), IROperand::func("add")}});
    mainFn.instructions.push_back({IROpcode::RET, {IROperand::reg(2)}});
    program.functions.push_back(std::move(mainFn));

    IRProgram optimized = Optimizer{}.optimize(program);
    const auto& mainInsts = optimized.functions[1].instructions;

    bool hasCall = false;
    for (const auto& inst : mainInsts) {
        if (inst.opcode == IROpcode::CALL) hasCall = true;
    }

    check(!hasCall, "inlines small straight-line function into caller");
    check(mainInsts.size() == 1, "folds inlined constant helper call");
    check(mainInsts[0].opcode == IROpcode::RET, "keeps final return after inlining");
    check(mainInsts[0].operands[0].kind == OperandKind::Immediate, "returns folded inlined value");
    check(immAt(mainInsts[0], 0) == 9, "inlined helper returns expected value");
}

int main() {
    std::printf("=== ToyC Optimizer Unit Tests ===\n\n");

    test_constant_folding();
    test_unreachable_after_return();
    test_algebraic_simplification();
    test_constant_branch_and_jump_cleanup();
    test_copy_cse_and_dead_code();
    test_global_const_propagation();
    test_tail_recursion_rewrite();
    test_small_function_inlining();

    std::printf("\n==============================\n");
    std::printf("  %d / %d tests passed\n", pass_count, test_count);
    std::printf("==============================\n");

    return (pass_count == test_count) ? 0 : 1;
}
