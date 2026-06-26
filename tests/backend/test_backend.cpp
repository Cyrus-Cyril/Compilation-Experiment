#include "toyc/backend/code_generator.h"
#include "toyc/ir/ir.h"

#include <cstdio>
#include <string>

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

static bool contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

static IRProgram buildReturnConstProgram(int value) {
    IRProgram program;
    IRFunction mainFn;
    mainFn.name = "main";
    mainFn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(0), IROperand::imm(0), IROperand::imm(value)}});
    mainFn.instructions.push_back({IROpcode::RET, {IROperand::reg(0)}});
    program.functions.push_back(std::move(mainFn));
    return program;
}

static void test_minimal_function() {
    std::printf("-- Minimal Function --\n");

    CodeGenerator gen;
    std::string asmText = gen.generate(buildReturnConstProgram(42));

    check(contains(asmText, ".text"), "emits text section");
    check(contains(asmText, ".globl main"), "exports main");
    check(contains(asmText, "main:"), "emits main label");
    check(contains(asmText, "addi sp, sp, -16"), "creates stack frame");
    check(contains(asmText, "sw ra, 12(sp)"), "saves return address");
    check(contains(asmText, "li t1, 42"), "loads constant operand");
    check(contains(asmText, "add t2, t0, t1"), "computes constant expression");
    check(contains(asmText, "lw a0, 0(sp)"), "loads return value into a0");
    check(contains(asmText, ".Lmain_return:"), "emits unified return label");
    check(contains(asmText, "lw ra, 12(sp)"), "restores return address");
    check(contains(asmText, "ret"), "emits ret instruction");
}

static void test_expressions_and_locals() {
    std::printf("\n-- Expressions And Locals --\n");

    IRProgram program;
    IRFunction mainFn;
    mainFn.name = "main";
    mainFn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(0), IROperand::imm(1), IROperand::imm(2)}});
    mainFn.instructions.push_back(
        {IROpcode::MUL, {IROperand::reg(1), IROperand::reg(0), IROperand::imm(3)}});
    mainFn.instructions.push_back(
        {IROpcode::STORE_LOCAL, {IROperand::imm(0), IROperand::reg(1)}});
    mainFn.instructions.push_back(
        {IROpcode::LOAD_LOCAL, {IROperand::reg(2), IROperand::imm(0)}});
    mainFn.instructions.push_back(
        {IROpcode::LT, {IROperand::reg(3), IROperand::reg(2), IROperand::imm(10)}});
    mainFn.instructions.push_back(
        {IROpcode::NOT, {IROperand::reg(4), IROperand::reg(3)}});
    mainFn.instructions.push_back(
        {IROpcode::NEG, {IROperand::reg(5), IROperand::reg(4)}});
    mainFn.instructions.push_back({IROpcode::RET, {IROperand::reg(5)}});
    program.functions.push_back(std::move(mainFn));

    CodeGenerator gen;
    std::string asmText = gen.generate(program);

    check(contains(asmText, "mul t2, t0, t1"), "emits multiplication");
    check(contains(asmText, "sw t0, 0(sp)"), "stores local variable");
    check(contains(asmText, "lw t0, 0(sp)"), "loads local variable");
    check(contains(asmText, "slt t2, t0, t1"), "emits less-than comparison");
    check(contains(asmText, "seqz t1, t0"), "emits logical not");
    check(contains(asmText, "neg t1, t0"), "emits unary negation");
}

int main() {
    std::printf("=== ToyC Backend Unit Tests ===\n\n");

    test_minimal_function();
    test_expressions_and_locals();

    std::printf("\n==============================\n");
    std::printf("  %d / %d tests passed\n", pass_count, test_count);
    std::printf("==============================\n");

    return (pass_count == test_count) ? 0 : 1;
}
