# ToyC 编译器架构设计

## 概述

本文档定义 ToyC 编译器的整体架构，包括管线阶段划分、模块职责边界、模块间接口契约、错误处理策略和项目目录布局。本文档是后续各模块详细设计的顶层约束。

**适用范围**：全部 11 个开发阶段，覆盖从源码到 RISC-V32 汇编的完整管线。

**上游依赖**：`docs/任务要求.md`（语言规范、测评约束、技术栈）
**下游文档**：`ast_design.md`、`ir_design.md`、`symbol_table.md`

---

## 核心设计

### 1. 编译器管线

```
┌─────────────────────────────────────────────────────────────┐
│                    ToyC Compiler Pipeline                    │
├───────────┬──────────┬──────────┬───────────┬───────────────┤
│  Source   │  Lexer   │  Parser  │ Semantic  │  IR Generator │
│  (.tc)    │  (Flex)  │  (Bison) │ Analyzer  │               │
│    │      │    │     │    │     │    │      │      │        │
│    ▼      │    ▼     │    ▼     │    ▼      │      ▼        │
│  stdin ───┤► Token   │►  AST    │► Annotated│►  IR Program  │
│           │   Stream  │          │   AST     │   (TAC)       │
├───────────┴──────────┴──────────┴───────────┴───────────────┤
│  IR Program ──► Optimizer ──► Backend ──► Assembly(.s)      │
│                 (-opt 时)       (RISC-V32)   stdout           │
└─────────────────────────────────────────────────────────────┘
```

### 2. 模块职责与接口

#### 2.1 Lexer（词法分析器）

| 项目 | 说明 |
|---|---|
| **实现方式** | Flex（`.l` 规则文件 → `yylex()`） |
| **输入** | ToyC 源文件（stdin / `FILE*`） |
| **输出** | Token 流（通过 `yylex()` 逐 Token 返回给 Parser） |
| **核心数据结构** | `TokenType` 枚举、`Token` 结构体（值 + 位置信息） |

**Token 类型清单**（共 29 个）：

| 类别 | Token |
|---|---|
| 关键字 (9) | `INT`, `VOID`, `CONST`, `IF`, `ELSE`, `WHILE`, `BREAK`, `CONTINUE`, `RETURN` |
| 运算符 (15) | `PLUS`(+), `MINUS`(-), `STAR`(*), `SLASH`(/), `PERCENT`(%), `LT`(<), `GT`(>), `LE`(<=), `GE`(>=), `EQ`(==), `NE`(!=), `AND`(&&), `OR`(\|\|), `NOT`(!), `ASSIGN`(=) |
| 分隔符 (6) | `LPAREN`(() `RPAREN`()), `LBRACE`({), `RBRACE`(}), `SEMICOLON`(;), `COMMA`(,) |
| 字面量/标识符 (2) | `ID`（标识符，携带名称字符串）, `NUMBER`（整数，携带数值） |

**Token 结构体**：

```cpp
enum class TokenType { INT, VOID, CONST, IF, ELSE, WHILE, BREAK, CONTINUE, RETURN,
                       PLUS, MINUS, STAR, SLASH, PERCENT, LT, GT, LE, GE, EQ, NE,
                       AND, OR, NOT, ASSIGN,
                       LPAREN, RPAREN, LBRACE, RBRACE, SEMICOLON, COMMA,
                       ID, NUMBER, END };

struct Token {
    TokenType type;
    std::string lexeme;     // 原始字符串
    int value;              // NUMBER 的数值
    SourceLocation loc;     // 行号 + 列号
};
```

**Flex → Bison 接口**：通过 `yylex()` 返回值传递 TokenType，通过全局变量 `yylval` 传递 Token 值（标识符名 / 数字值）。

**错误处理**：非法字符 → 输出错误信息（含行列号）到 stderr → 跳过该字符继续扫描（不终止）。

#### 2.2 Parser（语法分析器）

| 项目 | 说明 |
|---|---|
| **实现方式** | Bison（`.y` 文法文件 → `yyparse()`） |
| **输入** | Token 流（通过调用 `yylex()` 逐 Token 获取） |
| **输出** | AST 根节点（`CompUnit`） |
| **核心数据结构** | AST 节点类层次（详见 `ast_design.md`） |

