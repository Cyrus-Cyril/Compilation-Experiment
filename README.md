# ToyC 编译器开发流程清单

> 构建方法在 `BUILD.md` 中。

## 项目目标

实现一个 ToyC 编译器，将 ToyC 源程序编译为 RISC-V32 汇编代码。

```text
ToyC Source
      ↓
Lexer
      ↓
Parser
      ↓
AST
      ↓
Semantic Analysis
      ↓
IR Generation
      ↓
Optimization
      ↓
RISC-V Code Generation
      ↓
Assembly(.s)
```

---

# 第一阶段：需求分析

## 目标

理解 ToyC 语言规范和评测要求。

## 任务

* [x] 阅读《任务要求.md》
* [x] 整理 ToyC 文法
* [x] 整理语义约束
* [x] 整理评测规则
* [x] 整理接口约定（stdin/stdout）
* [x] 整理评分公式
* [x] 确定项目架构

## 输出

* [x] 需求分析文档 → `docs/design/architecture.md`
* [x] 项目总体设计文档 → `docs/design/architecture.md` + `ast_design.md` + `ir_design.md` + `symbol_table.md`

---

# 第二阶段：项目框架搭建

## 目标

建立统一开发环境和目录结构。

## 任务

* [x] 创建 GitHub 仓库
* [x] 配置 CMake → `CMakeLists.txt`
* [x] 配置代码规范 → `.clang-format`
* [x] 建立基础目录结构
* [ ] 配置 CI（可选）

## 项目结构

```text
ToyCCompiler/
│
├── CMakeLists.txt
├── README.md
├── .clang-format
│
├── docs/
│   ├── 任务要求.md
│   ├── design/
│   └── prompt/
│
├── include/toyc/
│   ├── lexer/       → token.h
│   ├── parser/      → ast.h
│   ├── semantic/    → symbol_table.h
│   ├── ir/          → ir.h
│   ├── optimize/    → optimizer.h
│   └── backend/     → code_generator.h
│
├── src/
│   ├── lexer/       → lexer.l
│   ├── parser/      → parser.y
│   ├── semantic/    → symbol_table.cpp, semantic_analyzer.cpp
│   ├── ir/          → ir_builder.cpp
│   ├── optimize/    → optimizer.cpp
│   └── backend/     → code_generator.cpp
│
├── tests/
│   ├── lexer/      → test_lexer.cpp
│   ├── parser/     → test_parser.cpp
│   ├── semantic/   → test_semantic.cpp
│   ├── ir/         → test_ir.cpp
│   └── backend/
│
└── main.cpp
```

## 输出

* [x] 可成功编译运行的空项目
      产生具体内容为：
```
.text
.globl main
main:
      li a0, 0
      ret ra      
```
* [x] 基础架构文档 → `docs/design/*.md`

---

# 技术选型

## 编程语言

C++（C++20 标准）

## 构建系统

CMake（最低 3.16，评测环境使用 4.0.3）

## 第三方库：Flex + Bison

本项目使用 **Flex（词法分析器生成器）+ Bison（语法分析器生成器）** 组合构建编译器前端。

### 工作流程

```
ToyC 源文件 (.tc)
      │
      ▼
  Flex ──► lex.yy.c ──► yylex() 词法分析，输出 Token 流
      │
      ▼
  Bison ──► parser.tab.c ──► yyparse() 语法分析，构建 AST
```

Flex 读取 `.l` 规则文件，生成词法分析器 C 代码；Bison 读取 `.y` 文法文件，生成语法分析器 C 代码。两者通过 `yylex()` → `yyparse()` 的调用链协作：Bison 在解析过程中按需调用 Flex 提供的 `yylex()` 获取下一个 Token。

### 优缺点

| 维度 | 说明 |
|---|---|
| **优点** | ① 轻量级，无额外运行时依赖，CMake 构建简单；② Flex 基于 DFA，Bison 基于 LALR(1)，执行效率高；③ 历史悠久，文档和社区资源丰富；④ 评测环境原生支持，无需额外安装 |
| **缺点** | ① 需要手动处理 LALR(1) 的文法歧义和左递归；② `.l`/`.y` 语法较古老，IDE 支持有限；③ 生成的错误信息不够直观，调试需要一定经验；④ 不支持 Unicode 标识符（ToyC 不需要） |

### 使用任务清单

