#include <cstring>
#include <iostream>

int main(int argc, char* argv[]) {
    // 解析命令行参数
    bool optFlag = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-opt") == 0) {
            optFlag = true;
        }
    }

    // TODO Phase 3-8: Lexer → Parser → Semantic → IR → Optimize → Backend
    // 当前阶段：框架搭建，仅输出空汇编文件

    // RISC-V 最小合法汇编（空程序）
    std::cout << ".text" << std::endl;
    std::cout << ".globl main" << std::endl;
    std::cout << "main:" << std::endl;
    std::cout << "    li a0, 0" << std::endl;
    std::cout << "    ret" << std::endl;

    return 0;
}