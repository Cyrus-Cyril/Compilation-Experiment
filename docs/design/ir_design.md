# ToyC IR 设计

## 概述

本文档定义 ToyC 编译器的中间表示（IR），采用三地址码（Three Address Code，TAC）形式。IR 是 Semantic Analyzer 的输出目标、Optimizer 的操作对象、Backend 的输入来源。

**上游文档**：`docs/design/ast_design.md`（AST → IR 的映射来源）
**下游文档**：`docs/design/architecture.md` 的 Backend 部分

---

## 核心设计

### 1. IR 操作数类型

IR 指令的操作数有 5 种类型：

| 类型 | 说明 | C++ 表示 | 示例 |
|---|---|---|---|
| `VirtualReg` | 无限虚拟寄存器，由 IR Builder 递增分配 | `uint32_t` | `r0, r1, r2` |
| `Immediate` | 整数立即数 | `int32_t` | `42, -1` |
| `Label` | 跳转标签 | `uint32_t`（标签序号） | `L0, L1` |
| `GlobalName` | 全局变量/常量名 | `std::string` | `"g_a"`, `"g_c"` |
| `FuncName` | 函数名 | `std::string` | `"main"`, `"add"` |

```cpp
// include/toyc/ir/ir.h

namespace toyc {

enum class OperandKind { VirtualReg, Immediate, Label, GlobalName, FuncName };

struct IROperand {
    OperandKind kind;
    // 使用 variant 存放不同类型的值
    std::variant<uint32_t, int32_t, std::string> value;

    // 便捷构造
    static IROperand reg(uint32_t id)   { return {OperandKind::VirtualReg, id}; }
    static IROperand imm(int32_t v)     { return {OperandKind::Immediate, v}; }
    static IROperand label(uint32_t id) { return {OperandKind::Label, id}; }
    static IROperand global(const std::string& n) { return {OperandKind::GlobalName, n}; }
    static IROperand func(const std::string& n)   { return {OperandKind::FuncName, n}; }
};

} // namespace toyc
```

### 2. IR 指令集

共 7 类 23 条指令。

#### 2.1 算术指令（6 条）

| 指令 | 格式 | 语义 | 操作数约束 |
|---|---|---|---|
| `ADD` | `dest, src1, src2` | `dest = src1 + src2` | dest=Reg, src1/2=Reg\|Imm |
| `SUB` | `dest, src1, src2` | `dest = src1 - src2` | dest=Reg, src1/2=Reg\|Imm |
| `MUL` | `dest, src1, src2` | `dest = src1 * src2` | dest=Reg, src1/2=Reg\|Imm |
| `DIV` | `dest, src1, src2` | `dest = src1 / src2` | dest=Reg, src1/2=Reg\|Imm |
| `MOD` | `dest, src1, src2` | `dest = src1 % src2` | dest=Reg, src1/2=Reg\|Imm |
| `NEG` | `dest, src` | `dest = -src` | dest=Reg, src=Reg\|Imm |

#### 2.2 比较指令（6 条）

| 指令 | 格式 | 语义 | 操作数约束 |
|---|---|---|---|
| `EQ` | `dest, src1, src2` | `dest = (src1 == src2) ? 1 : 0` | dest=Reg, src1/2=Reg\|Imm |
| `NE` | `dest, src1, src2` | `dest = (src1 != src2) ? 1 : 0` | dest=Reg, src1/2=Reg\|Imm |
| `LT` | `dest, src1, src2` | `dest = (src1 < src2) ? 1 : 0` | dest=Reg, src1/2=Reg\|Imm |
| `GT` | `dest, src1, src2` | `dest = (src1 > src2) ? 1 : 0` | dest=Reg, src1/2=Reg\|Imm |
| `LE` | `dest, src1, src2` | `dest = (src1 <= src2) ? 1 : 0` | dest=Reg, src1/2=Reg\|Imm |
| `GE` | `dest, src1, src2` | `dest = (src1 >= src2) ? 1 : 0` | dest=Reg, src1/2=Reg\|Imm |

