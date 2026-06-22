# ToyC 符号表设计

## 概述

本文档定义 ToyC 编译器的符号表（Symbol Table）与服务域管理机制。符号表是 Semantic Analyzer 的核心数据结构，承载所有标识符的定义、类型、作用域信息，并支撑所有语义检查逻辑。

**上游文档**：`docs/任务要求.md`（语义约束）、`docs/design/ast_design.md`（AST 中标识符引用点）
**下游文档**：IR Generator（使用符号表确定变量位置、函数签名）

---

## 核心设计

### 1. 符号类型定义

#### 1.1 SymbolKind

```cpp
// include/toyc/semantic/symbol_table.h

namespace toyc {

enum class SymbolKind {
    Variable,    // int 变量
    Constant,    // const int 常量
    Parameter,   // 函数形参（int）
    Function     // 函数
};

} // namespace toyc
```

#### 1.2 Type（类型系统）

```cpp
enum class Type { INT, VOID };
```

ToyC 类型系统只有两个类型：`int` 和 `void`。所有变量、常量、形参都是 `INT` 类型。函数返回值是 `INT` 或 `VOID`。函数参数类型固定为 `INT`。

#### 1.3 Symbol 数据结构

```cpp
struct Symbol {
    std::string name;              // 标识符名
    SymbolKind kind;               // 符号类别
    SourceLocation declLoc;        // 声明位置（行号+列号）

    // 按 kind 区分的有值字段：
    // Variable:
    bool isGlobalVar = false;      // 是否全局变量
    int stackOffset = -1;          // 局部变量栈帧偏移（在 IR 生成阶段填充）
    // 注：全局变量通过 name 访问，不需要 stackOffset

    // Constant:
    bool isGlobalConst = false;    // 是否全局常量
    int constValue = 0;            // 编译期确定的值
    // 注：全局常量通过 name 访问，局部常量使用 constValue

    // Parameter:
    int paramIndex = -1;           // 形参序号（0-based，用于参数传递）

    // Function:
    Type returnType = Type::INT;           // 返回类型
    std::vector<Type> paramTypes;          // 形参类型列表（每个都是 INT）
    bool hasReturnOnAllPaths = false;       // 语义阶段验证：每条路径都有 return
};
```

### 2. 作用域设计

#### 2.1 作用域类型

ToyC 有三层嵌套作用域：

```
全局作用域（Global Scope）
  ├── 全局变量 VarDecl
  ├── 全局常量 ConstDecl
  └── 函数定义 FuncDef
        └── 函数作用域（Function Scope）
              ├── 形参 Param
              ├── 局部声明 VarDecl / ConstDecl
              └── Block 作用域（Block Scope）—— 可多层嵌套
                    └── 局部声明 VarDecl / ConstDecl
```

#### 2.2 作用域创建与销毁时机

| 作用域 | 创建时机 | 销毁时机 |
|---|---|---|
| 全局作用域 | 编译器 Semantic Analyzer 初始化 | 编译结束 |
| 函数作用域 | 进入 `FuncDef` 遍历 | 离开 `FuncDef` 遍历 |
| Block 作用域 | 进入 `BlockStmt` 遍历 | 离开 `BlockStmt` 遍历 |

#### 2.3 作用域数据表示

每个作用域是一个 `std::unordered_map<std::string, Symbol>`，作用域栈是一个 `std::vector`：

```
作用域栈 (scopeStack)：
┌──────────────────────────────────┐
│ Scope[0]: 全局作用域              │
│   "a" → Symbol{k=Constant, ...}  │
│   "main" → Symbol{k=Function, ...}│
├──────────────────────────────────┤
│ Scope[1]: 函数 main 的作用域      │
│   "x" → Symbol{k=Parameter, ...} │
│   "y" → Symbol{k=Variable, ...}  │
├──────────────────────────────────┤
│ Scope[2]: 某个 Block 作用域       │
│   "z" → Symbol{k=Variable, ...}  │
└──────────────────────────────────┘
```

### 3. SymbolTable 接口

