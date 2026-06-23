%{
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "toyc/parser/ast.h"

#define yylval parser_yylval

extern int yylex();
extern void yyerror(const char* msg);
%}

%code requires {
#include "toyc/parser/ast.h"

#include <vector>
}

%code {
namespace toyc {
extern CompUnit* g_parse_root;
extern std::string g_parse_error;
}

static toyc::SourceLocation makeLoc() {
    return {};
}

static toyc::BinaryExpr* makeBinary(toyc::BinaryOp op, toyc::ExprNode* left, toyc::ExprNode* right) {
    return new toyc::BinaryExpr(makeLoc(), op,
                                std::unique_ptr<toyc::ExprNode>(left),
                                std::unique_ptr<toyc::ExprNode>(right));
}

static std::vector<std::unique_ptr<toyc::Param>> takeParams(std::vector<toyc::Param*>* params) {
    std::vector<std::unique_ptr<toyc::Param>> result;
    if (params != nullptr) {
        result.reserve(params->size());
        for (toyc::Param* param : *params) {
            result.emplace_back(param);
        }
        delete params;
    }
    return result;
}

static std::vector<std::unique_ptr<toyc::ExprNode>> takeArgs(std::vector<toyc::ExprNode*>* args) {
    std::vector<std::unique_ptr<toyc::ExprNode>> result;
    if (args != nullptr) {
        result.reserve(args->size());
        for (toyc::ExprNode* arg : *args) {
            result.emplace_back(arg);
        }
        delete args;
    }
    return result;
}
}

%define api.value.type {toyc::LexerValue}

%token INT VOID CONST IF ELSE WHILE BREAK CONTINUE RETURN
%token PLUS MINUS STAR SLASH PERCENT LT GT LE GE EQ NE AND OR NOT ASSIGN
%token LPAREN RPAREN LBRACE RBRACE SEMICOLON COMMA
%token <strVal> ID
%token <intVal> NUMBER

%type <compUnit> program
%type <node> top_level top_level_decl decl stmt
%type <block> block
%type <nodeList> top_level_list stmt_list
%type <param> param
%type <paramList> param_list param_list_opt
%type <expr> expr lor_expr land_expr rel_expr add_expr mul_expr unary_expr primary_expr
%type <exprList> arg_list arg_list_opt
%type <typeTag> type_spec

%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE
%left OR
%left AND
%nonassoc LT GT LE GE EQ NE
%left PLUS MINUS
%left STAR SLASH PERCENT
%right UPLUS UMINUS NOT

%start program

%%

program:
    top_level_list {
        $$ = new toyc::CompUnit(makeLoc());
        for (toyc::ASTNode* node : *$1) {
            $$->elements.emplace_back(node);
        }
        delete $1;
        toyc::g_parse_root = $$;
    }
    ;

top_level_list:
    top_level {
        $$ = new std::vector<toyc::ASTNode*>();
        $$->push_back($1);
    }
    | top_level_list top_level {
        $1->push_back($2);
        $$ = $1;
    }
    ;

top_level:
    top_level_decl { $$ = $1; }
    | type_spec ID LPAREN param_list_opt RPAREN block {
        $$ = new toyc::FuncDef(makeLoc(), std::string($2), static_cast<toyc::Type>($1),
                               takeParams($4), std::unique_ptr<toyc::BlockStmt>($6));
        free($2);
    }
    ;

top_level_decl:
    INT ID ASSIGN expr SEMICOLON {
        $$ = new toyc::VarDecl(makeLoc(), std::string($2), std::unique_ptr<toyc::ExprNode>($4), true);
        free($2);
    }
    | CONST INT ID ASSIGN expr SEMICOLON {
        $$ = new toyc::ConstDecl(makeLoc(), std::string($3), std::unique_ptr<toyc::ExprNode>($5), true);
        free($3);
    }
    ;

decl:
    INT ID ASSIGN expr SEMICOLON {
        $$ = new toyc::VarDecl(makeLoc(), std::string($2), std::unique_ptr<toyc::ExprNode>($4), false);
        free($2);
    }
    | CONST INT ID ASSIGN expr SEMICOLON {
        $$ = new toyc::ConstDecl(makeLoc(), std::string($3), std::unique_ptr<toyc::ExprNode>($5), false);
        free($3);
    }
    ;

type_spec:
    INT { $$ = static_cast<int>(toyc::Type::INT); }
    | VOID { $$ = static_cast<int>(toyc::Type::VOID); }
    ;

param_list_opt:
    /* empty */ { $$ = new std::vector<toyc::Param*>(); }
    | param_list { $$ = $1; }
    ;

param_list:
    param {
        $$ = new std::vector<toyc::Param*>();
        $$->push_back($1);
    }
    | param_list COMMA param {
        $1->push_back($3);
        $$ = $1;
    }
    ;

param:
    INT ID {
        $$ = new toyc::Param(makeLoc(), std::string($2));
        free($2);
    }
    ;

block:
    LBRACE stmt_list RBRACE {
        $$ = new toyc::BlockStmt(makeLoc());
        for (toyc::ASTNode* stmt : *$2) {
            $$->stmts.emplace_back(stmt);
        }
        delete $2;
    }
    ;

stmt_list:
    /* empty */ { $$ = new std::vector<toyc::ASTNode*>(); }
    | stmt_list stmt {
        $1->push_back($2);
        $$ = $1;
    }
    ;

