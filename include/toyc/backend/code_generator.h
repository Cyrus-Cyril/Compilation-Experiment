#pragma once

#include "toyc/ir/ir.h"

namespace toyc {

/// RISC-V32 代码生成器
class CodeGenerator {
public:
    std::string generate(const IRProgram& program);
};

}  // namespace toyc