```cpp
class SymbolTable {
public:
    // === 作用域管理 ===

    /// 进入新作用域（进入函数或 Block 时调用）
    void enterScope();

    /// 离开当前作用域（离开函数或 Block 时调用）
    void exitScope();

    /// 当前是否在全局作用域中
    bool isGlobalScope() const;

    // === 符号管理 ===

    /// 插入符号。在当前作用域存在同名符号则返回 false（重定义）
    bool insert(const Symbol& sym);

    /// 从内向外查找符号。找不到则返回 nullptr（未定义）
    Symbol* lookup(const std::string& name);

    /// 仅在当前作用域查找（用于重定义检查）
    Symbol* lookupCurrentScope(const std::string& name);

    /// 仅在全局作用域查找（用于函数调用检查）
    Symbol* lookupGlobalScope(const std::string& name);

    // === 上下文查询 ===

    /// 当前嵌套的循环深度（>0 表示在循环内）
    int loopDepth() const;

    /// 进入一个循环体
    void enterLoop();

    /// 离开一个循环体
    void leaveLoop();

    /// 当前所在的函数符号（无则为 nullptr，表示在全局作用域）
    Symbol* currentFunction() const;

    /// 设置当前所在的函数
    void setCurrentFunction(Symbol* func);

private:
    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
    int loopDepth_ = 0;
    Symbol* currentFunction_ = nullptr;
};
```

### 4. 语义检查与符号表的映射

所有语义检查项与符号表操作的对应关系：

#### 4.1 标识符检查

| 检查项 | 实现方式 | 触发时机 |
|---|---|---|
| **重定义检查** | `lookupCurrentScope(name) != nullptr` → 报错 | visit(DeclNode) 时在 insert 之前 |
| **未定义检查** | `lookup(name) == nullptr` → 报错 | visit(IdExpr) |
| **声明后使用** | 符号只能 insert 后才能 lookup 到（栈式作用域天然保证） | — |

#### 4.2 常量检查

| 检查项 | 实现方式 | 触发时机 |
|---|---|---|
| **const 不可修改** | AssignStmt 左值：`lookup(name) → kind == Constant` → 报错 | visit(AssignStmt) |
| **const 初始化编译期可确定** | 遍历 initExpr：数字字面量 + 已声明的 const 组合 → 可计算；含变量/函数调用 → 报错 | visit(ConstDecl) |
| **编译期常量求值** | 对合法的 const 初始化表达式进行 compile-time evaluation | visit(ConstDecl) |

#### 4.3 函数检查

| 检查项 | 实现方式 | 触发时机 |
|---|---|---|
| **函数存在检查** | `lookupGlobalScope(funcName)` 且 `kind == Function` | visit(CallExpr) |
| **参数数量检查** | `funcSymbol.paramTypes.size() == callExpr.args.size()` | visit(CallExpr) |
| **参数类型检查** | 每个实参表达式类型都是 INT（ToyC 中所有实参都是 INT） | visit(CallExpr) |
| **函数声明顺序** | 只在全局作用域中查找（`lookupGlobalScope`），天然保证只找到已声明的函数 | visit(CallExpr) |
| **int 函数每条路径 return** | 遍历函数体所有路径，验证 ReturnStmt 可达性。`hasReturnOnAllPaths` 标记 | visit(FuncDef) 结束时 |
| **void 函数 return 带值** | `currentFunction().returnType == VOID && returnStmt.value != nullptr` → 报错 | visit(ReturnStmt) |
| **void 函数调用作条件/右值** | CallExpr → funcSymbol.returnType == VOID 且处于 if/while 条件或赋值右值 → 报错 | visit(CallExpr) 上下文 |

#### 4.4 表达式检查

| 检查项 | 实现方式 | 触发时机 |
|---|---|---|
| **除数不能为零** | BinaryExpr(DIV/MOD) 且 right 为 NumberExpr(0) → 报错 | visit(BinaryExpr) |
| **短路求值逻辑正确性** | `&&` / `||` 的操作数都是 int 类型（自动满足，ToyC 中所有表达式都是 int） | visit(BinaryExpr) |

#### 4.5 控制流检查

