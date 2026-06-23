#include "toyc/parser/ast.h"
#include "toyc/parser/parser_api.h"

#include <cstdio>

using namespace toyc;

static int testCount = 0;
static int passCount = 0;

static void check(bool condition, const char* message) {
    ++testCount;
    if (condition) {
        ++passCount;
        std::printf("  PASS: %s\n", message);
    } else {
        std::printf("  FAIL: %s\n", message);
    }
}

static const FuncDef* findFunction(const CompUnit& unit, const char* name) {
    for (const auto& element : unit.elements) {
        if (element->kind() != NodeKind::FuncDef) {
            continue;
        }
        const auto* func = static_cast<const FuncDef*>(element.get());
        if (func->name == name) {
            return func;
        }
    }
    return nullptr;
}

int main() {
    std::printf("=== ToyC Parser Unit Tests ===\n\n");

    {
        auto ast = parseString(
            "const int g = 7;\n"
            "int main() { return g; }\n");
        check(ast != nullptr, "parse global const + main");
        check(ast && ast->elements.size() == 2, "compilation unit contains 2 top-level elements");
        check(ast && ast->elements[0]->kind() == NodeKind::ConstDecl, "first element is const declaration");
        check(ast && findFunction(*ast, "main") != nullptr, "main function exists");
    }

    {
        auto ast = parseString(
            "int main() {\n"
            "  return 1 + 2 * 3 - 4;\n"
            "}\n");
        check(ast != nullptr, "parse arithmetic precedence sample");
        const auto* mainFunc = ast ? findFunction(*ast, "main") : nullptr;
        check(mainFunc != nullptr, "find main in arithmetic sample");
        const auto* retStmt =
            (mainFunc && !mainFunc->body->stmts.empty())
                ? static_cast<const ReturnStmt*>(mainFunc->body->stmts[0].get())
                : nullptr;
        check(retStmt && retStmt->kind() == NodeKind::ReturnStmt, "statement is return");
        const auto* subExpr = retStmt ? static_cast<const BinaryExpr*>(retStmt->value.get()) : nullptr;
        check(subExpr && subExpr->op == BinaryOp::SUB, "top-level expression is subtraction");
        const auto* addExpr = subExpr ? static_cast<const BinaryExpr*>(subExpr->left.get()) : nullptr;
        check(addExpr && addExpr->op == BinaryOp::ADD, "left side keeps addition before subtraction");
        const auto* mulExpr = addExpr ? static_cast<const BinaryExpr*>(addExpr->right.get()) : nullptr;
        check(mulExpr && mulExpr->op == BinaryOp::MUL, "multiplication binds tighter than addition");
    }

    {
        auto ast = parseString(
            "int foo(int x, int y) { return x + y; }\n"
            "int main() {\n"
            "  int a = foo(1, 2);\n"
            "  while (a > 0) {\n"
            "    if (a == 1) break;\n"
            "    a = a - 1;\n"
            "    continue;\n"
            "  }\n"
            "  return a;\n"
            "}\n");
        check(ast != nullptr, "parse function call + control flow sample");
        const auto* mainFunc = ast ? findFunction(*ast, "main") : nullptr;
        check(mainFunc && mainFunc->body->stmts.size() == 3, "main body contains decl, while, return");
        const auto* declStmt = mainFunc ? static_cast<const DeclStmt*>(mainFunc->body->stmts[0].get()) : nullptr;
        check(declStmt && declStmt->decl->kind() == NodeKind::VarDecl, "first statement wraps local declaration");
        const auto* varDecl = declStmt ? static_cast<const VarDecl*>(declStmt->decl.get()) : nullptr;
        check(varDecl && varDecl->initExpr->kind() == NodeKind::CallExpr, "local declaration initializes from call");
        const auto* whileStmt = mainFunc ? static_cast<const WhileStmt*>(mainFunc->body->stmts[1].get()) : nullptr;
        check(whileStmt && whileStmt->body->kind() == NodeKind::BlockStmt, "while body is a block");
    }

    std::printf("\n==============================\n");
    std::printf("  %d / %d tests passed\n", passCount, testCount);
    std::printf("==============================\n");

    return (passCount == testCount) ? 0 : 1;
}
