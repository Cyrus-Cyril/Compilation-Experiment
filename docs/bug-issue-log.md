# Bug / Issue Log

## ISSUE-001 Lexer 中负号与减号冲突

- 时间: 2026-06-23
- 位置: `src/lexer/lexer.l`
- 关联文档: `docs/任务要求.md`, `docs/design/architecture.md`

### 现象

旧版词法规则把 `-5` 直接识别为单个 `NUMBER`，这会让 `a-1` 被切成 `ID NUMBER(-1)`，而不是 `ID MINUS NUMBER(1)`。

### 影响

- 二元减法表达式无法被 Parser 正确归约。
- 赋值、返回值、函数实参中的减法都会受影响。
- 与文法里已经存在的一元负号规则重复，造成前后端接口语义不一致。

### 复现输入

```c
int main() {
    int a = 2;
    return a-1;
}
```

### 排查过程

1. 检查表达式文法，确认 `UnaryExpr -> "-" UnaryExpr` 已经存在。
2. 回查 Lexer，发现 `NUMBER` 正则写成 `-?(0|[1-9][0-9]*)`。
3. 结合 Flex 最长匹配规则，确认 `-1` 会优先整体吃进 `NUMBER`，吞掉本该独立存在的 `MINUS`。

### 本次处理

- 将 `NUMBER` 调整为仅识别非负整数。
- 负数统一通过 Parser 的一元负号规则构造 AST。
- 新增词法测试，验证 `a-1` 会被切分成 `ID MINUS NUMBER(1)`。

### 建议同步到 GitHub Issue

- 标题: `Lexer NUMBER rule conflicts with unary/binary minus parsing`
- 标签: `bug`, `frontend`, `parser`
- Issue 描述重点: 说明“需求示例里的负数 token 化方式”和“表达式文法里的 unary minus”存在冲突，建议以独立 `MINUS` token 方案为准。

---

## ISSUE-003 评测用例分类 Bug 汇总

- 时间: 2026-06-22
- 位置: 全局（涉及 Lexer / Parser / Semantic / IR / Backend）
- 关联文档: `docs/任务要求.md`, `docs/design/`

### 分类统计

| 类别 | 用例数 | 现象 |
|---|---|---|
| **通过** | 22/30 | 功能正确，得分 3.33 |
| **错误输出** | 4 | 编译器静默完成，生成代码执行结果错误 |
| **编译器异常** | 1 | 编译器内部崩溃（segfault / uncaught exception） |
| **汇编错误** | 3 | 生成了 RISC-V 汇编，但无法通过汇编器 |
| **语义错误** | 1 | 编译器报语义错误退出（ISSUE-003j，`checkReturnOnAllPaths` 误报 else-if 链） |

**注**: ISSUE-003b 的原始编译器异常已被 ISSUE-003a 修复，相应用例目前表现为语义误报（详见 ISSUE-003j）|

### 详细 Issue

---

#### ISSUE-003a 短路求值 AND/OR 标签颠倒导致循环/分支条件判断错误

- 时间: 2026-07-03
- 位置: `src/ir/ir_builder.cpp:281-291`
- 关联文档: `docs/任务要求.md`, `docs/design/ir_design.md`
- 用例: `f06_break_continue`
- 现象: **错误输出** — 编译器完成编译，生成汇编运行结果与预期不符

### 现象

当 while 循环条件或 while 内部的 if 条件使用 `&&` 或 `||` 运算符时，条件判断的语义与 ToyC 语言规范不符。具体表现为：

- `A && B` 的行为等价于 `A || B`（AND 和 OR 的短路标签被颠倒）
- `A || B` 的行为等价于 `A`（OR 无法正确处理左操作数为真的短路情况）

这导致 break/continue 语句在错误的时机执行或跳过，最终运行结果与预期不符。

例如：`while (i < 10 && sum < 20)` 实际等价于 `while (i < 10 || sum < 20)`，当 i<10 为真时就进入循环体（跳过 sum<20 的检查），而 i<10 为假时反而检查 sum<20——完全颠倒了 AND 的语义。

