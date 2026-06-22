# ToyC AST 设计

## 概述

本文档定义 ToyC 编译器的抽象语法树（AST）节点体系，覆盖 `docs/任务要求.md` 中全部 ToyC 文法产生式。AST 是 Parser（Bison）的输出，是 Semantic Analyzer 的输入，同时也是 IR Generator 的遍历起点。

**上游文档**：`docs/任务要求.md`（文法定义）
**下游文档**：`docs/design/ir_design.md`、`docs/design/symbol_table.md`

---

## 核心设计

### 1. AST 节点基类

```cpp
// include/toyc/parser/ast.h

namespace toyc {

enum class NodeKind {
    CompUnit,
    VarDecl, ConstDecl,
    FuncDef, Param,
    BlockStmt, IfStmt, WhileStmt, ReturnStmt,
    BreakStmt, ContinueStmt, ExprStmt, AssignStmt,
    NullStmt, DeclStmt,
    BinaryExpr, UnaryExpr, CallExpr,
    IdExpr, NumberExpr
};

struct SourceLocation {
    int line;
    int column;
};

class ASTNode {
public:
    SourceLocation loc;

    explicit ASTNode(SourceLocation loc) : loc(loc) {}
    virtual ~ASTNode() = default;
    virtual NodeKind kind() const = 0;
};

} // namespace toyc
```

### 2. 节点类层次总览

```
ASTNode
├── CompUnit                   （编译单元，根节点）
│
├── DeclNode（声明基类）
│   ├── VarDecl                （变量声明）
│   └── ConstDecl              （常量声明）
│
├── FuncDef                    （函数定义）
├── Param                      （形参）
│
├── StmtNode（语句基类）
│   ├── BlockStmt              （语句块）
│   ├── IfStmt                 （if-else）
│   ├── WhileStmt              （while 循环）
│   ├── ReturnStmt             （return 语句）
│   ├── BreakStmt              （break 语句）
│   ├── ContinueStmt           （continue 语句）
│   ├── ExprStmt               （表达式语句）
│   ├── AssignStmt             （赋值语句）
│   ├── NullStmt               （空语句）
│   └── DeclStmt               （语句块内的局部声明）
│
└── ExprNode（表达式基类）
    ├── BinaryExpr             （二元运算）
    ├── UnaryExpr              （一元运算）
    ├── CallExpr               （函数调用）
    ├── IdExpr                 （标识符引用）
    └── NumberExpr             （数字字面量）
```

### 3. 节点详细定义

#### 3.1 CompUnit（编译单元）

```cpp
// 文法：CompUnit → (Decl | FuncDef)+
class CompUnit : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> elements;
    // elements 中包含 VarDecl*, ConstDecl*, FuncDef*
};
```

#### 3.2 VarDecl（变量声明）

```cpp
// 文法：VarDecl → "int" ID "=" Expr ";"
class VarDecl : public ASTNode {
public:
    std::string name;                       // 变量名
    std::unique_ptr<ExprNode> initExpr;     // 初始化表达式
    bool isGlobal;                          // 语义阶段填充：是否全局
};
```

#### 3.3 ConstDecl（常量声明）

```cpp
// 文法：ConstDecl → "const" "int" ID "=" Expr ";"
class ConstDecl : public ASTNode {
public:
    std::string name;                       // 常量名
    std::unique_ptr<ExprNode> initExpr;     // 初始化表达式（语义阶段验证编译期可确定）
    bool isGlobal;                          // 语义阶段填充
};
```

#### 3.4 FuncDef（函数定义）

```cpp
// 文法：FuncDef → ("int" | "void") ID "(" (Param ("," Param)*)? ")" Block
class FuncDef : public ASTNode {
public:
    std::string name;                       // 函数名
    Type returnType;                        // INT 或 VOID
    std::vector<std::unique_ptr<Param>> params;   // 形参列表
    std::unique_ptr<BlockStmt> body;        // 函数体
};
```

其中 Type 枚举：

```cpp
enum class Type { INT, VOID };
```

#### 3.5 Param（形参）

```cpp
// 文法：Param → "int" ID
class Param : public ASTNode {
public:
    std::string name;                       // 形参名
    // 类型固定为 INT，无需额外字段
};
```

