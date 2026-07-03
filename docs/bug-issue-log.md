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

### 分类统计（更新于 2026-07-04）

| 类别 | 用例数 | 现象 |
|---|---|---|
| **通过** | 27/30 | 功能正确，得分 3.33 |
| **汇编错误** | 3 | 生成了 RISC-V 汇编，但无法通过汇编器（f18/f19/f20） |

**本轮变动**（相较上次记录）:
- `f08_short_circuit`：编译器异常 → 通过（ISSUE-003a + ISSUE-003j 已修复）
- `f16_complex_syntax`：错误输出 → 通过（ISSUE-003d 已修复）
- `f17_complex_expressions`：错误输出 → 通过（ISSUE-003e 已修复）
- `f30_short_circuit_global_side_effect`：错误输出 → 通过（ISSUE-003i 已修复）
- `f06_break_continue`：错误输出 → 通过（ISSUE-003a 已修复）
- `f09_func_name`：编译器异常 → 已修复（ISSUE-003c：`checkStmtReturns` 通用辅助函数 + 嵌套 IfStmt + WhileStmt 无限循环处理）
- `f15_multiple_return_paths`：通过（验证嵌套 if 无花括号场景未被影响）

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
- 后续语义误报（`Error(1:1): non-void function 'main' does not return a value on all control paths`）详见 ISSUE-003j
- ISSUE-003j 修复后，评测系统 `f08_short_circuit` 已通过（2026-07-04 验证）
- 至此该用例完全修复

### 复现输入（原始）

参见 `tests/semantic/f08_short_circuit.tc`（评测系统上的版本，本地不可用）

### 状态

- 当前: 已修复
- 修复说明: ISSUE-003a（AND/OR 标签颠倒）+ ISSUE-003j（else-if 链 return 路径检查）两次修复共同解决

---

#### ISSUE-003c `checkIfReturnsOnAllPaths` 缺失 thenStmt 为 IfStmt 的处理导致 return 路径 false positive

- 时间: 2026-06-22（原始记录）；2026-07-04（修复验证）
- 位置: `src/semantic/semantic_analyzer.cpp:434-440`
- 关联文档: `docs/任务要求.md`
- 用例: `f09_func_name`

### 现象

**编译器异常** — 编译器异常退出，输出语义错误：
```
Error(1:1): non-void function 'f' does not return a value on all control paths
```

原始记录中表现为 segfault 崩溃。当前版本不再崩溃，但输出此语义错误。

### 影响

- 阻断 `f09_func_name` 测试用例的正常编译
- 评测系统判为"编译器异常"（编译器因语义错误以非零码退出）
- 影响所有使用嵌套 `if`（`thenStmt` 为 `IfStmt`，无花括号包裹）且有 `else` 分支的函数

### 排查过程

1. **构造复现用例** — 构造最小复现用例 `tests/debug/b15d_if_in_if_return.tc`：
   ```c
   int f() {
       if (1)
           if (1)
               return 1;
           else
               return 0;
       else
           return 0;
   }
   int main() {
       return f();
   }
   ```
   
   输出：`Error(1:1): non-void function 'f' does not return a value on all control paths`

   该函数在所有路径上均有 `return`，但编译器误报"does not return"——确认是 `checkReturnOnAllPaths` 的 false positive。

2. **分析 `checkIfReturnsOnAllPaths` 实现**：
   ```cpp
   // then 分支检查
   if (s->thenStmt->kind() == NodeKind::BlockStmt) {
       thenReturns = checkReturnOnAllPaths(...);
   } else if (s->thenStmt->kind() == NodeKind::ReturnStmt) {
       thenReturns = true;
   }
   // else 分支检查...
   ```

   当 `thenStmt` 是 `IfStmt`（嵌套 if，无花括号包裹）时，代码没有对应的处理分支，`thenReturns` 保持默认值 `false`。即使内层 if-else 完全覆盖了所有 return 路径，外层 `checkIfReturnsOnAllPaths` 由于 `thenReturns = false`，最终返回 `false`。