### 影响

- 所有使用 `&&` / `||` 的 while 循环条件判断错误
- while 内部 if 语句使用 `&&` / `||` 时分支判断错误
- break / continue 在条件分支中的触发时机错误
- 短路求值语义完全颠倒，影响所有涉及逻辑运算符的控制流

### 复现输入

**测试用例 1（OR 短路错误）：** `tests/debug/b09_while_with_or_break.tc`

```c
int main() {
    int i = 0;
    int flag = 1;
    while (i < 3 || flag == 0) {
        i = i + 1;
        if (i == 2) {
            break;
        }
    }
    return i;
}
/* 预期: i=0 -> 条件 true -> body: i=1, i=2(break) -> 返回 2
   实际: i<3 为真 -> 跳转到 midTrue -> 检查 flag==0 为假 -> 跳到 endLabel -> 返回 0 */
```

**测试用例 2（AND 短路错误）：** `tests/debug/b08_while_with_and_break.tc`

```c
int main() {
    int i = 0;
    int sum = 0;
    while (i < 10 && sum < 20) {
        i = i + 1;
        sum = sum + i;
        if (i == 5) {
            break;
        }
    }
    return sum;
}
/* 预期: i递增至5时 break, sum = 1+2+3+4+5 = 15
   实际: AND 被当作 OR 处理，但此例中副作用恰好不影响结果(仍需独立验证) */
```

### 排查过程

1. **检查 break/continue 的 IR 跳转目标** — 通过 IR dump 分析，`genBreakStmt` 正确生成 `JMP endLabel`，`genContinueStmt` 正确生成 `JMP condLabel`。测试 b01-b07（简单条件）的 IR 语义完全正确。

2. **检查 nested Block + while 场景** — 测试 b05（Block 内 break）验证通过。`loopStack_` 的 push/pop 在嵌套 while 场景下正确管理。

3. **检查 SymbolTable 的 loopDepth 管理** — `enterLoop()`/`leaveLoop()` 在 `visitWhileStmt` 中正确调用，语义检查未被绕过。

4. **引入 &&/|| 条件的测试** — 测试 b08（AND+break）和 b09（OR+break）的 IR dump 揭示了条件跳转逻辑异常：

   b09 `while (i < 3 || flag == 0)` 的 IR 片段：
   ```
   LABEL L0                  ; condLabel
   LOAD_LOCAL r2 #0          ; i
   LT r4 r2 #3               ; i < 3
   BNE r4 #0 L3              ; true -> L3 (midTrue) <-- 应为 L1(bodyLabel)
   JMP L2                    ; false -> L2 (endLabel) <-- 应为 L3(midTrue)
   LABEL L3                  ; midTrue
   LOAD_LOCAL r5 #4          ; flag
   EQ r7 r5 #0               ; flag == 0
   BNE r7 #0 L1              ; true -> body
   JMP L2                    ; false -> end <-- 左为真时不应再检查右操作数
   ```

   可见：左操作数为真时被导向 midTrue 而非直接进入 body；左操作数为假时被导向 endLabel 而非 midTrue。

