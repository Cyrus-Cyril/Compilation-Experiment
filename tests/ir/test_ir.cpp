// ToyC IR 生成单元测试
// 验证三地址码的结构和指令类型

#include "toyc/parser/ast.h"
#include "toyc/parser/parser_api.h"
#include "toyc/semantic/semantic_analyzer.h"
#include "toyc/ir/ir_builder.h"
#include "toyc/ir/ir.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace toyc;

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* msg) {
    ++test_count;
    if (condition) {
        ++pass_count;
        printf("  PASS: %s\n", msg);
    } else {
        printf("  FAIL: %s\n", msg);
    }
}

/// 辅助：解析 → 语义分析 → IR 生成
static IRProgram buildIR(const char* src) {
    auto ast = parseString(src);
    if (!ast) return {};

    SemanticAnalyzer analyzer;
    if (analyzer.analyze(ast.get())) return {};

    return IRBuilder::build(ast.get(), &analyzer.symbolTable());
}

/// 查找函数
static const IRFunction* findFn(const IRProgram& prog, const char* name) {
    for (const auto& fn : prog.functions) {
        if (fn.name == name) return &fn;
    }
    return nullptr;
}

/// 检查函数中是否存在指定 opcode 的指令
static bool hasOp(const IRFunction& fn, IROpcode op) {
    for (const auto& inst : fn.instructions) {
        if (inst.opcode == op) return true;
    }
    return false;
}

/// 统计指定 opcode 的出现次数
static int countOp(const IRFunction& fn, IROpcode op) {
    int cnt = 0;
    for (const auto& inst : fn.instructions) {
        if (inst.opcode == op) ++cnt;
    }
    return cnt;
}

// ============================================================
// 基本 IR 结构
// ============================================================

static void test_basic_struct() {
    printf("-- Basic IR Structure --\n");

    {
        IRProgram prog = buildIR("int main() { return 42; }");
        const IRFunction* mainFn = findFn(prog, "main");
        check(mainFn != nullptr, "main function exists in IR");
        check(hasOp(*mainFn, IROpcode::RET), "contains RET instruction");
    }

    {
        IRProgram prog = buildIR(
            "int foo() { return 1; }\nint main() { return foo(); }");
        check(prog.functions.size() == 2, "two functions generated");
        check(findFn(prog, "foo") != nullptr, "foo function exists");
        check(findFn(prog, "main") != nullptr, "main function exists");
    }
}

// ============================================================
// 表达式 IR
// ============================================================

static void test_expressions() {
    printf("\n-- Expressions --\n");

    {
        IRProgram prog = buildIR(
            "int main() { return 1 + 2; }");
        const IRFunction* fn = findFn(prog, "main");
        check(fn != nullptr, "arithmetic IR generated");
        check(hasOp(*fn, IROpcode::ADD), "contains ADD for 1+2");
    }

    {
        IRProgram prog = buildIR(
            "int main() { return 5 * 3 - 2; }");
        const IRFunction* fn = findFn(prog, "main");
        check(hasOp(*fn, IROpcode::MUL), "contains MUL for 5*3");
        check(hasOp(*fn, IROpcode::SUB), "contains SUB for ...-2");
    }

    {
        IRProgram prog = buildIR(
            "int main() { if (1 < 2) { return 1; } return 0; }");
        const IRFunction* fn = findFn(prog, "main");
        check(hasOp(*fn, IROpcode::LT), "contains LT for 1<2");
        check(hasOp(*fn, IROpcode::BNE), "contains BNE for branch");
    }

    {
        IRProgram prog = buildIR(
            "int main() { return !0; }");
        const IRFunction* fn = findFn(prog, "main");
        check(hasOp(*fn, IROpcode::NOT), "contains NOT for !0");
    }

    {
        IRProgram prog = buildIR(
            "int main() { return -42; }");
        const IRFunction* fn = findFn(prog, "main");
        check(hasOp(*fn, IROpcode::NEG), "contains NEG for -42");
    }
}

// ============================================================
// 短路求值
// ============================================================

static void test_short_circuit() {
    printf("\n-- Short-Circuit Evaluation --\n");

    {
        IRProgram prog = buildIR(
            "int main() { if (1 && 0) { return 1; } return 0; }");
        const IRFunction* fn = findFn(prog, "main");
        check(hasOp(*fn, IROpcode::JMP), "&& generates JMP for short-circuit");
    }

    {
        IRProgram prog = buildIR(
            "int main() { if (1 || 0) { return 1; } return 0; }");
        const IRFunction* fn = findFn(prog, "main");
        check(hasOp(*fn, IROpcode::JMP), "|| generates JMP for short-circuit");
    }

    {
        IRProgram prog = buildIR(
            "int main() { return (1 && 0) || (1 && 1); }");
        const IRFunction* fn = findFn(prog, "main");
        check(fn != nullptr, "logical combo generates without crash");
    }
}

// ============================================================
// 控制流
// ============================================================