#### 2.3 逻辑指令（3 条）

| 指令 | 格式 | 语义 | 说明 |
|---|---|---|---|
| `AND` | `dest, src1, src2` | `dest = src1 && src2` | 在短路求值展开后可能不再出现，保留用于常量折叠 |
| `OR` | `dest, src1, src2` | `dest = src1 \|\| src2` | 同上 |
| `NOT` | `dest, src` | `dest = !src` | dest=Reg, src=Reg\|Imm |

#### 2.4 访存指令（4 条）

| 指令 | 格式 | 语义 | 说明 |
|---|---|---|---|
| `LOAD_GLOBAL` | `dest, globalName` | 加载全局变量/常量的值 | dest=Reg |
| `STORE_GLOBAL` | `globalName, src` | 向全局变量存储值 | src=Reg\|Imm |
| `LOAD_LOCAL` | `dest, frameOffset` | 从当前栈帧加载局部变量 | dest=Reg |
| `STORE_LOCAL` | `frameOffset, src` | 向当前栈帧存储局部变量 | src=Reg\|Imm |

#### 2.5 跳转指令（3 条）

| 指令 | 格式 | 语义 |
|---|---|---|
| `JMP` | `label` | 无条件跳转到 label |
| `BEQ` | `src1, src2, label` | 若 src1 == src2 则跳转到 label |
| `BNE` | `src1, src2, label` | 若 src1 != src2 则跳转到 label |

#### 2.6 函数指令（3 条）

| 指令 | 格式 | 语义 |
|---|---|---|
| `PARAM` | `src` | 传递一个实参（CALL 之前使用） |
| `CALL` | `dest, funcName` | 调用函数，返回值存入 dest（void 时 dest=无效Reg） |
| `RET` | `src?` | 函数返回（可带返回值） |

#### 2.7 标签指令（1 条）

| 指令 | 格式 | 语义 |
|---|---|---|
| `LABEL` | `label` | 定义跳转目标标签 |

### 3. IR 数据结构

```cpp
enum class IROpcode {
    // 算术
    ADD, SUB, MUL, DIV, MOD, NEG,
    // 比较
    EQ, NE, LT, GT, LE, GE,
    // 逻辑
    AND, OR, NOT,
    // 访存
    LOAD_GLOBAL, STORE_GLOBAL,
    LOAD_LOCAL, STORE_LOCAL,
    // 跳转
    JMP, BEQ, BNE,
    // 函数
    PARAM, CALL, RET,
    // 标签
    LABEL
};

struct IRInstruction {
    IROpcode opcode;
    std::vector<IROperand> operands;
};

// 一个函数的 IR 片段
struct IRFunction {
    std::string name;
    std::vector<IRInstruction> instructions;
};

// 整个程序的 IR
struct IRProgram {
    std::vector<IRFunction> functions;
};
```

### 4. IR Builder 接口

```cpp
class IRBuilder {
public:
    IRProgram build(CompUnit* ast, SymbolTable* symTable);

private:
    uint32_t nextReg_ = 0;     // 虚拟寄存器计数器
    uint32_t nextLabel_ = 0;   // 标签计数器

    uint32_t newReg();          // 分配新虚拟寄存器
    uint32_t newLabel();        // 分配新标签

    // 各节点的 IR 生成方法
    uint32_t genExpr(ExprNode* expr);           // 返回存放结果的虚拟寄存器
    void genStmt(ASTNode* stmt);
    void genFunc(FuncDef* func);
    void genDecl(DeclNode* decl);
};
```

---

## 设计决策与理由

### 为什么使用虚拟寄存器而非直接分配物理寄存器？

- 虚拟寄存器是无限的，简化 IR 生成：不关心寄存器压力
- 将寄存器分配推迟到优化阶段或 Backend，可以更灵活地做分配
- 符合教科书三地址码的经典设计

### 为什么 AND/OR 保留指令但短路求值时展开？