5. **精确定位到 genCondBinary 函数** — [src/ir/ir_builder.cpp](file:///e:/wyf/Compilation-Experiment/src/ir/ir_builder.cpp#L281-L291)

### 根因定位

- **模块**: `src/ir/ir_builder.cpp`
- **函数**: `IRBuilderImpl::genCondBinary`（第 281-291 行）
- **根本原因**: AND 和 OR 的短路求值标签参数被颠倒使用。

当前代码：
```cpp
void IRBuilderImpl::genCondBinary(BinaryExpr* expr,
    uint32_t trueLabel, uint32_t falseLabel) {
    if (expr->op == BinaryOp::AND) {
        uint32_t midFalse = newLabel();
        genCondExpr(expr->left.get(), trueLabel, midFalse);   // 错误
        emitLabel(midFalse);
        genCondExpr(expr->right.get(), trueLabel, falseLabel);
    } else if (expr->op == BinaryOp::OR) {
        uint32_t midTrue = newLabel();
        genCondExpr(expr->left.get(), midTrue, falseLabel);   // 错误
        emitLabel(midTrue);
        genCondExpr(expr->right.get(), trueLabel, falseLabel);
    }
}
```

AND 的正确语义：`A false -> falseLabel（短路）；A true -> 检查 B`
当前代码中 AND 使用的 `genCondExpr(left, trueLabel, midFalse)` 将 A=true 导向 trueLabel（跳过了 B），A=false 导向 midFalse（仍要检查 B）——恰好颠倒了 AND 的短路方向。

OR 的正确语义：`A true -> trueLabel（短路）；A false -> 检查 B`
当前代码中 OR 使用的 `genCondExpr(left, midTrue, falseLabel)` 将 A=true 导向 midTrue（仍要检查 B），A=false 导向 falseLabel（短路为假）——恰好与 AND 的短路模式混淆。

正确实现应为：
```cpp
// AND: A false -> falseLabel; A true -> 检查 B
genCondExpr(left, midCheck, falseLabel);
emitLabel(midCheck);
genCondExpr(right, trueLabel, falseLabel);

// OR: A true -> trueLabel; A false -> 检查 B
genCondExpr(left, trueLabel, midCheck);
emitLabel(midCheck);
genCondExpr(right, trueLabel, falseLabel);
```

### 修复方法

**文件**: [src/ir/ir_builder.cpp](file:///e:/wyf/Compilation-Experiment/src/ir/ir_builder.cpp#L281-L291)

将 `genCondBinary` 函数中 AND 和 OR 分支的 `genCondExpr` 调用参数互换：

```cpp
// 修复前 (AND)：
genCondExpr(expr->left.get(), trueLabel, midFalse);   // 错误：A=true→trueLabel
// 修复后 (AND)：
genCondExpr(expr->left.get(), midCheck, falseLabel);   // 正确：A=true→midCheck

// 修复前 (OR)：
genCondExpr(expr->left.get(), midTrue, falseLabel);    // 错误：A=true→midTrue
// 修复后 (OR)：
genCondExpr(expr->left.get(), trueLabel, midCheck);    // 正确：A=true→trueLabel
```

同时将两分支的中间标签变量统一命名为 `midCheck`，逻辑对称清晰。

**验证**: 用 `tests/debug/b08_while_with_and_break.tc`（AND）和 `tests/debug/b09_while_with_or_break.tc`（OR）验证生成的 RISC-V 汇编，确认 AND/OR 短路跳转语义正确。

**交叉引用**: 此修复同时解决 ISSUE-003b（短路求值崩溃）和 ISSUE-003i（短路+全局副作用）的根因。

### 状态

- 当前: 已修复
- 修复时间: 2026-07-03

---

#### ISSUE-003b 短路求值导致编译器异常

- 时间: 2026-06-22（原始记录）；2026-07-03（更新）
- 位置: `src/ir/ir_builder.cpp`（原崩溃根因，已由 ISSUE-003a 修复）
- 关联文档: `docs/任务要求.md`, `docs/design/ir_design.md`
- 用例: `f08_short_circuit`

### 现象（原始）

**编译器异常** — 编译器内部崩溃（segfault / exception），未完成编译

### 影响（原始）

短路求值表达式导致编译器崩溃，阻断编译流程

### 状态更新

- 原始崩溃问题已被 ISSUE-003a（AND/OR 标签颠倒修复）解决
- 评测系统上该用例现在表现为 **语义错误**: `Error(1:1): non-void function 'main' does not return a value on all control paths`
- 该语义误报的根因详见 **ISSUE-003j**（`checkReturnOnAllPaths` 不处理 else-if 链）

### 复现输入（原始）

参见 `tests/semantic/f08_short_circuit.tc`（评测系统上的版本，本地不可用）

### 状态

- 当前: 已修复（原始编译器异常不再存在）

---

#### ISSUE-003c 函数名相关编译器异常

- 用例: `f09_func_name`
- 现象: **编译器异常** — 编译器内部崩溃
- 可能原因: 涉及函数名作为表达式的场景（函数指针不存在，但函数名单独出现可能导致 SymbolTable lookup 异常）
- 复现输入: `tests/semantic/f09_func_name.tc`
- 排查建议:
  1. 确认 `IdExpr` 在 Parser 构建时，是否可能把函数名当作变量引用处理
  2. 检查 Semantic 中 `IdExpr` 的 visit 逻辑，函数名出现在非调用上下文的处理
  3. 确认 `Symbol* symbol` 指针是否因为类型转换失败导致空指针

---

#### ISSUE-003d 复杂语法语义错误

- 用例: `f16_complex_syntax`
- 现象: **错误输出** — 编译完成但运行结果错误
- 可能原因: 多种语法特性组合时，AST→IR 的翻译存在遗漏或重复
- 复现输入: `tests/semantic/f16_complex_syntax.tc`
- 排查建议:
  1. dump AST 确认 Parser 构建正确
  2. dump IR 确认每个表达式的计算顺序
  3. 逐步简化输入到最小复现

---

#### ISSUE-003e 复杂表达式语义错误

- 用例: `f17_complex_expressions`
- 现象: **错误输出** — 编译完成但运行结果错误
- 可能原因: 长表达式链中，IR 虚拟寄存器编号溢出或重复使用，导致结果覆盖
- 复现输入: `tests/semantic/f17_complex_expressions.tc`
- 排查建议:
  1. 检查 IRBuilder 的 `newReg()` 逻辑，确认每个子表达式分配独立虚拟寄存器
  2. dump IR 并人工模拟执行，看中间值是否正确传递

---

#### ISSUE-003f 多变量声明 IR 偏移/访存错误

- 用例: `f18_many_variables`
- 现象: **汇编错误** — 生成的 `.s` 不能被汇编器正常汇编
- 可能原因: 局部变量的栈帧偏移计算错误，导致 `lw/sw` 的 offset 超出 RISC-V 12-bit 立即数范围，或偏移地址重叠
- 复现输入: `tests/semantic/f18_many_variables.tc`
- 排查建议:
  1. 检查 Backend 中局部变量栈偏移的分配逻辑
  2. 确认每个 `STORE_LOCAL` / `LOAD_LOCAL` 指令的 offset 是否在 `[-2048, 2047]` 范围内
  3. 若超出范围，需要生成 `addi sp, sp, -framesize` + 多指令寻址

---

#### ISSUE-003g 多实参传递 IR/Backend 错误

- 用例: `f19_many_arguments`
- 现象: **汇编错误** — 生成的 `.s` 不能被汇编器正常汇编
- 可能原因: 函数实参数量超过 a0~a7（8个寄存器），超出部分未实现栈传递
- 复现输入: `tests/semantic/f19_many_arguments.tc`
- 排查建议:
  1. 确认 Backend 在超过 8 个实参时的处理逻辑
  2. 检查 IR 侧 `PARAM` 指令是否按序生成，Backend 是否只映射了 a0~a7
  3. 如果当前只实现了寄存器传参，需要补充栈传参支持

---

#### ISSUE-003h 综合程序多模块联动错误

- 用例: `f20_comprehensive`
- 现象: **汇编错误** — 生成的 `.s` 不能被汇编器正常汇编
- 可能原因: 多种特性组合（全局变量 + 函数调用 + 循环 + 短路求值）导致的 IR 或汇编错误累积
- 复现输入: `tests/semantic/f20_comprehensive.tc`
- 排查建议:
  1. 先修复上述单项 Issue（003f 多变量、003g 多参数），通常综合性用例的错误是单项 bug 的叠加
  2. 逐阶段 dump（AST → IR → Assembly），定位第一次出现错误的阶段

---

#### ISSUE-003i 短路求值与全局副作用交互错误

- 用例: `f30_short_circuit_global_side_effect`
- 现象: **错误输出** — 编译完成但运行结果错误
- 可能原因: 短路求值中，全局变量赋值的副作用被错误跳过或重复执行
- 复现输入: `tests/semantic/f30_short_circuit_global_side_effect.tc`
- 排查建议:
  1. 确认 `&&` / `||` 短路展开时，全局变量 `STORE_GLOBAL` 指令是否被正确放置在跳转的合适分支中
  2. dump IR 并检查全局变量的 `LOAD_GLOBAL` / `STORE_GLOBAL` 指令顺序
  3. 与 f08 短路求值的基础版本比对，看加入全局变量后出了什么问题

#### ISSUE-003j `checkReturnOnAllPaths` 不处理 else-if（IfStmt 作为 elseStmt）导致 return 路径误报

- 时间: 2026-07-03
- 位置: `src/semantic/semantic_analyzer.cpp:390-428`
- 关联文档: `docs/任务要求.md`（"int 函数必须在每一条可能的执行路径上通过 return 语句返回一个 int 类型的值"）
- 用例: `f08_short_circuit`（评测系统用例，ISSUE-003b 的原始崩溃修复后暴露的新问题）

### 现象

对于使用了 else-if 链的 `int` 函数，即使所有控制路径都包含 `return` 语句，语义分析器也会误报：
```
Error(1:1): non-void function 'main' does not return a value on all control paths
```

### 影响

- 所有含 else-if 链结构的 `int` 类型函数被错误阻断编译
- 评测用例 `f08_short_circuit` 因此失败（原始崩溃已由 ISSUE-003a 修复，但修复后暴露出此语义误报）
- 此 Bug 与短路求值本身无关，但与含有短路求值条件的 else-if 链测试用例恰好重叠

### 复现输入

最小复现（`tests/debug/b15c_minimal_else_if.tc`）：

```c
int main() {
    if (1) {
        return 1;
    } else if (1) {
        return 2;
    } else {
        return 0;
    }
}
```

### 排查过程

1. **确认错误现象**：`f08_short_circuit` 评测用例在 ISSUE-003a 修复后不再崩溃，但报语义错误 `Error(1:1): non-void function 'main' does not return a value on all control paths`

2. **检查语义分析器**：定位到 `checkReturnOnAllPaths` 函数是执行 return 路径检查的唯一入口

3. **构造边界测试**：
   - 简单 if-else（有 return）→ 通过 ✅
   - 简单 if-else（无 return）→ 正确报错 ✅
   - else-if 链（所有路径都有 return）→ 误报 ❌
   - else-if 链（不含短路求值）→ 同样误报 ❌（确认与短路求值无关）

4. **分析 `checkReturnOnAllPaths` 逻辑**：函数只检查函数体 `BlockStmt` 中最后一个语句。如果最后一个语句是 `IfStmt` 且有 `elseStmt`，则对 then/else 分别检查：
   - `thenStmt` 是 `BlockStmt` → 递归检查
   - `thenStmt` 是 `ReturnStmt` → 标记 true
   - `elseStmt` 是 `BlockStmt` → 递归检查
   - `elseStmt` 是 `ReturnStmt` → 标记 true
   - **缺失**: `elseStmt` 是 `IfStmt`（即 else-if 的情况）未做任何处理，`elseReturns` 保持默认 `false`

5. **验证 AST 结构**：Parser 将 `else if (cond) { ... }` 解析为外层 `IfStmt` 的 `elseStmt` 指向一个内层 `IfStmt` 节点。`IfStmt` 的 `elseStmt` 类型是 `std::unique_ptr<ASTNode>`，其 `kind()` 为 `NodeKind::IfStmt`。

### 根因定位

- **模块**: `src/semantic/semantic_analyzer.cpp`
- **函数**: `SemanticAnalyzer::checkReturnOnAllPaths`（第 390-428 行）
- **根本原因**: `checkReturnOnAllPaths` 在处理最后一个语句为 `IfStmt` 且有 `elseStmt` 时，仅处理了 `elseStmt` 为 `BlockStmt` 或 `ReturnStmt` 两种情况，遗漏了 `elseStmt` 为 `IfStmt`（else-if 链）的情况。当 `elseStmt->kind()` 是 `NodeKind::IfStmt` 时，`elseReturns` 保持默认值 `false`，导致 `thenReturns && elseReturns` 结果为 `false`，误报 return 路径缺失。

### 修复建议（方向性）

**文件**: `src/semantic/semantic_analyzer.cpp:390-428`

在 `checkReturnOnAllPaths` 的 `IfStmt` 分支中，为 `elseStmt` 增加对 `NodeKind::IfStmt` 的处理：

1. 当 `elseStmt->kind() == NodeKind::IfStmt` 时，应递归调用一个能处理 `IfStmt` 的辅助函数
2. 具体来说，可以提取 `IfStmt` 的 else-if 检查逻辑到递归函数 `checkIfReturnsOnAllPaths(IfStmt*)`，该函数检查 if-else 链中每个分支的 return 情况
3. 对于 `else if (cond) { return ... } else { return ... }` 结构，需要递归遍历整个 else-if 链
4. 注意处理边界情况：else-if 链中的 if 可能没有 else（此时返回 false 是正确的）

同时建议扩展 `checkReturnOnAllPaths` 对更多语句类型的支持（如 `WhileStmt` 中 break 是否影响 return 路径等）。

### 状态

- 当前: 已修复
- 修复版本: 在 `SemanticAnalyzer` 中新增 `checkIfReturnsOnAllPaths(IfStmt*)` 辅助方法，递归处理 if-else 链的 return 路径检查。`checkReturnOnAllPaths` 的 `IfStmt` 分支委托给该方法。当 `elseStmt` 为 `IfStmt`（else-if）时，`checkIfReturnsOnAllPaths` 递归调用自身，确保整个 else-if 链的所有分支都被检查。同时对 then/else 中 `ReturnStmt`、`BlockStmt`、`IfStmt` 三种情况均做处理。
- 验证日期: 2026-07-03

---

## ISSUE-002 本机构建工具链不满足当前项目要求

- 时间: 2026-06-26
- 位置: `CMakeLists.txt`, `src/parser/parser.y`
- 关联文档: `BUILD.md`, `docs/任务要求.md`

### 现象

当前本机环境缺少 `cmake` 命令，无法直接执行 `cmake -S . -B build`。同时系统自带 `bison` 版本为 2.3，无法识别当前 `parser.y` 中使用的 `%code` 指令。

### 影响

- 不能在本机通过 CMake 完整验证 `toyc` 和各单元测试目标。
- Flex 可以生成 lexer，但 Bison 生成 parser 会失败，导致前端测试无法在当前环境闭环。
- 后续阶段实现前需要在可用工具链环境中复核构建基线。

### 复现命令

```bash
command -v cmake
bison --version
bison -d -o /tmp/parser.tab.cpp src/parser/parser.y
```

### 排查结果

- `command -v cmake` 无输出。
- `bison --version` 显示 `GNU Bison 2.3`。
- `bison -d ... src/parser/parser.y` 报错：`invalid directive: %code`。

### 本次处理

- 先记录为环境类 Issue，不修改前端实现。
- 后续在安装新版 CMake/Bison 后重新执行完整构建与测试。

### 建议同步到 GitHub Issue

- 标题: `Local toolchain cannot build current Flex/Bison pipeline`
- 标签: `bug`, `build`, `toolchain`
- Issue 描述重点: 说明本机缺少 CMake 且 Bison 2.3 过旧；建议团队统一使用新版 CMake 和 Bison，或在 CI 中固定工具链版本。