static void test_control_flow() {
    printf("\n-- Control Flow --\n");

    {
        IRProgram prog = buildIR(
            "int main() { if (1) { return 10; } else { return 20; } }");
        const IRFunction* fn = findFn(prog, "main");
        check(hasOp(*fn, IROpcode::JMP), "if-else generates JMP");
        check(hasOp(*fn, IROpcode::RET), "if-else has RET");
    }

    {
        IRProgram prog = buildIR(
            "int main() { while (0) { return 1; } return 0; }");
        const IRFunction* fn = findFn(prog, "main");
        check(hasOp(*fn, IROpcode::JMP), "while generates JMP for loop back");
        check(hasOp(*fn, IROpcode::LABEL), "while generates labels");
    }

    {
        IRProgram prog = buildIR(
            "int main() { while (1) { if (1) { break; } else { continue; } } return 0; }");
        const IRFunction* fn = findFn(prog, "main");
        check(fn != nullptr, "nested break/continue generates without crash");
        check(hasOp(*fn, IROpcode::JMP), "loop with break/continue has jumps");
    }
}

// ============================================================
// 局部变量
// ============================================================

static void test_local_vars() {
    printf("\n-- Local Variables --\n");

    {
        IRProgram prog = buildIR(
            "int main() { int x = 10; int y = x + 5; return y; }");
        const IRFunction* fn = findFn(prog, "main");
        check(fn != nullptr, "local var declaration generates IR");
        check(countOp(*fn, IROpcode::STORE_LOCAL) >= 2, "has STORE_LOCAL for x and y");
        check(hasOp(*fn, IROpcode::LOAD_LOCAL), "has LOAD_LOCAL for reading x");
        check(hasOp(*fn, IROpcode::ADD), "has ADD for x+5");
    }

    {
        IRProgram prog = buildIR(
            "int main() { int x = 1; x = x + 1; return x; }");
        const IRFunction* fn = findFn(prog, "main");
        check(hasOp(*fn, IROpcode::STORE_LOCAL), "assignment generates STORE_LOCAL");
    }
}

// ============================================================
// 函数调用
// ============================================================

static void test_function_calls() {
    printf("\n-- Function Calls --\n");

    {
        IRProgram prog = buildIR(
            "int add(int a, int b) { return a + b; }\nint main() { return add(1, 2); }");
        const IRFunction* mainFn = findFn(prog, "main");
        check(mainFn != nullptr, "caller function exists");
        check(hasOp(*mainFn, IROpcode::PARAM), "has PARAM for arg passing");
        check(hasOp(*mainFn, IROpcode::CALL), "has CALL instruction");
    }

    {
        IRProgram prog = buildIR(
            "int add(int a, int b) { return a + b; }\nint main() { return add(1, 2); }");
        const IRFunction* addFn = findFn(prog, "add");
        check(addFn != nullptr, "callee function exists");
        check(hasOp(*addFn, IROpcode::LOAD_LOCAL), "callee loads params");
    }
}

// ============================================================
// 全局变量
// ============================================================

static void test_globals() {
    printf("\n-- Global Variables --\n");

    {
        IRProgram prog = buildIR(
            "int g = 42;\nint main() { return g; }");
        check(!prog.globalNames.empty(), "globalNames is populated");
        check(prog.globalNames[0] == "g", "global name is 'g'");
        check(!prog.globals.empty(), "globals is populated");
        check(prog.globals[0].name == "g", "global object name is 'g'");
        check(!prog.globals[0].isConst, "global object records variable kind");
        check(prog.globals[0].initialValue == 42, "global initializer is preserved");
    }

    {
        IRProgram prog = buildIR(
            "const int N = 6 * 7;\nint main() { return N; }");
        check(prog.globals.size() == 1, "global const is recorded");
        check(prog.globals[0].name == "N", "global const name is 'N'");
        check(prog.globals[0].isConst, "global object records const kind");
        check(prog.globals[0].initialValue == 42, "global const initializer is folded");
    }

    {
        IRProgram prog = buildIR(
            "const int N = 40;\nint g = N + 2;\nint main() { return g; }");
        check(prog.globals.size() == 2, "multiple globals are recorded");
        check(prog.globals[1].name == "g", "second global name is 'g'");
        check(prog.globals[1].initialValue == 42, "global initializer can reference const");
    }

    {
        IRProgram prog = buildIR(
            "int g = 42;\nint main() { return g; }");
        const IRFunction* fn = findFn(prog, "main");
        check(hasOp(*fn, IROpcode::LOAD_GLOBAL), "LOAD_GLOBAL for reading global");
    }

    {
        IRProgram prog = buildIR(
            "int g = 0;\nint main() { g = 10; return g; }");
        const IRFunction* fn = findFn(prog, "main");
        check(hasOp(*fn, IROpcode::STORE_GLOBAL), "STORE_GLOBAL for writing global");
    }
}

// ============================================================
// 多层级作用域
// ============================================================

static void test_nested_scopes() {
    printf("\n-- Nested Scopes --\n");

    {
        IRProgram prog = buildIR(
            "int main() { int x = 1; { int y = 2; return x + y; } }");
        const IRFunction* fn = findFn(prog, "main");
        check(fn != nullptr, "nested scope generates without crash");
        check(countOp(*fn, IROpcode::STORE_LOCAL) >= 2, "both x and y get STORE_LOCAL");
    }
}

int main() {
    printf("=== ToyC IR Builder Unit Tests ===\n\n");

    test_basic_struct();
    test_expressions();
    test_short_circuit();
    test_control_flow();
    test_local_vars();
    test_function_calls();
    test_globals();
    test_nested_scopes();

    printf("\n==============================\n");
    printf("  %d / %d tests passed\n", pass_count, test_count);
    printf("==============================\n");

    return (pass_count == test_count) ? 0 : 1;
}