* [x] 编写 `lexer.l` 文件（正则规则 → Token 定义）→ 占位骨架已创建，完整实现在第三阶段
* [x] 编写 `parser.y` 文件（ToyC 文法 → 语义动作 → AST 构建）→ 已完成基础 AST 构建与表达式优先级处理
* [x] CMake 集成 Flex/Bison（`find_package` 或 `add_custom_command`）
* [x] 定义 Token 语义值结构和类型声明（`%type`，使用自定义语义值结构替代 `%union`）
* [x] 处理 Flex → Bison 的 Token 传递（`yylval`）
* [x] 处理运算符优先级与结合性（`%left`/`%right`/`%nonassoc`）
* [x] 处理文法冲突（移进/归约警告 → 调整文法）
* [ ] 错误恢复策略（`error` 产生式）
* [ ] 源位置追踪（行号/列号，用于报错）

---

# 第三阶段：词法分析器（Lexer）

## 目标

将源代码转换为 Token 流。

## 输入

```c
int a = 1;
```

## 输出

```text
INT
ID(a)
ASSIGN
NUMBER(1)
SEMICOLON
```

## 任务

### Token 类型设计

* [x] TokenType 枚举（含 `tokenTypeName()`）
* [x] Token 结构体（含 `LexerValue` 传递 yylval）

### 关键字识别

* [x] int
* [x] void
* [x] const
* [x] if
* [x] else
* [x] while
* [x] break
* [x] continue
* [x] return

### 标识符识别

* [x] ID（`[_A-Za-z][_A-Za-z0-9]*`，通过 `yylval.strVal` 传递）

### 数字识别

* [x] NUMBER（`0|[1-9][0-9]*`，拒绝前导零，通过 `yylval.intVal` 传递；负号由 Parser 的一元表达式处理）

### 运算符识别

* [x] +
* [x] -
* [x] *
* [x] /
* [x] %
* [x] <
* [x] >
* [x] <=
* [x] >=
* [x] ==
* [x] !=
* [x] &&
* [x] ||
* [x] !
* [x] =

### 分隔符识别

* [x] (
* [x] )
* [x] {
* [x] }
* [x] ;
* [x] ,

### 注释处理

* [x] 单行注释（`//` 到行末）
* [x] 多行注释（`/* ... */`，Start Condition 处理，行列号正确追踪）

### 测试

* [x] Lexer 单元测试（36 个测试用例覆盖全部类别）
* [x] 记录 Lexer / Parser 联调 bug → `docs/bug-issue-log.md`

## 输出

* [x] Lexer 模块完成（`token.h` + `lexer.l` + `main.cpp` 集成）

---

# 第四阶段：语法分析器（Parser）

## 目标

将 Token 流转换为 AST。

## 任务

### AST 节点设计

* [x] Program
* [x] FunctionDecl
* [x] VarDecl
* [x] ConstDecl

### Statement

* [x] BlockStmt
* [x] IfStmt
* [x] WhileStmt
* [x] ReturnStmt
* [x] BreakStmt
* [x] ContinueStmt
* [x] AssignStmt
* [x] DeclStmt（语句块内的局部声明）

### Expression

* [x] BinaryExpr
* [x] UnaryExpr
* [x] CallExpr
* [x] IdentifierExpr
* [x] NumberExpr

### Parser

* [x] 编译单元解析
* [x] 函数定义解析
* [x] 声明解析
* [x] 语句解析
* [x] 表达式解析

### 测试

* [x] AST 输出测试
* [x] 文法覆盖测试（基础样例：全局声明、函数调用、控制流、表达式优先级）

## 输出

* [x] AST 构建完成
* [x] Parser 基础测试完成（`tests/parser/test_parser.cpp`）
* [x] 本地构建与自动化测试验证

---

# 第五阶段：语义分析

## 目标

检查程序是否合法。

## 任务

### 符号表

* [x] Symbol
* [x] Scope
* [x] SymbolTable

### 作用域管理

* [x] 全局作用域
* [x] 函数作用域
* [x] Block 作用域

### 类型检查

* [x] int
* [x] void

### 语义检查

#### 标识符

* [x] 重定义检查
* [x] 未定义检查
* [x] 使用必须在声明之后

#### 常量

* [x] const 不可修改
* [x] const 初始化表达式必须在编译期可确定（只能含数字字面量和已知常量）

#### 声明

* [x] VarDecl / ConstDecl 必须带初始化表达式

#### 函数

* [x] 函数存在检查
* [x] 参数数量检查
* [x] 参数类型检查（仅 int）
* [x] 返回值检查（int 函数每条执行路径必须 return；void 函数 return 不能带值）
* [x] 函数调用必须在被调函数声明之后
* [x] void 函数调用不能作为 if/while 条件或赋值语句右值

#### 表达式

* [x] 除数不能为零
* [x] 短路求值逻辑正确性

#### 控制流

* [x] break 检查（仅在循环中）
* [x] continue 检查（仅在循环中）

