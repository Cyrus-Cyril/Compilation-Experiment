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
ADD_FLEX_BISON_DEPENDENCY(ToyCLexer ToyCParser)

# 将生成的 lexer/parser 编译为库
add_library(toyc_lexer STATIC
    ${FLEX_ToyCLexer_OUTPUTS}
)
set_source_files_properties(${FLEX_ToyCLexer_OUTPUTS} PROPERTIES LANGUAGE CXX)
target_include_directories(toyc_lexer PUBLIC
    ${LEXER_OUTPUT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(toyc_parser STATIC
    ${BISON_ToyCParser_OUTPUTS}
    src/parser/parser_api.cpp
)
target_include_directories(toyc_parser PUBLIC
    ${PARSER_OUTPUT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(toyc_parser PUBLIC toyc_lexer)

# Bison 生成的 parser.tab.cpp 引用了 lexer 的 header，需要 lexer gen dir
target_include_directories(toyc_parser PRIVATE ${LEXER_OUTPUT_DIR})

enable_testing()

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

add_executable(test_lexer
    tests/lexer/test_lexer.cpp
)
target_include_directories(test_lexer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_lexer PRIVATE toyc_lexer)

add_executable(test_parser
    tests/parser/test_parser.cpp
)
target_include_directories(test_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_parser PRIVATE toyc_parser toyc_lexer)

add_executable(test_semantic
    tests/semantic/test_semantic.cpp
)
target_include_directories(test_semantic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_semantic PRIVATE toyc_semantic toyc_parser toyc_lexer)

add_executable(test_ir
    tests/ir/test_ir.cpp
)
target_include_directories(test_ir PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_ir PRIVATE toyc_ir toyc_parser toyc_lexer)

add_executable(test_backend
    tests/backend/test_backend.cpp
)
target_include_directories(test_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_backend PRIVATE toyc_backend)

add_executable(test_optimizer
    tests/optimize/test_optimizer.cpp
)
target_include_directories(test_optimizer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_optimizer PRIVATE toyc_optimize)

add_executable(test_integration
    tests/integration/test_integration.cpp
)
target_include_directories(test_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_integration PRIVATE TOYC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_integration PRIVATE
    toyc_backend
    toyc_optimize
    toyc_ir
    toyc_semantic
    toyc_parser
    toyc_lexer
)

add_test(NAME lexer_unit COMMAND test_lexer)
add_test(NAME parser_unit COMMAND test_parser)
add_test(NAME semantic_unit COMMAND test_semantic)
add_test(NAME ir_unit COMMAND test_ir)
add_test(NAME backend_unit COMMAND test_backend)
add_test(NAME optimizer_unit COMMAND test_optimizer)
add_test(NAME integration_unit COMMAND test_integration)
