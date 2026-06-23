// ToyC Compiler Main Entry
//
// 编译管线：
//   stdin → Lexer(Flex) → Parser(Bison) → AST
//        → Semantic Analyzer (符号表 + 语义检查)
//        → IR Builder (三地址码)
//        → Optimizer (-opt 时)
//        → Backend (RISC-V32 汇编)
//        → stdout

#include <cstring>
#include <iostream>
#include <memory>

#include "toyc/lexer/token.h"
#include "toyc/parser/parser_api.h"
#include "toyc/semantic/semantic_analyzer.h"
#include "toyc/ir/ir_builder.h"
#include "toyc/ir/ir.h"
#include "toyc/optimize/optimizer.h"
#include "toyc/backend/code_generator.h"

int main(int argc, char* argv[]) {
    // 解析命令行参数
    bool optFlag = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-opt") == 0) {
            optFlag = true;
        }
    }

    // ===== 第一步：词法分析 + 语法分析 =====
    auto ast = toyc::parseStdin();
    if (!ast) {
        return 1;
    }

    // ===== 第二步：语义分析 =====
    toyc::SemanticAnalyzer analyzer;
    if (analyzer.analyze(ast.get())) {
        return 1;
    }

    // ===== 第三步：IR 生成 =====
    toyc::IRProgram irProgram = toyc::IRBuilder::build(ast.get(), &analyzer.symbolTable());

    // ===== 第四步：优化（-opt 时启用）=====
    if (optFlag) {
        toyc::Optimizer optimizer;
        irProgram = optimizer.optimize(irProgram);
    }

    // ===== 第五步：代码生成 =====
    toyc::CodeGenerator codeGen;
    std::string assembly = codeGen.generate(irProgram);

    std::cout << assembly;

    return 0;
}