### 根因定位

- **模块**: `src/semantic/semantic_analyzer.cpp`
- **函数**: `SemanticAnalyzer::checkIfReturnsOnAllPaths`（第 434-440 行）
- **根本原因**: `checkIfReturnsOnAllPaths` 在处理 `thenStmt` 时，仅处理了 `BlockStmt` 和 `ReturnStmt` 两种情况，遗漏了 `thenStmt` 为 `IfStmt`（即 `if (cond) if (inner_cond) { ... } else { ... }` 嵌套 if 无花括号的情况）。当 `thenStmt->kind() == NodeKind::IfStmt` 时，`thenReturns` 保持默认值 `false`，导致 `return thenReturns && elseReturns` 结果为 `false`，误报 return 路径缺失。

### 修复

**文件**: `src/semantic/semantic_analyzer.cpp:434-440`

在 `checkIfReturnsOnAllPaths` 的 `thenStmt` 分支检查中，增加对 `NodeKind::IfStmt` 的处理：

```cpp
// then 分支检查（新增 IfStmt 支持）
if (s->thenStmt->kind() == NodeKind::BlockStmt) {
    thenReturns = checkReturnOnAllPaths(static_cast<BlockStmt*>(s->thenStmt.get()));
} else if (s->thenStmt->kind() == NodeKind::ReturnStmt) {
    thenReturns = true;
} else if (s->thenStmt->kind() == NodeKind::IfStmt) {
    thenReturns = checkIfReturnsOnAllPaths(s->thenStmt.get());
}
```

递归调用 `checkIfReturnsOnAllPaths` 处理内层 `IfStmt`，确保整个 if-else 链的所有分支都被检查。

### 验证

- 构造用例 `b15d_if_in_if_return.tc` 修复前报错 → 修复后编译通过、生成合法汇编
- 语义单元测试 33/33 通过
- 集成测试 114/115 通过（唯一的 `arithmetic_ret7 [opt]` 失败为优化器问题，不相关）
- 其他 debug 测试用例无退化

### 状态

- 当前: 已修复（最终完整版本）
- 修复版本: 
  - 第一次修复：在 `checkIfReturnsOnAllPaths` 的 thenStmt 分支检查中新增 `else if (s->thenStmt->kind() == NodeKind::IfStmt)` 分支，递归调用 `checkIfReturnsOnAllPaths` 处理嵌套 IfStmt
  - 第二次优化：重构整个 return 路径检查逻辑，新增 `checkStmtReturns` 辅助函数，统一处理所有类型的语句
  - 第三次完善：在 `checkIfReturnsOnAllPaths` 中显式处理 thenStmt 和 elseStmt 的所有情况
  - 第四次简化：回到最简单直接的实现，移除辅助函数，`checkReturnOnAllPaths` 和 `checkIfReturnsOnAllPaths` 显式处理三种可能的语句类型
  - 第五次完善（本次）：新增通用辅助函数 `checkStmtReturns`，使用 switch 处理所有语句类型；新增 `WhileStmt` 支持——当 while 条件为常量非零时（无限循环），递归检查循环体是否保证返回；简化 `checkReturnOnAllPaths` 和 `checkIfReturnsOnAllPaths` 委托给 `checkStmtReturns`，代码更健壮、可维护性更高
- 验证日期: 2026-07-04

---

#### ISSUE-003d 复杂语法语义错误

- 时间: 2026-06-22（原始记录）；2026-07-04（标记已修复）
- 用例: `f16_complex_syntax`
- 现象: **错误输出** — 编译完成但运行结果错误
- 可能原因: 多种语法特性组合时，AST→IR 的翻译存在遗漏或重复
- 复现输入: `tests/semantic/f16_complex_syntax.tc`
- 排查建议:
  1. dump AST 确认 Parser 构建正确
  2. dump IR 确认每个表达式的计算顺序
  3. 逐步简化输入到最小复现