#### 3.6 BlockStmt（语句块）

```cpp
// 文法：Block → "{" Stmt* "}"
class BlockStmt : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> stmts;
    // stmts 中包含各种 StmtNode* 和 DeclNode*
};
```

#### 3.7 IfStmt（条件分支）

```cpp
// 文法："if" "(" Expr ")" Stmt ("else" Stmt)?
class IfStmt : public ASTNode {
public:
    std::unique_ptr<ExprNode> condition;    // 条件表达式
    std::unique_ptr<ASTNode> thenStmt;      // then 分支
    std::unique_ptr<ASTNode> elseStmt;      // else 分支（可为 nullptr）
};
```

#### 3.8 WhileStmt（循环）

```cpp
// 文法："while" "(" Expr ")" Stmt
class WhileStmt : public ASTNode {
public:
    std::unique_ptr<ExprNode> condition;    // 循环条件
    std::unique_ptr<ASTNode> body;          // 循环体
};
```

#### 3.9 ReturnStmt（返回语句）

```cpp
// 文法："return" Expr? ";"
class ReturnStmt : public ASTNode {
public:
    std::unique_ptr<ExprNode> value;        // 返回值表达式（void 函数时为 nullptr）
};
```

#### 3.10 BreakStmt / ContinueStmt

```cpp
// 文法："break" ";"
class BreakStmt : public ASTNode {};

// 文法："continue" ";"
class ContinueStmt : public ASTNode {};
```

#### 3.11 ExprStmt（表达式语句）

```cpp
// 文法：Expr ";"
class ExprStmt : public ASTNode {
public:
    std::unique_ptr<ExprNode> expr;
};
```

#### 3.12 AssignStmt（赋值语句）

```cpp
// 文法：ID "=" Expr ";"
class AssignStmt : public ASTNode {
public:
    std::string name;                       // 被赋值的变量名
    std::unique_ptr<ExprNode> value;        // 右值表达式
};
```

#### 3.13 NullStmt（空语句）

```cpp
// 文法：";"
class NullStmt : public ASTNode {};
```

#### 3.14 DeclStmt（语句块内的局部声明）

```cpp
// 文法：Decl（在语句块内出现的 VarDecl 或 ConstDecl）
// 直接复用 VarDecl / ConstDecl 节点类型，通过包装节点区分位置
class DeclStmt : public ASTNode {
public:
    std::unique_ptr<ASTNode> decl;          // VarDecl* 或 ConstDecl*
};
```

#### 3.15 BinaryExpr（二元运算表达式）

```cpp
// 文法：涵盖 + - * / % < > <= >= == != && ||
class BinaryExpr : public ASTNode {
public:
    BinaryOp op;                            // 运算符枚举
    std::unique_ptr<ExprNode> left;         // 左操作数
    std::unique_ptr<ExprNode> right;        // 右操作数
};
```

```cpp
enum class BinaryOp {
    ADD, SUB, MUL, DIV, MOD,             // 算术
    LT, GT, LE, GE, EQ, NE,              // 关系
    AND, OR                              // 逻辑
};
```

#### 3.16 UnaryExpr（一元运算表达式）

```cpp
// 文法：("+" | "-" | "!") UnaryExpr
class UnaryExpr : public ASTNode {
public:
    UnaryOp op;                             // 运算符枚举
    std::unique_ptr<ExprNode> operand;      // 操作数
};
```

```cpp
enum class UnaryOp { POS, NEG, NOT };
```

#### 3.17 CallExpr（函数调用表达式）

```cpp
// 文法：ID "(" (Expr ("," Expr)*)? ")"
class CallExpr : public ASTNode {
public:
    std::string funcName;                   // 被调函数名
    std::vector<std::unique_ptr<ExprNode>> args;  // 实参列表
};
```

#### 3.18 IdExpr（标识符引用表达式）

```cpp
// 文法：ID
class IdExpr : public ASTNode {
public:
    std::string name;                       // 标识符名
    // 语义阶段填充：
    Symbol* symbol;                         // 指向符号表条目
};
```

