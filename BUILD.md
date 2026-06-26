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

#### 1. 验证完整编译输出

项目根目录提供了综合测试文件 `test.tc`；当前 `toyc` 默认执行完整编译管线并输出 RISC-V32 汇编：

```bash
# 生成汇编
./build/toyc < test.tc > output.s
```

如需验证单个模块，优先运行 CTest 或对应单元测试目标。

---

## 模块独立调试

当前主程序不提供 `-ast`、`-semantic`、`-ir` 等调试输出参数；模块验证通过单元测试完成：

```bash
# 全部已注册单元测试
ctest --test-dir build --output-on-failure

# 或单独运行
./build/test_lexer
./build/test_parser
./build/test_semantic
./build/test_ir
./build/test_backend
```

端到端样例位于 `tests/integration/`，文件名中的 `retN` 表示期望退出码为 `N`。

### 优化器

```bash
# `-opt` 当前会启用 Optimizer 入口；未实现的优化会保持 IR 不变
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