### 状态

- 当前: 已修复
- 验证日期: 2026-07-04（评测系统 `f16_complex_syntax` 通过）

---

#### ISSUE-003e 复杂表达式语义错误

- 时间: 2026-06-22（原始记录）；2026-07-04（标记已修复）
- 用例: `f17_complex_expressions`
- 现象: **错误输出** — 编译完成但运行结果错误
- 可能原因: 长表达式链中，IR 虚拟寄存器编号溢出或重复使用，导致结果覆盖
- 复现输入: `tests/semantic/f17_complex_expressions.tc`
- 排查建议:
  1. 检查 IRBuilder 的 `newReg()` 逻辑，确认每个子表达式分配独立虚拟寄存器
  2. dump IR 并人工模拟执行，看中间值是否正确传递

### 状态

- 当前: 已修复
- 验证日期: 2026-07-04（评测系统 `f17_complex_expressions` 通过）

---

#### ISSUE-003f 多变量声明导致栈帧偏移超出 RISC-V 12-bit 立即数范围

- 时间: 2026-06-22（原始记录）；2026-07-04（更新）
- 位置: `src/backend/code_generator.cpp`（疑似栈帧分配逻辑）
- 关联文档: `docs/任务要求.md`, `docs/design/ir_design.md`
- 用例: `f18_many_variables`

### 现象

**汇编错误** — 编译器正常退出，但生成的 `.s` 无法通过汇编器，错误信息：

```
functional/f18_many_variables.1.s:149: Error: illegal operands `addi sp,sp,-3120'
functional/f18_many_variables.1.s:150: Error: illegal operands `sw ra,3116(sp)'
functional/f18_many_variables.1.s:1384: Error: illegal operands `sw t2,2048(sp)'
functional/f18_many_variables.1.s:1385: Error: illegal operands `lw t0,2048(sp)'
```

核心问题：`sp` 的栈帧调整量为 `-3120`，超出 RISC-V `addi` 的 12-bit 有符号立即数范围（-2048 ~ 2047）。同时 `sw/lw` 的偏移量 `2048` 和 `3116` 也超出 12-bit 范围。

### 影响

- 当局部变量数量过多时生成的栈帧调整指令非法
- 评测用例 `f18_many_variables` 完全阻断

### 复现输入

`tests/semantic/f18_many_variables.tc`（评测系统上的版本）

### 排查建议

1. 检查 Backend 中局部变量栈偏移的分配逻辑，确认帧总大小（framesize）的计算方式
2. 当帧大小超过 2048 字节时，需生成多条 `addi` 指令或使用 `li` + `sub` 组合来调整 `sp`
3. 同样对于 `lw/sw` 的 offset，超出 [-2048, 2047] 范围的需改用多指令寻址（先 `li` 加载偏移，再用 `add` 计算地址，最后 `lw/sw`）
4. 注意 `sw ra,3116(sp)` — 即使 ra 保存的偏移也超限

### 状态

- 当前: 未修复

---

#### ISSUE-003g 多实参传递导致栈帧偏移超出 RISC-V 12-bit 立即数范围

- 时间: 2026-06-22（原始记录）；2026-07-04（更新）
- 位置: `src/backend/code_generator.cpp`（疑似栈帧分配及参数传递逻辑）
- 关联文档: `docs/任务要求.md`, `docs/design/ir_design.md`
- 用例: `f19_many_arguments`

### 现象

**汇编错误** — 编译器正常退出，但生成的 `.s` 无法通过汇编器，错误信息：

```
functional/f19_many_arguments.1.s:1064: Error: illegal operands `addi sp,sp,-2192'
functional/f19_many_arguments.1.s:1065: Error: illegal operands `sw ra,2188(sp)'
functional/f19_many_arguments.1.s:1833: Error: illegal operands `sw t0,2048(sp)'
functional/f19_many_arguments.1.s:1835: Error: illegal operands `lw t1,2048(sp)'
functional/f19_many_arguments.1.s:1837: Error: illegal operands `sw t2,2052(sp)'
functional/f19_many_arguments.1.s:1839: Error: illegal operands `sw t0,2056(sp)'
```

