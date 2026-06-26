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
