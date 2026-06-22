#pragma once

#include "toyc/ir/ir.h"

namespace toyc {

/// 优化器
class Optimizer {
public:
    IRProgram optimize(const IRProgram& input);
};

}  // namespace toyc