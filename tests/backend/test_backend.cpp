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
    check(contains(asmText, "li a0, 42"), "returns constant in a0");
    check(contains(asmText, ".Lmain_return:"), "emits unified return label");
    check(contains(asmText, "lw ra, 12(sp)"), "restores return address");
    check(contains(asmText, "ret"), "emits ret instruction");
}

int main() {
    std::printf("=== ToyC Backend Unit Tests ===\n\n");

    test_minimal_function();

    std::printf("\n==============================\n");
    std::printf("  %d / %d tests passed\n", pass_count, test_count);
    std::printf("==============================\n");

    return (pass_count == test_count) ? 0 : 1;
}
