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

static bool notContains(const std::string& text, const char* needle) {
    return text.find(needle) == std::string::npos;
}

static bool appearsBefore(const std::string& text, const char* lhs, const char* rhs) {
    size_t lhsPos = text.find(lhs);
    size_t rhsPos = text.find(rhs);
    return lhsPos != std::string::npos && rhsPos != std::string::npos && lhsPos < rhsPos;
}

static bool appearsInOrder(const std::string& text, const char* first, const char* second, const char* third) {
    size_t firstPos = text.find(first);
    if (firstPos == std::string::npos) return false;
    size_t secondPos = text.find(second, firstPos + std::string(first).size());
    if (secondPos == std::string::npos) return false;
    size_t thirdPos = text.find(third, secondPos + std::string(second).size());
    return thirdPos != std::string::npos;
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
    check(!contains(asmText, "sw ra,"), "omits return address save in leaf function");
    check(contains(asmText, "li t1, 42"), "loads constant operand");
    check(contains(asmText, "add t2, t0, t1"), "computes constant expression");
    check(contains(asmText, "lw a0,") || contains(asmText, "mv a0,") ||
          contains(asmText, "li a0,"), "loads return value into a0");
    check(contains(asmText, ".Lmain_return:"), "emits unified return label");
    check(!contains(asmText, "lw ra,"), "omits return address restore in leaf function");
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

    check(contains(asmText, "mul ") || contains(asmText, "slli "), "emits multiplication");
    check(contains(asmText, "s1") || contains(asmText, "s2") ||
          contains(asmText, "t4") || contains(asmText, "t5") || contains(asmText, "t6"),
          "uses register cache");
    check(contains(asmText, "slti ") || contains(asmText, "slt "), "emits less-than comparison");
    check(contains(asmText, "seqz "), "emits logical not");
    check(contains(asmText, "neg "), "emits unary negation");
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
    mainFn.instructions.push_back({IROpcode::RET, {IROperand::reg(1)}});
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
    check(contains(asmText, "a0"), "callee handles first argument register");
    check(contains(asmText, "a1"), "callee handles second argument register");
    check(contains(asmText, "a0"), "loads first argument before call");
    check(contains(asmText, "a1"), "loads second argument before call");
    check(contains(asmText, "call add"), "emits call instruction");
    check(contains(asmText, "a0"), "handles call return value");
    check(appearsBefore(asmText, "add t2, t0, t1\n    sw t2,", "li t1, 3"),
          "spills first argument before reusing scratch register");
    check(appearsInOrder(asmText, "li t1, 3", "sw t2,", "lw a0,"),
          "spills second argument before loading call arguments");
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

static void test_many_stack_arguments() {
    std::printf("\n-- Many Stack Arguments --\n");

    IRProgram program;

    IRFunction sum10Fn;
    sum10Fn.name = "sum10";
    sum10Fn.paramCount = 10;
    for (int i = 0; i < 10; ++i) {
        sum10Fn.instructions.push_back(
            {IROpcode::STORE_LOCAL, {IROperand::imm(i * 4), IROperand::reg(static_cast<uint32_t>(i))}});
    }
    sum10Fn.instructions.push_back(
        {IROpcode::LOAD_LOCAL, {IROperand::reg(10), IROperand::imm(32)}});
    sum10Fn.instructions.push_back(
        {IROpcode::LOAD_LOCAL, {IROperand::reg(11), IROperand::imm(36)}});
    sum10Fn.instructions.push_back(
        {IROpcode::SUB, {IROperand::reg(12), IROperand::reg(11), IROperand::reg(10)}});
    sum10Fn.instructions.push_back({IROpcode::RET, {IROperand::reg(12)}});
    program.functions.push_back(std::move(sum10Fn));

    IRFunction mainFn;
    mainFn.name = "main";
    for (int i = 0; i < 10; ++i) {
        mainFn.instructions.push_back({IROpcode::PARAM, {IROperand::imm(i)}});
    }
    mainFn.instructions.push_back({IROpcode::CALL, {IROperand::reg(0), IROperand::func("sum10")}});
    mainFn.instructions.push_back({IROpcode::RET, {IROperand::reg(0)}});
    program.functions.push_back(std::move(mainFn));

    CodeGenerator gen;
    std::string asmText = gen.generate(program);

    check(contains(asmText, "sum10:"), "emits ten-argument callee");
    check(contains(asmText, "lw t0,") && contains(asmText, "sw t0,"), "imports stack arguments");
    check(contains(asmText, "li t0, 8") && contains(asmText, "li t0, 9"),
          "passes ninth and tenth arguments on caller stack");
    check(notContains(asmText, "mv s9, t0\n    mv s10, t0"),
          "does not alias multiple stack parameters to the same scratch register");
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

    check(contains(asmText, "mv a0, s1") || contains(asmText, "mv a0, t4") ||
          contains(asmText, "mv a0, t5") || contains(asmText, "mv a0, t6"),
          "returns value from cached register");
}

int main() {
    std::printf("=== ToyC Backend Unit Tests ===\n\n");

    test_minimal_function();
    test_expressions_and_locals();
    test_globals_and_control_flow();
    test_function_calls();
    test_recursive_call_shape();
    test_many_stack_arguments();
    test_cached_return_value();

    std::printf("\n==============================\n");
    std::printf("  %d / %d tests passed\n", pass_count, test_count);
    std::printf("==============================\n");

    return (pass_count == test_count) ? 0 : 1;
}