**Bison 配置要点**：

```
%union —— 存放 AST 节点指针、Token 值等
%type  —— 声明每个非终结符对应的语义值类型
%left / %right / %nonassoc —— 运算符优先级与结合性
%start —— CompUnit
```

**运算符优先级**（从低到高）：

| 优先级 | 运算符 | 结合性 |
|---|---|---|
| 1 | `\|\|` | 左结合 |
| 2 | `&&` | 左结合 |
| 3 | `< > <= >= == !=` | 左结合（不可链式） |
| 4 | `+ -` | 左结合 |
| 5 | `* / %` | 左结合 |
| 6 | `+ - !`（一元） | 右结合 |

**语义动作**：每个产生式对应的 C++ 代码块，负责创建 AST 节点、连接父子关系。

**Bison → Semantic 接口**：`yyparse()` 返回 0 表示成功，AST 根节点通过全局变量或返回值传出。

**错误处理**：语法错误 → `yyerror()` 报告位置到 stderr → 通过 `error` 产生式恢复到 `;` 或 `}`，继续解析（不终止）。

#### 2.3 Semantic Analyzer（语义分析器）

| 项目 | 说明 |
|---|---|
| **实现方式** | C++ 手写，通过 AST Visitor 遍历 |
| **输入** | AST 根节点（未经语义标注） |
| **输出** | 类型标注后的 AST + 完整的 SymbolTable |
| **核心数据结构** | SymbolTable（详见 `symbol_table.md`） |

**语义检查项**（详见 `symbol_table.md` 第5节）：

1. 重定义 / 未定义检查
2. 声明后使用检查
3. const 不可修改检查
4. const 初始化编译期可确定检查
5. 函数存在 / 参数数量 / 参数类型检查
6. 返回值检查（int 函数每条路径 return；void 函数无返回值）
7. void 函数调用不能作为条件/赋值右值
8. break/continue 仅在循环中
9. 除数不能为零
10. main 函数检查（存在、无参数、返回 int）

**Semantic → IR 接口**：类型标注后的 AST + 完整的 SymbolTable（含每个符号的类型、位置信息）。

**错误处理**：语义错误 → 输出错误信息到 stderr → 继续检查后续节点（尽可能累积报告所有错误）→ 最终返回是否有错误。

#### 2.4 IR Generator（中间代码生成器）

| 项目 | 说明 |
|---|---|
| **实现方式** | C++ 手写，遍历 Annotated AST |
| **输入** | Annotated AST + SymbolTable |
| **输出** | IR 指令序列（`std::vector<IRInstruction>`） |
| **核心数据结构** | IR 指令集（详见 `ir_design.md`） |

**IR Generator → Optimizer → Backend 接口**：`IRProgram`（即 `std::vector<IRInstruction>`），模块间通过内存传递。

#### 2.5 Optimizer（优化器）

| 项目 | 说明 |
|---|---|
| **实现方式** | C++ 手写，在 IR 指令序列上逐 Pass 变换 |
| **输入** | IR 指令序列 |
| **输出** | 优化后的 IR 指令序列 |
| **触发条件** | 命令行参数 `-opt` 存在时启用 |

**优化 Pass 列表**：
- Constant Folding（常量折叠）
- Constant Propagation（常量传播）
- Dead Code Elimination（死代码删除）
- Common Subexpression Elimination（CSE）
- Copy Propagation（可选）
- Peephole Optimization（可选）
- Simple Register Allocation（可选）

#### 2.6 Backend（代码生成器）

| 项目 | 说明 |
|---|---|
| **实现方式** | C++ 手写 |
| **输入** | 优化后的 IR 指令序列 |
| **输出** | RISC-V32 汇编文本（stdout） |
| **目标** | RV32I 基础指令集 |

**输出结构**：
```asm
.data
    # 全局变量和常量

.text
    # 函数代码
    main:
        # 栈帧建立
        # 函数体
        # 栈帧回收
```