#### 3.19 NumberExpr（数字字面量表达式）

```cpp
// 文法：NUMBER
class NumberExpr : public ASTNode {
public:
    int value;                              // 数值
};
```

---

## 设计决策与理由

### 为什么使用 unique_ptr 管理子节点所有权？

- 编译器内 AST 是树状结构，有唯一所有者，无需共享所有权
- `unique_ptr` 零开销，语义清晰
- 避免内存泄漏和悬空指针

### 为什么 ExprStmt 和 AssignStmt 分开而不是统一用"表达式语句"？

- AssignStmt 的语义检查与 ExprStmt 不同：需要检查左值是否可修改（不能是 const）
- `ID "=" Expr ";"` 在文法是 Stmt 的直接产生式，不是 Expr

### 为什么 DeclStmt 包装 VarDecl/ConstDecl 而不是直接在 Stmt 中引用？

- 便于区分全局声明和局部声明（后者在语义分析时需建立 Block 级作用域）
- 保持 Stmt 列表的类型一致性（所有元素都是语句节点）

### 为什么 BinaryExpr 用一个节点覆盖所有二元运算符？

- 所有二元运算的 AST 结构相同（op + left + right）
- 运算符区分通过 `BinaryOp` 枚举值完成
- 减少节点类型数量，简化 Visitor 接口

### 为什么 IdExpr 预留 symbol 指针？

- Parser 阶段无法解析标识符引用关系（不知道符号是否已声明）
- Semantic 阶段通过符号表查找后回填 symbol 指针
- 后续 IR 生成可直接使用，无需再次查表

---

## 与模块的映射

| AST 节点 | Parser（创建） | Semantic（标注） | IR Generator（使用） |
|---|---|---|---|
| CompUnit | Bison 根规则创建 | 遍历顶层元素 | 遍历生成函数 IR |
| VarDecl | Bison Decl 规则 | 插入符号表 | 生成全局/局部变量 IR |
| ConstDecl | Bison Decl 规则 | 插入符号表+常量求值 | 生成全局/局部常量 IR |
| FuncDef | Bison FuncDef 规则 | 插入符号表+检查 return | 生成函数体 IR |
| BlockStmt | Bison Block 规则 | 进入新作用域 | 继续遍历子语句 |
| IfStmt | Bison if 规则 | 检查条件类型 | 生成条件跳转 IR |
| WhileStmt | Bison while 规则 | 标记循环上下文 | 生成循环 IR |
| ReturnStmt | Bison return 规则 | 检查返回类型 | 生成 RET IR |
| BreakStmt | Bison break 规则 | 检查是否在循环中 | 生成跳转 IR |
| ContinueStmt | Bison continue 规则 | 检查是否在循环中 | 生成跳转 IR |
| AssignStmt | Bison assign 规则 | 检查左值是否可修改 | 生成 STORE IR |
| BinaryExpr | Bison 各表达式规则 | 类型检查 | 生成算术/比较/逻辑 IR |
| UnaryExpr | Bison Unary 规则 | 类型检查 | 生成一元运算 IR |
| CallExpr | Bison PrimaryExpr 规则 | 函数存在/参数检查 | 生成 CALL IR |
| IdExpr | Bison PrimaryExpr 规则 | 符号表查找+回填 symbol | 生成 LOAD IR |
| NumberExpr | Bison PrimaryExpr 规则 | 无 | 直接使用 value |

---

## 示例

### 输入

```c
int add(int x, int y) {
    return x + y;
}
int main() {
    int a = add(1, 2);
    return a;
}
```

### AST 结构

```
CompUnit
└── FuncDef(name="add", returnType=INT)
    ├── Param(name="x")
    ├── Param(name="y")
    └── BlockStmt
        └── ReturnStmt
            └── BinaryExpr(ADD)
                ├── IdExpr(name="x")
                └── IdExpr(name="y")
└── FuncDef(name="main", returnType=INT)
    └── BlockStmt
        ├── VarDecl(name="a")
        │   └── CallExpr(funcName="add")
        │       ├── NumberExpr(1)
        │       └── NumberExpr(2)
        └── ReturnStmt
            └── IdExpr(name="a")
```