#### 程序入口

* [x] main 函数检查（存在、无参数、返回 int）

### 常量表达式求值

* [x] 编译期计算 const

## 输出

* [x] Semantic Analyzer 完成

---

# 第六阶段：IR 设计

## 目标

建立统一中间表示。

## 推荐方案

三地址码（Three Address Code）

示例：

```text
t1 = b * c
t2 = a + t1
```

## 任务

### 算术指令

* [x] ADD
* [x] SUB
* [x] MUL
* [x] DIV
* [x] MOD
* [x] NEG（一元取负）

### 比较指令

* [x] EQ
* [x] NE
* [x] LT
* [x] GT
* [x] LE
* [x] GE

### 逻辑指令

* [x] AND
* [x] OR
* [x] NOT

### 访存指令

* [x] LOAD（从内存加载全局/局部变量）
* [x] STORE（向内存存储全局/局部变量）

### 跳转指令

* [x] JMP
* [x] BEQ
* [x] BNE

### 函数指令

* [x] CALL
* [x] PARAM
* [x] RET

### IR Builder

* [x] Expr → IR
* [x] Stmt → IR
* [x] Function → IR

## 输出

* [x] IR 模块完成

---

# 第七阶段：代码优化

## 目标

提高性能测试得分。

## 基础优化

### 常量折叠

* [x] Constant Folding

```c
1 + 2 * 3
```

↓

```c
7
```

### 常量传播

* [x] Constant Propagation

### 死代码删除

* [x] Dead Code Elimination

### 公共子表达式消除

* [ ] CSE

## 高级优化（可选）

* [ ] Copy Propagation
* [ ] Peephole Optimization
* [ ] 简单寄存器分配

## 输出

* [x] Optimizer 基础优化完成

---

# 第八阶段：RISC-V 后端

## 目标

生成合法 RISC-V32 汇编。

## 数据段

### 全局变量

```asm
.data
g:
.word 10
```

### 全局常量

```asm
.data
c:
.word 5
```

## 代码段

### 函数

* [x] 函数标签
* [x] 栈帧建立
* [x] 栈帧回收

### 参数传递

* [x] a0 ~ a7

### 返回值

* [x] a0

### 控制流

* [x] if
* [x] while
* [x] break
* [x] continue

### 变量访问

* [x] 全局变量加载（la / lw）
* [x] 全局常量加载（la / lw）
* [x] 全局变量/常量存储（la / sw）
* [x] 局部变量栈内读写（lw / sw 相对 sp）

### 短路求值

* [x] `&&` 短路跳转
* [x] `||` 短路跳转

### 函数调用

* [x] call
* [x] ret

## 输出

* [x] Code Generator 完成（已通过 backend 结构测试，端到端运行仍需 RISC-V 工具链验证）

---

# 第九阶段：测试

## Lexer

* [x] Token 测试

## Parser

* [x] AST 测试（基础样例）

## Semantic

* [x] 语义错误测试

## IR

* [x] IR 生成测试

## Backend

* [x] 汇编生成测试

## 集成测试

* [x] 小型程序（样例已加入 `tests/integration`）
* [x] 递归程序（样例已加入 `tests/integration`）
* [x] 循环程序（样例已加入 `tests/integration`）
* [x] 多函数程序（样例已加入 `tests/integration`）

## 输出

* [x] 集成测试样例与端到端测试目标（`tests/integration/*.tc` + `tests/integration/test_integration.cpp`）
* [ ] 基于 RISC-V 工具链的真实运行结果测试报告

---

# 第十阶段：性能优化冲分

## 检查项

* [x] 常量折叠
* [x] 常量传播
* [x] 死代码删除
* [ ] 减少访存
* [ ] 减少跳转
* [ ] 简单寄存器分配

## 输出

* [ ] 性能测试结果

---

# 第十一阶段：实践报告

## 章节

* [ ] 项目概述
* [ ] 需求分析
* [ ] 总体设计
* [ ] 词法分析
* [ ] 语法分析
* [ ] AST 设计
* [ ] 语义分析
* [ ] IR 设计
* [ ] 优化设计
* [ ] RISC-V 后端
* [ ] 测试分析
* [ ] 团队分工
* [ ] 项目总结

---

# 最终验收检查

## 功能

* [ ] 所有功能测试通过

## 性能

* [ ] 开启 -opt 能正常运行

## 工程

* [x] CMake 正常构建
* [ ] README 完整
* [ ] 文档完整

## 提交

* [ ] GitHub 仓库
* [ ] 实践报告
* [ ] 指定分支
* [ ] 最终 Tag

```
```