### 3. 主流程（main.cpp）

```
main(argc, argv):
    1. 解析命令行：检查 argv 中是否有 "-opt"
    2. 设置输入源为 stdin
    3. 调用 yyparse() 进行词法+语法分析 → 得到 AST
    4. 若 yyparse() 失败，输出错误到 stderr，返回非零退出码
    5. 创建 SemanticAnalyzer，遍历 AST → 进行语义检查
    6. 若语义错误，输出所有错误到 stderr，返回非零退出码
    7. 创建 IRGenerator，遍历 AST + SymbolTable → 生成 IR
    8. 若 -opt：
       创建 Optimizer，在 IR 上逐 Pass 执行
    9. 创建 CodeGenerator，将 IR 翻译为 RISC-V32 汇编 → 写入 stdout
    10. 返回 0
```

### 4. 错误处理策略

| 阶段 | 策略 | 是否终止 |
|---|---|---|
| Lexer | 报告非法字符 → 跳过该字符继续 | 不终止 |
| Parser | 报告语法错误 → error 恢复继续 | 不终止 |
| Semantic | 累积报告所有语义错误 → 检查完毕后统一判断 | 所有检查完成后决定是否终止 |
| IR / Optimizer | 不产生新错误（输入已合法） | — |
| Backend | 不产生新错误 | — |

**错误信息格式**：`Error(line:col): message`

---

## 设计决策与理由

### 为什么选择 Flex + Bison？

- 评测环境原生支持，无需额外安装依赖
- 轻量级，无运行时依赖
- Flex 基于 DFA（O(n)），Bison 基于 LALR(1)，解析效率高
- ToyC 文法满足 LALR(1)，不需要 GLR 等更强算法

### 为什么选择三地址码（TAC）而非直接生成汇编？

- TAC 是平台无关的中间表示，将前端（语法/语义）与后端（RISC-V）解耦
- TAC 指令粒度适合做优化（常量折叠、死代码删除等）
- TAC 便于调试：可单独打印 IR 验证生成正确性

### 为什么 Semantic Analyzer 后置而非嵌入 Parser？

- 分离关注点：Parser 只负责结构正确，Semantic 负责意义正确
- 语义检查需要完整的符号表和作用域信息，Parser 阶段尚未建立
- 更容易定位错误：语法错误和语义错误分开报告

### 为什么全局变量/常量通过 LOAD_GLOBAL/STORE_GLOBAL 而不是直接用地址？

- 保持 IR 的平台无关性：不同目标平台的访存指令不同
- Backend 负责将符号名映射为 RISC-V 的 `la/lw/sw` 序列

---

## 模块间数据流

```
stdin ──► Flex(yylex) ──── TokenType ────► Bison(yyparse)
                  yylval(标识符名/数值)
                                              │
                                              ▼ AST (CompUnit*)
                                     SemanticAnalyzer
                                              │
                                              ▼ Annotated AST + SymbolTable
                                       IRGenerator
                                              │
                                              ▼ vector<IRInstruction>
                                  ┌── Optimizer (-opt 时)
                                  │
                                  ▼ vector<IRInstruction>
                                  CodeGenerator
                                              │
                                              ▼ RISC-V Assembly(text)
                                             stdout
```

---

## 项目目录结构

