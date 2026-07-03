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
| **编译器异常** | 2 | 编译器内部崩溃（segfault / uncaught exception） |
| **汇编错误** | 3 | 生成了 RISC-V 汇编，但无法通过汇编器 |

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

- 用例: `f08_short_circuit`
- 现象: **编译器异常** — 编译器内部崩溃（segfault / exception），未完成编译
- 可能原因: 短路求值展开为条件跳转时，IR 生成了对空指针或无效标签的引用
- 复现输入: `tests/semantic/f08_short_circuit.tc`
- 排查建议:
  1. 用 `-fsanitize=address` 编译编译器后重新运行，定位崩溃点
  2. 检查 `&&` / `||` 短路展开的 IR 生成代码，确认标签分配和条件跳转逻辑
  3. 确认 `IdExpr::symbol` 指针在短路求值路径上被正确回填

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
