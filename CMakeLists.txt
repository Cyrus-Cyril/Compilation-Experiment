cmake_minimum_required(VERSION 3.16)
project(ToyCCompiler LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Flex & Bison
# ============================================================
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

# Flex: 生成 lexer
set(LEXER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen/lexer)
file(MAKE_DIRECTORY ${LEXER_OUTPUT_DIR})

FLEX_TARGET(ToyCLexer
    src/lexer/lexer.l
    ${LEXER_OUTPUT_DIR}/lex.yy.c
    COMPILE_FLAGS "--header-file=${LEXER_OUTPUT_DIR}/lex.yy.h"
)

# Bison: 生成 parser
set(PARSER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen/parser)
file(MAKE_DIRECTORY ${PARSER_OUTPUT_DIR})

BISON_TARGET(ToyCParser
    src/parser/parser.y
    ${PARSER_OUTPUT_DIR}/parser.tab.cpp
    COMPILE_FLAGS "-d"
)

# 将生成的 lexer/parser 编译为库
add_library(toyc_lexer STATIC
    ${FLEX_ToyCLexer_OUTPUTS}
)
set_target_properties(toyc_lexer PROPERTIES LINKER_LANGUAGE C)
target_include_directories(toyc_lexer PUBLIC
    ${LEXER_OUTPUT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(toyc_parser STATIC
    ${BISON_ToyCParser_OUTPUTS}
)
target_include_directories(toyc_parser PUBLIC
    ${PARSER_OUTPUT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(toyc_parser PUBLIC toyc_lexer)

# Bison 生成的 parser.tab.cpp 引用了 lexer 的 header，需要 lexer gen dir
target_include_directories(toyc_parser PRIVATE ${LEXER_OUTPUT_DIR})

# ============================================================
# Semantic Analyzer
# ============================================================
add_library(toyc_semantic STATIC
    src/semantic/symbol_table.cpp
    src/semantic/semantic_analyzer.cpp
)
target_include_directories(toyc_semantic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(toyc_semantic PUBLIC toyc_parser)

# ============================================================
# IR Generator
# ============================================================
add_library(toyc_ir STATIC
    src/ir/ir_builder.cpp
)
target_include_directories(toyc_ir PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(toyc_ir PUBLIC toyc_semantic)

# ============================================================
# Optimizer
# ============================================================
add_library(toyc_optimize STATIC
    src/optimize/optimizer.cpp
)
target_include_directories(toyc_optimize PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(toyc_optimize PUBLIC toyc_ir)

# ============================================================
# Backend (RISC-V32 Code Generator)
# ============================================================
add_library(toyc_backend STATIC
    src/backend/code_generator.cpp
)
target_include_directories(toyc_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(toyc_backend PUBLIC toyc_ir)

# ============================================================
# 可执行文件
# ============================================================
add_executable(toyc main.cpp)
target_include_directories(toyc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(toyc PRIVATE
    toyc_backend
    toyc_optimize
    toyc_ir
    toyc_semantic
    toyc_parser
    toyc_lexer
)