```
ToyCCompiler/
├── CMakeLists.txt                    # 构建配置
├── README.md                         # 进度仪表盘
│
├── docs/
│   ├── 任务要求.md                    # 需求基线（只读）
│   ├── design/
│   │   ├── architecture.md           # 本文档
│   │   ├── ast_design.md             # AST 节点体系设计
│   │   ├── ir_design.md              # 三地址码指令集设计
│   │   └── symbol_table.md           # 符号表与作用域设计
│   └── prompt/                       # 各 Agent 的 System Prompt
│
├── include/toyc/
│   ├── lexer/
│   │   └── token.h                   # TokenType 枚举 + Token 结构体
│   ├── parser/
│   │   └── ast.h                     # AST 节点定义（按 ast_design.md）
│   ├── semantic/
│   │   └── symbol_table.h            # SymbolTable 定义（按 symbol_table.md）
│   ├── ir/
│   │   └── ir.h                      # IR 数据结构定义（按 ir_design.md）
│   ├── optimize/
│   │   └── optimizer.h               # 优化 Pass 接口
│   └── backend/
│       └── code_generator.h          # RISC-V 代码生成接口
│
├── src/
│   ├── lexer/
│   │   └── lexer.l                   # Flex 规则文件
│   ├── parser/
│   │   └── parser.y                  # Bison 文法文件
│   ├── semantic/
│   │   ├── semantic_analyzer.cpp     # 语义检查实现
│   │   └── symbol_table.cpp          # 符号表实现
│   ├── ir/
│   │   └── ir_builder.cpp            # IR 生成实现
│   ├── optimize/
│   │   └── optimizer.cpp             # 优化 Pass 实现
│   └── backend/
│       └── code_generator.cpp        # RISC-V 代码生成实现
│
├── tests/
│   ├── lexer/                        # Lexer 单元测试
│   ├── parser/                       # Parser 单元测试
│   ├── semantic/                     # 语义错误测试
│   ├── ir/                           # IR 生成测试
│   └── backend/                      # 汇编生成测试
│
└── main.cpp                          # 编译器入口
```

### CMake 模块分工

| CMake 目标 | 依赖 |
|---|---|
| `toyc_lexer` | Flex 生成 |
| `toyc_parser` | Bison 生成 + `toyc_lexer` |
| `toyc_semantic` | `toyc_parser` |
| `toyc_ir` | `toyc_semantic` |
| `toyc_optimize` | `toyc_ir` |
| `toyc_backend` | `toyc_ir` |
| `toyc` (可执行文件) | 全部以上 |

---

## 示例

### 输入（ToyC）

```c
int a = 1;
const int b = 2;

int main() {
    int c = a + b;
    return c;
}
```

### 经过各阶段

**Token Stream**：
```
INT ID(a) ASSIGN NUMBER(1) SEMICOLON
CONST INT ID(b) ASSIGN NUMBER(2) SEMICOLON
INT ID(main) LPAREN RPAREN LBRACE
INT ID(c) ASSIGN ID(a) PLUS ID(b) SEMICOLON
RETURN ID(c) SEMICOLON
RBRACE
```

**AST（简化表示）**：
```
CompUnit
├── VarDecl(name="a", type=int, init=NumberExpr(1))
├── ConstDecl(name="b", type=int, init=NumberExpr(2))
└── FuncDef(name="main", returnType=int, params=[])
    └── BlockStmt
        └── VarDecl(name="c", type=int, init=BinaryExpr(+, IdExpr("a"), IdExpr("b")))
        └── ReturnStmt(IdExpr("c"))
```

**IR（三地址码）**：
```
LOAD_GLOBAL  r0, a      # r0 = a
LOAD_GLOBAL  r1, b      # r1 = b
ADD          r2, r0, r1 # r2 = a + b
STORE_LOCAL  [c], r2     # c = r2
LOAD_LOCAL   r3, [c]    # r3 = c
RET          r3         # return r3
```

**RISC-V32 Assembly**：
```asm
.data
a: .word 1
b: .word 2

.text
main:
    addi sp, sp, -16
    sw   ra, 12(sp)
    la   t0, a
    lw   t0, 0(t0)
    la   t1, b
    lw   t1, 0(t1)
    add  t2, t0, t1
    sw   t2, 8(sp)
    lw   a0, 8(sp)
    lw   ra, 12(sp)
    addi sp, sp, 16
    ret
```

---

## 内部一致性检查

- [x] 模块接口定义清晰：每个 `→` 有明确的数据类型
- [x] 错误处理策略一致：stderr 报错 + 尽可能继续（前端）+ 累积报错（语义）
- [x] 目录结构对应模块划分：每个模块在 `include/` 和 `src/` 下均有独立目录
- [x] 与 `ast_design.md` 一致：AST 节点类型列表对齐
- [x] 与 `ir_design.md` 一致：IR 指令集定义对齐
- [x] 与 `symbol_table.md` 一致：符号表接口对齐