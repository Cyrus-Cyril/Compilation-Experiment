// ToyC 语义分析单元测试
// 测试符号表构建、各类语义检查

#include "toyc/parser/ast.h"
#include "toyc/parser/parser_api.h"
#include "toyc/semantic/semantic_analyzer.h"
#include "toyc/semantic/symbol_table.h"

#include <cstdio>
#include <cstring>

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

/// 辅助：解析并做语义分析，返回分析器
static std::pair<bool, std::unique_ptr<CompUnit>>
parseAndAnalyze(const char* src, SemanticAnalyzer& analyzer) {
    auto ast = parseString(src);
    if (!ast) return {false, nullptr};
    bool ok = analyzer.analyze(ast.get());
    return {!ok, std::move(ast)};
}

// ============================================================
// 基本声明测试
// ============================================================

static void test_basic_declarations() {
    printf("-- Basic Declarations --\n");

    // 正确：简单主函数
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main() { return 0; }", a);
        check(ok && a.errors().empty(), "simple main function passes");
    }

    // 错误：缺少 main 函数
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int foo() { return 0; }", a);
        check(!ok || !a.errors().empty(), "missing main function detected");
    }

    // 错误：main 函数参数签名错误
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main(int x) { return x; }", a);
        check(!ok || !a.errors().empty(), "main with arguments rejected");
    }

    // 错误：main 返回 void
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "void main() { return; }", a);
        check(!ok || !a.errors().empty(), "void main rejected");
    }

    // 正确：全局变量
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int g = 42;\nint main() { return g; }", a);
        check(ok && a.errors().empty(), "global variable declaration");
    }

    // 正确：全局常量
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "const int N = 10;\nint main() { return N; }", a);
        check(ok && a.errors().empty(), "global const declaration");
    }
}

// ============================================================
// 重定义检查
// ============================================================

static void test_redefinition() {
    printf("\n-- Redefinition --\n");

    // 错误：函数重定义
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int f() { return 1; }\nint f() { return 2; }\nint main() { return 0; }", a);
        check(!ok || !a.errors().empty(), "function redefinition detected");
    }

    // 错误：局部变量重定义
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main() { int x = 1; int x = 2; return x; }", a);
        check(!ok || !a.errors().empty(), "local variable redefinition detected");
    }

    // 错误：参数与局部变量同名
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int f(int x) { int x = 1; return x; }\nint main() { return 0; }", a);
        check(!ok || !a.errors().empty(), "parameter shadow by local variable detected");
    }

    // 正确：不同作用域的同名变量
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main() { int x = 1; { int x = 2; } return 0; }", a);
        check(ok && a.errors().empty(), "shadowing in diff scope allowed");
    }
}

// ============================================================
// 类型检查
// ============================================================

static void test_type_checking() {
    printf("\n-- Type Checking --\n");

    // 正确：int 运算
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main() { int x = 1 + 2 * 3; return x; }", a);
        check(ok && a.errors().empty(), "integer arithmetic passes");
    }

    // 正确：关系运算用作条件
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main() { if (1 < 2) { return 1; } return 0; }", a);
        check(ok && a.errors().empty(), "relational as condition passes");
    }

    // 错误：void 表达式用于条件 (通过函数调用产生)
    // ToyC 不支持 void 函数调用作为条件直接测试，
    // 但语法上 void() 不能用于 expression context
}

// ============================================================
// return 路径检查
// ============================================================

static void test_return_check() {
    printf("\n-- Return Path --\n");

    // 正确：所有路径有 return
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main() { if (1) { return 1; } else { return 0; } }", a);
        check(ok && a.errors().empty(), "return on all paths passes");
    }

    // 正确：最后一个 stmt 是 return
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main() { int x = 1; return x; }", a);
        check(ok && a.errors().empty(), "trailing return passes");
    }

    // 错误：非 void 函数无返回值
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main() { int x = 1; }", a);
        check(!ok || !a.errors().empty(), "missing return in non-void detected");
    }

    // 正确：void 函数可以不 return
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "void f(int a) { int x__ = a; } int main() { return 0; }", a);
        check(ok && a.errors().empty(), "void function without return passes");
    }
}