- 短路求值 `&&` / `||` 在 IR 生成时直接展开为条件跳转（`BEQ`/`BNE` + `JMP`），不需要 `AND`/`OR` 指令
- 但保留 `AND`/`OR` 指令用于：优化器做常量折叠时使用（如 `1 && 0 → 0`）
- 正常 IR 生成流程中不生成 `AND`/`OR` 指令

### 为什么区分 LOAD_GLOBAL/STORE_GLOBAL 和 LOAD_LOCAL/STORE_LOCAL？

- 全局变量：通过符号名访问，Backend 需要生成 `la/lw/sw` 序列
- 局部变量：通过栈帧偏移访问，Backend 只需要 `lw/sw offset(sp)`
- 两类访存的寻址模式完全不同，分开有利于 Backend 处理

### 为什么 CALL 之前用 PARAM 指令传递参数？

- 使参数传递顺序在 IR 层面显式化
- PARAM 从右到左生成（或从左到右，取决于调用约定），Backend 据此分配 a0~a7
- 便于优化：可以消除冗余的 PARAM 指令

---

## 与模块的映射

| IR 指令 | AST 节点来源 | Backend RISC-V 映射 |
|---|---|---|
| ADD/SUB/MUL/DIV/MOD | BinaryExpr(+ - * / %) | `add/sub/mul/div/rem` |
| NEG | UnaryExpr(NEG) | `neg` 或 `sub rd, x0, rs` |
| EQ/NE/LT/GT/LE/GE | BinaryExpr(== != < > <= >=) | `slt/sltu/xor` 组合 |
| NOT | UnaryExpr(NOT) | `seqz` 或 `xori rd, rs, 1` |
| LOAD_GLOBAL | IdExpr(指向全局变量/常量) | `la t, name; lw rd, 0(t)` |
| STORE_GLOBAL | AssignStmt(目标为全局变量) | `la t, name; sw rs, 0(t)` |
| LOAD_LOCAL | IdExpr(指向局部变量/形参) | `lw rd, offset(sp)` |
| STORE_LOCAL | AssignStmt(目标为局部变量) | `sw rs, offset(sp)` |
| JMP | BreakStmt/ContinueStmt/短路展开 | `j label` |
| BEQ/BNE | IfStmt/WhileStmt/短路展开 | `beq/bne rs1, rs2, label` |
| PARAM | CallExpr 的实参 | `mv aN, rs` |
| CALL | CallExpr | `call func` 或 `jal ra, func` |
| RET | ReturnStmt | `mv a0, rs; ret` |

---

## 示例

### 输入

```c
int main() {
    int x = 10;
    int y = 3;
    if (x > y) {
        return x;
    } else {
        return y;
    }
}
```

### IR 输出

```
main:
    r0 = 10                    # 立即数 10 → r0
    STORE_LOCAL [x], r0        # x = r0
    r1 = 3                     # 立即数 3 → r1
    STORE_LOCAL [y], r1        # y = r1
    r2 = LOAD_LOCAL [x]        # 加载 x
    r3 = LOAD_LOCAL [y]        # 加载 y
    GT r4, r2, r3             # r4 = (x > y)
    BEQ r4, 0, L0             # 若 r4==0 跳转到 L0（else）
    r5 = LOAD_LOCAL [x]        # then: 加载 x
    RET r5                     # return x
    JMP L1                     # 跳过 else 分支
L0:
    r6 = LOAD_LOCAL [y]        # else: 加载 y
    RET r6                     # return y
L1:
    RET                        # void return（实际上不会到达）
```

### 短路求值展开示例

```c
// a && b  展开为：
r_t = a
BEQ r_t, 0, L_false
r_t = b
BEQ r_t, 0, L_false
r_result = 1
JMP L_end
L_false:
r_result = 0
L_end:

// a || b  展开为：
r_t = a
BNE r_t, 0, L_true
r_t = b
BNE r_t, 0, L_true
r_result = 0
JMP L_end
L_true:
r_result = 1
L_end:
```