stmt:
    block { $$ = $1; }
    | SEMICOLON { $$ = new toyc::NullStmt(makeLoc()); }
    | expr SEMICOLON { $$ = new toyc::ExprStmt(makeLoc(), std::unique_ptr<toyc::ExprNode>($1)); }
    | ID ASSIGN expr SEMICOLON {
        $$ = new toyc::AssignStmt(makeLoc(), std::string($1), std::unique_ptr<toyc::ExprNode>($3));
        free($1);
    }
    | decl { $$ = new toyc::DeclStmt(makeLoc(), std::unique_ptr<toyc::ASTNode>($1)); }
    | IF LPAREN expr RPAREN stmt %prec LOWER_THAN_ELSE {
        $$ = new toyc::IfStmt(makeLoc(), std::unique_ptr<toyc::ExprNode>($3), std::unique_ptr<toyc::ASTNode>($5));
    }
    | IF LPAREN expr RPAREN stmt ELSE stmt {
        $$ = new toyc::IfStmt(makeLoc(), std::unique_ptr<toyc::ExprNode>($3),
                              std::unique_ptr<toyc::ASTNode>($5),
                              std::unique_ptr<toyc::ASTNode>($7));
    }
    | WHILE LPAREN expr RPAREN stmt {
        $$ = new toyc::WhileStmt(makeLoc(), std::unique_ptr<toyc::ExprNode>($3), std::unique_ptr<toyc::ASTNode>($5));
    }
    | BREAK SEMICOLON { $$ = new toyc::BreakStmt(makeLoc()); }
    | CONTINUE SEMICOLON { $$ = new toyc::ContinueStmt(makeLoc()); }
    | RETURN SEMICOLON { $$ = new toyc::ReturnStmt(makeLoc()); }
    | RETURN expr SEMICOLON { $$ = new toyc::ReturnStmt(makeLoc(), std::unique_ptr<toyc::ExprNode>($2)); }
    ;

expr:
    lor_expr { $$ = $1; }
    ;

lor_expr:
    land_expr { $$ = $1; }
    | lor_expr OR land_expr { $$ = makeBinary(toyc::BinaryOp::OR, $1, $3); }
    ;

land_expr:
    rel_expr { $$ = $1; }
    | land_expr AND rel_expr { $$ = makeBinary(toyc::BinaryOp::AND, $1, $3); }
    ;

rel_expr:
    add_expr { $$ = $1; }
    | rel_expr LT add_expr { $$ = makeBinary(toyc::BinaryOp::LT, $1, $3); }
    | rel_expr GT add_expr { $$ = makeBinary(toyc::BinaryOp::GT, $1, $3); }
    | rel_expr LE add_expr { $$ = makeBinary(toyc::BinaryOp::LE, $1, $3); }
    | rel_expr GE add_expr { $$ = makeBinary(toyc::BinaryOp::GE, $1, $3); }
    | rel_expr EQ add_expr { $$ = makeBinary(toyc::BinaryOp::EQ, $1, $3); }
    | rel_expr NE add_expr { $$ = makeBinary(toyc::BinaryOp::NE, $1, $3); }
    ;

add_expr:
    mul_expr { $$ = $1; }
    | add_expr PLUS mul_expr { $$ = makeBinary(toyc::BinaryOp::ADD, $1, $3); }
    | add_expr MINUS mul_expr { $$ = makeBinary(toyc::BinaryOp::SUB, $1, $3); }
    ;

mul_expr:
    unary_expr { $$ = $1; }
    | mul_expr STAR unary_expr { $$ = makeBinary(toyc::BinaryOp::MUL, $1, $3); }
    | mul_expr SLASH unary_expr { $$ = makeBinary(toyc::BinaryOp::DIV, $1, $3); }
    | mul_expr PERCENT unary_expr { $$ = makeBinary(toyc::BinaryOp::MOD, $1, $3); }
    ;

unary_expr:
    primary_expr { $$ = $1; }
    | PLUS unary_expr %prec UPLUS {
        $$ = new toyc::UnaryExpr(makeLoc(), toyc::UnaryOp::POS, std::unique_ptr<toyc::ExprNode>($2));
    }
    | MINUS unary_expr %prec UMINUS {
        $$ = new toyc::UnaryExpr(makeLoc(), toyc::UnaryOp::NEG, std::unique_ptr<toyc::ExprNode>($2));
    }
    | NOT unary_expr {
        $$ = new toyc::UnaryExpr(makeLoc(), toyc::UnaryOp::NOT, std::unique_ptr<toyc::ExprNode>($2));
    }
    ;

primary_expr:
    ID {
        $$ = new toyc::IdExpr(makeLoc(), std::string($1));
        free($1);
    }
    | ID LPAREN arg_list_opt RPAREN {
        $$ = new toyc::CallExpr(makeLoc(), std::string($1), takeArgs($3));
        free($1);
    }
    | NUMBER { $$ = new toyc::NumberExpr(makeLoc(), $1); }
    | LPAREN expr RPAREN { $$ = $2; }
    ;

arg_list_opt:
    /* empty */ { $$ = new std::vector<toyc::ExprNode*>(); }
    | arg_list { $$ = $1; }
    ;

arg_list:
    expr {
        $$ = new std::vector<toyc::ExprNode*>();
        $$->push_back($1);
    }
    | arg_list COMMA expr {
        $1->push_back($3);
        $$ = $1;
    }
    ;

%%

void yyerror(const char* msg) {
    toyc::g_parse_error = msg;
    std::fprintf(stderr, "Error: %s\n", msg);
}
