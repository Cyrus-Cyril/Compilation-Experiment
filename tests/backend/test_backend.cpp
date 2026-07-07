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
    check(contains(asmText, "s1") || contains(asmText, "s2"), "uses saved-register cache");
    check(contains(asmText, "slti t2,") || contains(asmText, "slt t2,"), "emits less-than comparison");
    check(contains(asmText, "seqz t1, t0"), "emits logical not");
    check(contains(asmText, "neg t1, t0"), "emits unary negation");
}

static void test_globals_and_control_flow() {
    std::printf("\n-- Globals And Control Flow --\n");

    IRProgram program;
    program.globals.push_back({"g", false, 7});

    IRFunction mainFn;
    mainFn.name = "main";
    mainFn.instructions.push_back(
        {IROpcode::LOAD_GLOBAL, {IROperand::reg(0), IROperand::global("g")}});
    mainFn.instructions.push_back(
        {IROpcode::STORE_GLOBAL, {IROperand::global("g"), IROperand::reg(0)}});
    mainFn.instructions.push_back(
        {IROpcode::BNE, {IROperand::reg(0), IROperand::imm(0), IROperand::label(1)}});
    mainFn.instructions.push_back({IROpcode::JMP, {IROperand::label(2)}});
    mainFn.instructions.push_back({IROpcode::LABEL, {IROperand::label(1)}});
    mainFn.instructions.push_back(
        {IROpcode::OR, {IROperand::reg(1), IROperand::reg(0), IROperand::imm(0)}});
    mainFn.instructions.push_back({IROpcode::LABEL, {IROperand::label(2)}});
    mainFn.instructions.push_back({IROpcode::RET, {IROperand::reg(0)}});
    program.functions.push_back(std::move(mainFn));

    CodeGenerator gen;
    std::string asmText = gen.generate(program);

    check(contains(asmText, ".data"), "emits data section");
    check(contains(asmText, "g:"), "emits global label");
    check(contains(asmText, ".word 7"), "emits global initializer");
    check(contains(asmText, "la t0, g"), "loads global address");
    check(contains(asmText, "lw t1, 0(t0)"), "loads global value");
    check(contains(asmText, "la t1, g"), "loads global address for store");
    check(contains(asmText, "sw t0, 0(t1)"), "stores global value");
    check(contains(asmText, "bne ") && contains(asmText, ".Lmain_1"), "emits conditional branch");
    check(contains(asmText, "j .Lmain_2"), "emits jump");
    check(contains(asmText, ".Lmain_1:"), "emits first label");
    check(contains(asmText, ".Lmain_2:"), "emits second label");
    check(contains(asmText, "or ") && contains(asmText, "snez "), "emits logical or");
}

static void test_function_calls() {
    std::printf("\n-- Function Calls --\n");

    IRProgram program;

    IRFunction addFn;
    addFn.name = "add";
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
        {IROpcode::ADD, {IROperand::reg(0), IROperand::imm(0), IROperand::imm(1)}});
    mainFn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(1), IROperand::imm(0), IROperand::imm(3)}});
    mainFn.instructions.push_back({IROpcode::PARAM, {IROperand::reg(0)}});
    mainFn.instructions.push_back({IROpcode::PARAM, {IROperand::reg(1)}});
    mainFn.instructions.push_back({IROpcode::CALL, {IROperand::reg(2), IROperand::func("add")}});
    mainFn.instructions.push_back({IROpcode::RET, {IROperand::reg(2)}});
    program.functions.push_back(std::move(mainFn));

    CodeGenerator gen;
    std::string asmText = gen.generate(program);

    check(contains(asmText, "add:"), "emits callee label");
    check(contains(asmText, "sw a0, 16(sp)"), "callee saves first argument register");
    check(contains(asmText, "sw a1, 20(sp)"), "callee saves second argument register");
    check(contains(asmText, "lw a0,"), "loads first argument before call");
    check(contains(asmText, "lw a1,"), "loads second argument before call");
    check(contains(asmText, "call add"), "emits call instruction");
    check(contains(asmText, "sw a0,"), "stores call return value");
}

static void test_recursive_call_shape() {
    std::printf("\n-- Recursive Call Shape --\n");

    IRProgram program;
    IRFunction factFn;
    factFn.name = "fact";
    factFn.instructions.push_back({IROpcode::PARAM, {IROperand::reg(0)}});
    factFn.instructions.push_back({IROpcode::CALL, {IROperand::reg(1), IROperand::func("fact")}});
    factFn.instructions.push_back({IROpcode::RET, {IROperand::reg(1)}});
    program.functions.push_back(std::move(factFn));

    CodeGenerator gen;
    std::string asmText = gen.generate(program);

    check(contains(asmText, "fact:"), "emits recursive function label");
    check(contains(asmText, "call fact"), "emits recursive call");
}

static void test_cached_return_value() {
    std::printf("\n-- Cached Return Value --\n");

    IRProgram program;
    IRFunction mainFn;
    mainFn.name = "main";
    mainFn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(0), IROperand::imm(0), IROperand::imm(7)}});
    mainFn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(1), IROperand::reg(0), IROperand::imm(1)}});
    mainFn.instructions.push_back(
        {IROpcode::ADD, {IROperand::reg(2), IROperand::reg(0), IROperand::imm(2)}});
    mainFn.instructions.push_back({IROpcode::RET, {IROperand::reg(0)}});
    program.functions.push_back(std::move(mainFn));

    CodeGenerator gen;
    std::string asmText = gen.generate(program);

    check(contains(asmText, "mv a0, s1"), "returns value from cached saved register");
}

int main() {
    std::printf("=== ToyC Backend Unit Tests ===\n\n");

    test_minimal_function();
    test_expressions_and_locals();
    test_globals_and_control_flow();
    test_function_calls();
    test_recursive_call_shape();
    test_cached_return_value();

    std::printf("\n==============================\n");
    std::printf("  %d / %d tests passed\n", pass_count, test_count);
    std::printf("==============================\n");

    return (pass_count == test_count) ? 0 : 1;
}