// ============================================================
// const 检查
// ============================================================

static void test_const_check() {
    printf("\n-- Const Check --\n");

    // 正确：const 用字面量初始化
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "const int N = 42;\nint main() { return N; }", a);
        check(ok && a.errors().empty(), "const init with literal passes");
    }

    // 错误：const 用非常量表达式初始化
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int g = 1;\nconst int N = g;\nint main() { return 0; }", a);
        check(!ok || !a.errors().empty(), "const init with non-const detected");
    }
}

// ============================================================
// break / continue 检查
// ============================================================

static void test_break_continue() {
    printf("\n-- Break / Continue --\n");

    // 正确：循环内 break/continue
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main() { while (1) { break; continue; } return 0; }", a);
        check(ok && a.errors().empty(), "break/continue in loop passes");
    }

    // 错误：循环外 break
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main() { break; return 0; }", a);
        check(!ok || !a.errors().empty(), "break outside loop detected");
    }

    // 错误：循环外 continue
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main() { continue; return 0; }", a);
        check(!ok || !a.errors().empty(), "continue outside loop detected");
    }
}

// ============================================================
// 函数调用检查
// ============================================================

static void test_function_call() {
    printf("\n-- Function Call --\n");

    // 正确：调用已定义函数
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int add(int a, int b) { return a + b; }\nint main() { return add(1, 2); }", a);
        check(ok && a.errors().empty(), "function call with correct args passes");
    }

    // 错误：调用未声明函数
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int main() { return foo(1); }", a);
        check(!ok || !a.errors().empty(), "call to undeclared function detected");
    }

    // 错误：参数数量不匹配
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int add(int a, int b) { return a + b; }\nint main() { return add(1, 2, 3); }", a);
        check(!ok || !a.errors().empty(), "argument count mismatch detected");
    }

    // 错误：参数类型不匹配 (void 作为实参)
    {
        SemanticAnalyzer a;
        auto [ok, ast] = parseAndAnalyze(
            "int f(int a) { return a; }\nvoid g() { return; }\nint main() { return f(g()); }", a);
        check(!ok || !a.errors().empty(), "void arg to int param detected");
    }
}

// ============================================================
// 符号表 作用域测试
// ============================================================

static void test_symbol_table() {
    printf("\n-- Symbol Table Scoping --\n");

    {
        SymbolTable st;
        st.enterScope();  // global

        Symbol g;
        g.name = "x";
        g.kind = SymbolKind::Variable;
        g.isGlobalVar = true;
        st.insert(g);

        st.enterScope();  // function
        Symbol f;
        f.name = "y";
        f.kind = SymbolKind::Variable;
        st.insert(f);

        st.enterScope();  // block
        Symbol b;
        b.name = "z";
        b.kind = SymbolKind::Variable;
        st.insert(b);

        check(st.lookup("z") != nullptr, "finds symbol in current scope");
        check(st.lookup("y") != nullptr, "finds symbol in parent scope");
        check(st.lookup("x") != nullptr, "finds symbol in global scope");

        st.exitScope();
        check(st.lookup("z") == nullptr, "cannot find exited scope symbol");
        check(st.lookup("y") != nullptr, "still finds parent scope symbol after exit");

        st.exitScope();
        check(st.lookup("y") == nullptr, "cannot find exited function scope");

        check(st.lookupGlobalScope("x") != nullptr, "finds global scope");
        check(st.lookupGlobalScope("y") == nullptr, "local y not in global");
    }
}

int main() {
    printf("=== ToyC Semantic Analyzer Unit Tests ===\n\n");

    test_basic_declarations();
    test_redefinition();
    test_type_checking();
    test_return_check();
    test_const_check();
    test_break_continue();
    test_function_call();
    test_symbol_table();

    printf("\n==============================\n");
    printf("  %d / %d tests passed\n", pass_count, test_count);
    printf("==============================\n");

    return (pass_count == test_count) ? 0 : 1;
}
