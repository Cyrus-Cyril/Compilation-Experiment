#include "toyc/parser/parser_api.h"

#include "toyc/parser/ast.h"

extern int yyparse();
extern void* yy_scan_string(const char*);
extern void yy_delete_buffer(void*);

namespace toyc {

CompUnit* g_parse_root = nullptr;
std::string g_parse_error;

std::unique_ptr<CompUnit> parseString(const std::string& input) {
    g_parse_root = nullptr;
    g_parse_error.clear();

    void* buffer = yy_scan_string(input.c_str());
    const int result = yyparse();
    yy_delete_buffer(buffer);

    if (result != 0 || g_parse_root == nullptr) {
        delete g_parse_root;
        g_parse_root = nullptr;
        return nullptr;
    }

    std::unique_ptr<CompUnit> root(g_parse_root);
    g_parse_root = nullptr;
    return root;
}

std::unique_ptr<CompUnit> parseStdin() {
    g_parse_root = nullptr;
    g_parse_error.clear();

    const int result = yyparse();
    if (result != 0 || g_parse_root == nullptr) {
        delete g_parse_root;
        g_parse_root = nullptr;
        return nullptr;
    }

    std::unique_ptr<CompUnit> root(g_parse_root);
    g_parse_root = nullptr;
    return root;
}

const std::string& lastParseError() { return g_parse_error; }

}  // namespace toyc
