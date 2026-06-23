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
