#include "toyc/backend/code_generator.h"
#include "toyc/ir/ir_builder.h"
#include "toyc/optimize/optimizer.h"
#include "toyc/parser/parser_api.h"
#include "toyc/semantic/semantic_analyzer.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace toyc;

namespace fs = std::filesystem;

#ifndef TOYC_SOURCE_DIR
#define TOYC_SOURCE_DIR "."
#endif

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const std::string& message) {
    ++test_count;
    if (condition) {
        ++pass_count;
        std::printf("  PASS: %s\n", message.c_str());
    } else {
        std::printf("  FAIL: %s\n", message.c_str());
    }
}

struct CompileResult {
    bool ok = false;
    std::string stage;
    std::string assembly;
};

static std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

static int countAsmToken(const std::string& text, const std::string& token) {
    int count = 0;
    size_t pos = 0;
    const std::string needle = token + " ";
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

static CompileResult compileSource(const std::string& source, bool optimize) {
    CompileResult result;

    auto ast = parseString(source);
    if (!ast) {
        result.stage = "parse";
        return result;
    }

    SemanticAnalyzer analyzer;
    if (analyzer.analyze(ast.get())) {
        result.stage = "semantic";
        return result;
    }

    IRProgram ir = IRBuilder::build(ast.get(), &analyzer.symbolTable());
    if (optimize) {
        ir = Optimizer{}.optimize(ir);
    }

    result.assembly = CodeGenerator{}.generate(ir);
    result.ok = !result.assembly.empty();
    result.stage = result.ok ? "ok" : "backend";
    return result;
}

static void checkBaseAssembly(const std::string& asmText, const std::string& caseName, bool optimize) {
    const std::string prefix = caseName + (optimize ? " [opt]" : " [base]");
    check(contains(asmText, ".text"), prefix + " emits .text");
    check(contains(asmText, ".globl main"), prefix + " exports main");
    check(contains(asmText, "main:"), prefix + " emits main label");
    check(contains(asmText, "ret"), prefix + " emits ret");
}

static void checkCaseSpecificAssembly(const std::string& caseName, const std::string& asmText, bool optimize) {
    const std::string prefix = caseName + (optimize ? " [opt]" : " [base]");

    if (caseName == "arithmetic_ret7") {
        if (optimize) {
            check(!contains(asmText, "mul "), prefix + " folds constant arithmetic");
        } else {
            check(contains(asmText, "mul "), prefix + " keeps arithmetic multiply path");
        }
    } else if (caseName == "function_call_ret9") {
        check(contains(asmText, "call add"), prefix + " emits call to add");
    } else if (caseName == "globals_ret42") {
        check(contains(asmText, ".data"), prefix + " emits data section for globals");
        check(contains(asmText, "g:"), prefix + " emits global label g");
    } else if (caseName == "if_else_ret11") {
        check(contains(asmText, ".Lmain_"), prefix + " emits branch labels");
    } else if (caseName == "locals_ret9") {
        check(contains(asmText, "sw t0, 0(sp)") || contains(asmText, "sw t0, 4(sp)") ||
              contains(asmText, "sw t0, 8(sp)") || contains(asmText, "s1") ||
              contains(asmText, "s2"), prefix + " materializes local storage");
    } else if (caseName == "recursion_ret120") {
        check(contains(asmText, "call fact"), prefix + " emits recursive call");
    } else if (caseName == "return_const_ret42") {
        check(contains(asmText, "li a0, 42") || contains(asmText, "li t1, 42") ||
              contains(asmText, "li t0, 42"), prefix + " materializes constant 42");
    } else if (caseName == "short_circuit_ret1") {
        check(contains(asmText, "bne ") || contains(asmText, "beq "), prefix + " emits conditional branch");
        check(contains(asmText, "j .Lmain_"), prefix + " emits short-circuit jump");
    } else if (caseName == "while_break_continue_ret25") {
        check(contains(asmText, ".Lmain_"), prefix + " emits loop labels");
        check(contains(asmText, "j .Lmain_"), prefix + " emits loop jumps");
    } else if (caseName == "loop_heavy_ret100") {
        check(contains(asmText, ".Lmain_"), prefix + " emits hot-loop labels");
        check(contains(asmText, "j .Lmain_"), prefix + " emits hot-loop back edge");
        check(contains(asmText, "s1") || contains(asmText, "s2"), prefix + " can use register cache");
        if (optimize) {
            int memoryLike = countAsmToken(asmText, "lw") + countAsmToken(asmText, "sw") +
                             countAsmToken(asmText, "mv") + countAsmToken(asmText, "j");
            check(memoryLike <= 12, prefix + " keeps optimized loop storage compact");
        }
    }
}

static void testIntegrationCases() {
    std::printf("=== ToyC Integration Tests ===\n\n");

    const fs::path root = fs::path(TOYC_SOURCE_DIR) / "tests" / "integration";
    std::vector<fs::path> cases;

    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".tc") {
            cases.push_back(entry.path());
        }
    }

    std::sort(cases.begin(), cases.end());
    check(!cases.empty(), "integration cases discovered");

    for (const auto& path : cases) {
        const std::string caseName = path.stem().string();
        const std::string source = readFile(path);

        CompileResult baseResult = compileSource(source, false);
        check(baseResult.ok, caseName + " compiles without optimization");
        if (baseResult.ok) {
            checkBaseAssembly(baseResult.assembly, caseName, false);
            checkCaseSpecificAssembly(caseName, baseResult.assembly, false);
        }

        CompileResult optResult = compileSource(source, true);
        check(optResult.ok, caseName + " compiles with optimization");
        if (optResult.ok) {
            checkBaseAssembly(optResult.assembly, caseName, true);
            checkCaseSpecificAssembly(caseName, optResult.assembly, true);
        }
    }

    std::printf("\n==============================\n");
    std::printf("  %d / %d tests passed\n", pass_count, test_count);
    std::printf("==============================\n");
}

int main() {
    testIntegrationCases();
    return (pass_count == test_count) ? 0 : 1;
}