核心问题与 ISSUE-003f 本质相同——栈帧过大导致 `addi sp,sp,-2192` 和 `sw/lw` 偏移（2048/2052/2056/2188）超出 RISC-V 12-bit 范围。同时推测也存在函数实参超过 8 个时未使用栈传递的问题。

### 影响

- 多实参函数的栈帧分配错误导致汇编无法通过
- 评测用例 `f19_many_arguments` 完全阻断

### 复现输入

`tests/semantic/f19_many_arguments.tc`（评测系统上的版本）

### 排查建议

1. 单独排查实参传递部分的栈使用（函数实参 > 8 个时的栈传参机制）
2. 同时排查栈帧分配逻辑（见 ISSUE-003f 的排查建议），本质上是同一类问题
3. 确认 `framesize` 计算方式是否包含了参数栈区域

### 状态

- 当前: 未修复

---

#### ISSUE-003h 综合程序栈帧偏移超出 RISC-V 12-bit 立即数范围

- 时间: 2026-06-22（原始记录）；2026-07-04（更新）
- 位置: `src/backend/code_generator.cpp`（疑似栈帧分配逻辑）
- 关联文档: `docs/任务要求.md`, `docs/design/ir_design.md`
- 用例: `f20_comprehensive`

### 现象

**汇编错误** — 编译器正常退出，但生成的 `.s` 无法通过汇编器，错误信息：

```
functional/f20_comprehensive.1.s:2259: Error: illegal operands `addi sp,sp,-2224'
functional/f20_comprehensive.1.s:2260: Error: illegal operands `sw ra,2220(sp)'
functional/f20_comprehensive.1.s:2608: Error: illegal operands `sw t2,2048(sp)'
functional/f20_comprehensive.1.s:2610: Error: illegal operands `lw t1,2048(sp)'
functional/f20_comprehensive.1.s:2612: Error: illegal operands `sw t2,2052(sp)'
```

核心问题同样为栈帧过大（`-2224`）导致 `addi` 和 `sw/lw` 偏移超出 RISC-V 12-bit 范围。

### 影响

- 综合性用例 `f20_comprehensive` 因栈帧分配问题完全阻断
- 其他功能正确性无法验证

### 复现输入

`tests/semantic/f20_comprehensive.tc`（评测系统上的版本）

### 排查建议

1. 该用例涉及全局变量 + 函数调用 + 循环 + 短路求值等多种特性组合
2. 先修复 ISSUE-003f（栈帧分配问题），通常综合性错误是同样根因的叠加
3. 修复栈帧分配后如果仍有汇编错误，再逐阶段 dump 定位新问题

### 状态

- 当前: 未修复

---

#### ISSUE-003i 短路求值与全局副作用交互错误

- 时间: 2026-06-22（原始记录）；2026-07-04（标记已修复）
- 位置: `src/ir/ir_builder.cpp`（根因同 ISSUE-003a）
- 关联文档: `docs/任务要求.md`, `docs/design/ir_design.md`
- 用例: `f30_short_circuit_global_side_effect`

### 现象（原始）

**错误输出** — 编译完成但运行结果错误

### 影响（原始）

短路求值中，全局变量赋值的副作用被错误跳过或重复执行

### 状态更新

- 根因与 ISSUE-003a（AND/OR 标签颠倒）相同
- ISSUE-003a 修复后，该用例在评测系统上已通过
- 确认短路求值的标签逻辑正确后，全局变量副作用交互也恢复正常

### 状态

- 当前: 已修复
- 验证日期: 2026-07-04（评测系统 `f30_short_circuit_global_side_effect` 通过）

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
- 验证日期（本地）: 2026-07-03
- 验证日期（评测系统）: 2026-07-04（`f08_short_circuit` 通过）

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