| 检查项 | 实现方式 | 触发时机 |
|---|---|---|
| **break 仅在循环中** | `loopDepth() == 0` → 报错 | visit(BreakStmt) |
| **continue 仅在循环中** | `loopDepth() == 0` → 报错 | visit(ContinueStmt) |

#### 4.6 程序入口检查

| 检查项 | 实现方式 | 触发时机 |
|---|---|---|
| **main 函数存在** | `lookupGlobalScope("main")` 且 `kind == Function` → 存在 | 分析结束后 |
| **main 无参数** | `mainSymbol.paramTypes.empty()` | 分析结束后 |
| **main 返回 int** | `mainSymbol.returnType == INT` | 分析结束后 |

---

## 设计决策与理由

### 为什么使用 vector + unordered_map 的栈式作用域而非树式？

- ToyC 的作用域是严格的栈式（进入→离开），不存在跨作用域的复杂引用
- 栈式查找逻辑简单：从 vector 尾部向前遍历，找到即返回
- 树式作用域需要维护 parent 指针，复杂度更高但 ToyC 不需要

### 为什么 loopDepth 用计数器而非标志位？

- 循环可以嵌套，`while { while { break; } }` 中的 break 需要退出最内层循环
- 计数器方式天然支持嵌套

### 为什么 currentFunction 单独维护而非通过作用域查找？

- 符号表可以高效查找函数符号但不知道当前在哪个函数内
- 进入函数时设置 currentFunction，ReturnStmt/BreakStmt/ContinueStmt 检查时直接用
- 比每次遍历作用域栈查找最内层函数符号更高效

### 为什么 const 编译期求值单独处理？

- const 初始化表达式可能包含算术、关系、逻辑运算的复杂组合
- 需要在符号表建立后（能查找其他 const 的值）才能递归求值
- 是一个独立的 compile-time evaluator，而非简单的符号表操作

---

## 与模块的映射

| 组件 | 实现位置 | 关系 |
|---|---|---|
| `SymbolKind`, `Type`, `Symbol` | `include/toyc/semantic/symbol_table.h` | 头文件定义 |
| `SymbolTable` | `include/toyc/semantic/symbol_table.h` + `src/semantic/symbol_table.cpp` | 接口 + 实现 |
| Semantic Analyzer | `src/semantic/semantic_analyzer.cpp` | 使用 SymbolTable 做检查 |
| IR Generator | `src/ir/ir_builder.cpp` | 读取 SymbolTable 获取变量/函数信息 |
| Backend | `src/backend/code_generator.cpp` | 读取 SymbolTable 获取全局变量列表（用于 .data 段） |

---

## 示例

### 输入

```c
const int C = 100;
int g = 10;

int foo(int x) {
    int y = x + C;
    {
        int z = y * 2;
        return z;
    }
}

int main() {
    int a = foo(g);
    return a;
}
```

### 符号表演化过程

**初始状态** —— 全局作用域：

| 符号 | 类型 | 属性 |
|---|---|---|
| `C` | Constant | global, value=100 |
| `g` | Variable | global |
| `foo` | Function | returnType=INT, params=[INT] |
| `main` | Function | returnType=INT, params=[] |

**进入 `foo` 函数** —— 函数作用域推入栈顶：

| 符号 | 类型 | 属性 |
|---|---|---|
| `x` | Parameter | index=0 |

**声明 `int y = x + C`**：

| 符号 | 类型 | 属性 |
|---|---|---|
| `y` | Variable | local |

**进入 Block** —— Block 作用域推入栈顶：

| 符号 | 类型 | 属性 |
|---|---|---|
| `z` | Variable | local |

**离开 Block** —— Block 作用域弹出，`z` 不可见。

**离开 `foo`** —— 函数作用域弹出，`x`, `y` 不可见。

**进入 `main` 函数**：

| 符号 | 类型 | 属性 |
|---|---|---|
| `a` | Variable | local |

`lookup("foo")` 查找路径：main作用域(fail) → 全局作用域(hit) → 返回 foo 的 Symbol。

`lookup("g")` 查找路径：main作用域(fail) → 全局作用域(hit) → 返回 g 的 Symbol。