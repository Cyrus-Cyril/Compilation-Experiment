# ToyC 编译器 — 构建与运行指南

## 环境要求

| 工具 | 最低版本 | 说明 |
|---|---|---|
| CMake | 3.16+ | 构建系统（评测环境 4.0.3） |
| C++ 编译器 | 支持 C++20 | MSVC 2019+ / GCC 10+ / Clang 12+ |
| Flex | 2.5+ | 词法分析器生成器 |
| Bison | 3.0+ | 语法分析器生成器 |

> **注意**：Windows 上 CMake 默认可能选择 NMake 生成器，这会要求 Visual Studio 工具链。
> 如果你的系统**没有安装 Visual Studio**，需使用 MinGW-w64 (GCC) 工具链。
> 可通过以下命令检查是否有 C++ 编译器：`g++ --version`

### Windows 安装

#### 方案 A：MinGW-w64 + Flex/Bison（推荐）

1. 下载 MinGW-w64（GCC 14.2.0）：
   - 从 [mingw-builds](https://github.com/niXman/mingw-builds-binaries/releases) 下载 `x86_64-14.2.0-release-posix-seh-ucrt-rt_v12-rev0.7z`
   - 解压到 `C:\mingw64`
   - 将 `C:\mingw64\bin` 添加到系统 PATH

2. 安装 Flex + Bison：
   ```powershell
   # 通过 Chocolatey（推荐）
   choco install cmake flex bison

   # 或下载 win_flex_bison 并重命名为 flex.exe / bison.exe 放入 C:\mingw64\bin
   ```

#### 方案 B：MSYS2

```powershell
# 安装 MSYS2 后在 MSYS2 MinGW 终端中运行
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc mingw-w64-x86_64-flex mingw-w64-x86_64-bison
```

### Linux 安装

```bash
sudo apt install cmake g++ flex bison   # Ubuntu/Debian
```

### macOS 安装

```bash
brew install cmake flex bison
```

---

## 构建

### 步骤 1：生成构建文件

> **Windows 用户**：若使用 MinGW-w64（GCC），需额外指定生成器：
> ```powershell
> cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
> ```
> 若使用 Visual Studio 工具链，则不需要 `-G` 参数。

**Linux / macOS / Windows（有 VS）**：

```bash
# 在项目根目录下执行
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

Release 模式生成高效代码；调试阶段可用 `Debug`：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

### 步骤 2：编译

```bash
cmake --build build
```

或指定并行编译核数：

```bash
cmake --build build -j 8
```

编译成功后，可执行文件位于：

- Windows：`build\Release\toyc.exe` 或 `build\Debug\toyc.exe`
- Linux/macOS：`build/toyc`

---

## 运行

### 基本用法

编译器从 **stdin** 读取 ToyC 源码，向 **stdout** 输出 RISC-V32 汇编。

```bash
# 交互式输入（输入后按 Ctrl+C 结束）
./build/toyc

# 从文件重定向输入
./build/toyc < input.tc > output.s

# 查看汇编输出
./build/toyc < test.tc
```

#### 1.验证 Lexer

项目根目录提供了综合测试文件 `test.tc`，覆盖全部关键字、运算符、分隔符、
数字（含负数/零）、标识符、注释和非法字符场景：

```bash
# 运行词法分析，查看完整 Token 输出
.\build\toyc.exe < test.tc

# 将 Token 输出保存到文件，错误信息分开查看
.\build\toyc < test.tc > tokens.txt 2> errors.txt
```

预期结果：

- 正常 Token 全部正确识别（每行一个 `TYPE` 或 `TYPE(值)`）
- 非法字符 `@` 输出到 stderr：
  ```
  Error(line:col): illegal character '@'
  ```
- 返回码 0（词法错误不终止编译）

---

## 模块独立调试

以下方式允许单独运行某个模块，验证其产出是否正确，适合开发阶段逐模块排查问题。

### 词法分析器（Lexer）

读取 ToyC 源码，输出 Token 流（每行一个 Token，格式 `TYPE` 或 `TYPE(值)`）：

```bash
# 交互式输入
./build/toyc
# 输入源码后按 Ctrl+Z 回车结束

# 从文件重定向
./build/toyc < test.tc

# 文件重定向，仅保留 Token 输出（错误信息送 stderr，可分开查看）
./build/toyc < test.tc > tokens.txt 2> errors.txt
```

输出示例（输入 `int a = 42;`）：

```
INT
ID(a)
ASSIGN
NUMBER(42)
SEMICOLON
```

非法字符示例（输出到 stderr）：

```
Error(1:9): illegal character '@'
```

### 语法分析器（Parser）← 待实现

```bash
# 读取 ToyC 源码，输出 AST
./build/toyc -ast < test.tc
```

### 语义分析器（Semantic Analyzer）← 待实现

```bash
# 读取 ToyC 源码，输出符号表信息
./build/toyc -semantic < test.tc
```

### IR 生成器 ← 待实现

```bash
# 读取 ToyC 源码，输出三地址码 IR
./build/toyc -ir < test.tc
```

### 优化器 ← 待实现

```bash
# 读取 ToyC 源码，输出优化后的 IR
./build/toyc -opt < test.tc
```

### 完整编译（默认模式）

```bash
# 读取 ToyC 源码，输出 RISC-V32 汇编
./build/toyc < test.tc > output.s
```

### 启用优化

添加 `-opt` 参数开启优化 Pass（常量折叠、死代码删除等）：

```bash
./build/toyc -opt < input.tc > output.s
```

### 验证汇编

生成的 RISC-V32 汇编可以用交叉编译工具链测试，也可以在 Linux 上用 QEMU 模拟：

```bash
# 安装 RISC-V 工具链
sudo apt install gcc-riscv64-linux-gnu qemu-user

# 汇编 + 链接
riscv64-linux-gnu-gcc -march=rv32im -mabi=ilp32 -static -o test output.s

# 运行
qemu-riscv32 ./test
echo "Exit code: $?"
```

---

## 快速测试示例

创建测试文件 `hello.tc`：

```c
int main() {
    return 42;
}
```

编译并运行：

```bash
# Windows (PowerShell)
Get-Content hello.tc | .\build\Release\toyc.exe > hello.s

# Linux/macOS
./build/toyc < hello.tc > hello.s
```

（最终编译阶段完成后，RISC-V 运行结果 `exit code` 应为 `42`）

---

## 项目结构速览

```
ToyCCompiler/
├── CMakeLists.txt      —— 构建配置
├── BUILDL.md           —— 本文档
├── README.md           —— 开发进度仪表盘
├── main.cpp            —— 编译器入口
├── include/toyc/       —— 头文件（按模块分目录）
├── src/                —— 源文件（按模块分目录）
├── tests/              —— 单元测试
└── docs/               —— 设计文档 + Agent Prompt
```