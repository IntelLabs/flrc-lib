%{
/*
 * COPYRIGHT_NOTICE_1
 */

#include "pillar2c.ast.h"

#if 0
#ifndef LEX_CPP
extern "C" int yyparse(void);
extern "C" int yylex(void);
#else
extern int yyparse(void);
extern int yylex(void);
#endif
#endif
extern int yyparse(void);
extern int yylex(void);

extern "C" int yyerror(char const *s);
extern int yylineno;

#include <stdio.h>

#include <vector>
#include <stack>

using namespace std;

template <class T>
class dumbstack {
public:
  dumbstack(void) {}
  void push(T) {}
  void pop(void) {}
};

#ifndef NO_TOP_LIST
std::list<external_declaration *> g_ed_list;
#else
translation_unit *g_ast_tree=NULL;
#endif

#if 0
#ifndef LEX_CPP
extern "C" {
#endif
int g_pillar2c_debug_level;
#ifndef LEX_CPP
}
#endif
#endif

int g_make_type_into_identifier = 0;

PHASE phase_selector = FULLFILE;

extern "C" void set_type_into_identifier(void) {
    if(g_make_type_into_identifier) {
        if(g_pillar2c_debug_level > 2) {
            printf("Didn't turn off previous make_type_into_identifier.\n");
        }
//        assert(0);
    }
    if(g_pillar2c_debug_level > 2) {
        printf("Setting type_into_identifier.\n");
    }
    g_make_type_into_identifier = 1;
}

extern "C" void unset_type_into_identifier(void) {
    if(g_pillar2c_debug_level > 2) {
        printf("Unsetting type_into_identifier.\n");
    }
    g_make_type_into_identifier = 0;
}

#ifdef VS_DPRINTF
#define dprintf(a,...) if(g_pillar2c_debug_level > 1) printf(a, __VA_ARGS__)
#else
#define dprintf(a...) if(g_pillar2c_debug_level > 1) printf(a)
#endif


//unsigned g_pillar_mode = 1;
std::stack<PILLAR2C_CALLCONV> g_cc_stack;

extern int g_short_mainline;

%}

%expect 3

%union {
    char *str;
    id_info *symbol;
    primary_expression *prim_exp;
    postfix_expression *post_exp;
    call_expression *call_exp;
    argument_expression_list *ael;
    unary_expression *unary_exp;
    unary_operator *unary_op;
    cast_expression *cast;
    multiplicative_expression *mult;
    additive_expression *addit;
    shift_expression *shift;
    relational_expression *relate;
    equality_expression *equal;
    and_expression *and_expr;
    exclusive_or_expression *eoe;
    inclusive_or_expression *ioe;
    logical_and_expression *logand;
    logical_or_expression *logor;
    conditional_expression *condexp;
    assignment_expression *assignexp;
    assignment_operator *assignop;
    expression *expr;
    constant_expression *ce;
    declaration      *dec;
    declaration_specifiers *ds;
    init_declarator_list *init_decl_list;
    init_declarator *init_decl;
    storage_class_specifier *scs;
    type_specifier   *ts;
    ts_comma_list *tscl;
    assignment_expression_list *aelist;
    multiple_ret_value *mrv;
    multiple_ret_expr *mre;
    call_conv_specifier *ccs;
    struct_or_union_specifier *sous;
    struct_or_union *sou;
    struct_declaration_list *sdecllist;
    struct_declaration *sdecl;
    specifier_qualifier_list *sql;
    struct_declarator_list *sdlist;
    struct_declarator *strdec;
    enum_specifier *enums;
    enumerator_list *enum_list;
    enumerator *enumer;
    type_qualifier *tq;
    function_specifier *fs;
    declarator       *decl;
    direct_declarator *direct_decl;
    pointer *ptr;
    type_qualifier_list *tql;
    parameter_type_list *ptl;
    parameter_list *plist;
    parameter_declaration *pd;
    identifier_list *id_list;
    type_name *tn;
    abstract_declarator *ad;
    direct_abstract_declarator *dad;
    initializer      *initer;
    initializer_list *init_list;
    designation      *design;
    designator_list  *desig_list;
    designator       *desig;
    statement        *stat;
//    statement_list   *statlist;
    labeled_statement *lstat;
    compound_statement *cstat;
    block_item_list  *bil;
    block_item       *bi;
    expression_statement *expression_stmt;
    selection_statement *select;
    iteration_statement *iter;
    jump_statement   *jump;
    function_definition *func_def;
    declaration_list *decl_list;
    external_declaration *ex_decl;
    translation_unit *tu;
    uudeclspec_list *uuds_list;
    declspec_specifier *dspec;
    anonymous_struct_declaration *asd;
    type_list *tl;
    continuation_type *cont_type;
    continuation_var_type *cont_var_type;
    Scope *scope;
    attr_or_uuasmuu *attr_or_uuasmuu_type;
}

%type<prim_exp> primary_expression
%type<post_exp> postfix_expression
%type<call_exp> call_expression
%type<ael> argument_expression_list
%type<unary_exp> unary_expression
%type<unary_op> unary_operator
%type<cast> cast_expression
%type<mult> multiplicative_expression
%type<addit> additive_expression
%type<shift> shift_expression
%type<relate> relational_expression
%type<equal> equality_expression
%type<and_expr> and_expression
%type<eoe> exclusive_or_expression
%type<ioe> inclusive_or_expression
%type<logand> logical_and_expression
%type<logor> logical_or_expression
%type<condexp> conditional_expression
%type<assignexp> assignment_expression
%type<assignop> assignment_operator
%type<expr> expression
%type<ce> constant_expression
%type<dec> declaration
%type<ds> declaration_specifiers
//%type<ds> declaration_specifiers_return
//%type<ds> fd_decl_specs
%type<init_decl_list> init_declarator_list
%type<init_decl> init_declarator
%type<scs> storage_class_specifier
%type<ts> type_specifier
%type<tscl> ts_comma_list
%type<aelist> ae_list
%type<mrv> multiple_ret_value
%type<mre> multiple_ret_expr
%type<ccs> call_conv_specifier
%type<sous> struct_or_union_specifier
%type<sou> struct_or_union
%type<sdecllist> struct_declaration_list
%type<sdecl> struct_declaration
%type<sql> specifier_qualifier_list
%type<sdlist> struct_declarator_list
%type<strdec> struct_declarator
%type<enums> enum_specifier;
%type<enum_list> enumerator_list
%type<enumer> enumerator
%type<tq> type_qualifier
%type<fs> function_specifier
%type<decl> declarator
%type<direct_decl> direct_declarator
%type<ptr> pointer
%type<tql> type_qualifier_list
%type<ptl> parameter_type_list
%type<plist> parameter_list
%type<pd> parameter_declaration
%type<id_list> identifier_list
%type<tn> type_name
%type<ad> abstract_declarator
%type<dad> direct_abstract_declarator
%type<initer> initializer
%type<init_list> initializer_list
%type<design> designation
%type<desig_list> designator_list
%type<desig> designator
%type<stat> statement
//%type<statlist> statement_list
%type<lstat> labeled_statement
%type<cstat> compound_statement
%type<bil> block_item_list
%type<bi> block_item
%type<expression_stmt> expression_statement
%type<select> selection_statement
%type<iter> iteration_statement
%type<jump> jump_statement
%type<func_def> function_definition
%type<decl_list> declaration_list
%type<ex_decl> external_declaration
%type<tu> translation_unit
%type<dspec> declspec_specifier
%type<uuds_list> uudeclspec_list
%type<asd> anonymous_struct_declaration
%type<tl> type_list
%type<cont_type> continuation_type
%type<cont_var_type> continuation_var_type
%type<attr_or_uuasmuu_type> attr_or_uuasmuu

%token <symbol> IDENTIFIER TYPE_NAME TYPE_STRUCT_NAME SYMBOL
%token <str> HASH UUASM UASM UUASMUU POUND_LINE MAINTAIN
%token <str> CONSTANT STRING_LITERAL
%token <str> CONSTANT_INT CONSTANT_UNSIGNED CONSTANT_LONG CONSTANT_ULONG CONSTANT_FLOAT CONSTANT_DOUBLE CONSTANT_LONG_DOUBLE CONSTANT_ULONGLONG
%token PTR_OP INC_OP DEC_OP LEFT_OP RIGHT_OP LE_OP GE_OP EQ_OP NE_OP
%token AND_OP OR_OP MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN ADD_ASSIGN
%token SUB_ASSIGN LEFT_ASSIGN RIGHT_ASSIGN AND_ASSIGN
%token XOR_ASSIGN OR_ASSIGN SIZEOF BUILTIN_EXPECT BUILTIN_OFFSETOF

%token TYPEDEF EXTERN STATIC AUTO REGISTER INLINE RESTRICT UURESTRICT UUINLINE UINLINE UUINLINEUU
%token CHAR SHORT INT LONG SIGNED UNSIGNED FLOAT DOUBLE CONST VOLATILE VOID BUILTIN_VA_LIST INT64 INT32 INT16 INT8
%token BOOL COMPLEX IMAGINARY
%token STRUCT UNION ENUM ELLIPSIS FORCEINLINE

%token CASE DEFAULT IF ELSE SWITCH WHILE DO FOR GOTO CONTINUE BREAK RETURN
%token UCDECL CDECL STDCALL PASCAL UDECLSPEC UUDECLSPEC ALIGN512 ALIGN256 ALIGN128 ALIGN64 ALIGN32 ALIGN16 ALIGN8 ALIGN4 ALIGN2 ALIGN1 DLLIMPORT DLLEXPORT NORETURN NOINLINE NAKED DEPRECATED NOALIAS UUW64 UUPTR64 INTRIN_TYPE

%token PRT_CUT PRT_TO PRT_TAILCALL PRT_MANAGED_OFF PRT_MANAGED_ON PRT_REF PRT_PDECL PRT_PCDECL PRT_NOYIELD PRT_PCALL PRT_WITH PRT_ALSO PRT_CUTS PRT_CONTINUATION PRT_CONTINUATION_VAR PRT_VSE
%token SOURCE_ANNOTATION_ATTRIBUTE SAA_PARAM SAA_RETVAL SAA_METHOD MULT_START MULT_END PRT_PUSH_CC PRT_POP_CC
%token <str> GCC_ATTRIBUTE

%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%start translation_unit
%%

primary_expression
	: IDENTIFIER
             {
                  $$ = new primary_expression_identifier($1);
                  dprintf("IDENTIFIER=%s\n",$1->get_name());
                  unset_type_into_identifier();
             }
	| CONSTANT
             {
                  $$ = new primary_expression_constant($1);
                  dprintf("CONSTANT\n");
				  free($1);
             }
	| CONSTANT_INT
             {
                  $$ = new primary_expression_constant_int($1);
                  dprintf("CONSTANT_INT\n");
				  free($1);
             }
	| CONSTANT_UNSIGNED
             {
                  $$ = new primary_expression_constant_unsigned($1);
                  dprintf("CONSTANT_UNSIGNED\n");
				  free($1);
             }
	| CONSTANT_LONG
             {
                  $$ = new primary_expression_constant_long($1);
                  dprintf("CONSTANT_LONG\n");
				  free($1);
             }
	| CONSTANT_ULONG
             {
                  $$ = new primary_expression_constant_ulong($1);
                  dprintf("CONSTANT_ULONG\n");
				  free($1);
             }
	| CONSTANT_ULONGLONG
             {
                  $$ = new primary_expression_constant_ulonglong($1);
                  dprintf("CONSTANT_ULONGLONG\n");
				  free($1);
             }
	| CONSTANT_FLOAT
             {
                  $$ = new primary_expression_constant_float($1);
                  dprintf("CONSTANT_FLOAT\n");
				  free($1);
             }
	| CONSTANT_DOUBLE
             {
                  $$ = new primary_expression_constant_double($1);
                  dprintf("CONSTANT_DOUBLE\n");
				  free($1);
             }
	| CONSTANT_LONG_DOUBLE
             {
                  $$ = new primary_expression_constant_long_double($1);
                  dprintf("CONSTANT_LONG_DOUBLE\n");
				  free($1);
             }
	| STRING_LITERAL
             {
                  $$ = new primary_expression_string($1);
                  dprintf("STRING_LITERAL\n");
             }
	| '(' expression ')'
             {
                  $$ = new primary_expression_expression($2);
                  dprintf("( expression )\n");
             }
	;

call_expression
	: postfix_expression '(' ')'
             {
                  $$ = new call_expression_postfix_expression_empty_paren($1);
                  dprintf("postfix_expression ( )\n");
             }
	| postfix_expression '(' argument_expression_list ')'
             {
                  $$ = new call_expression_postfix_expression_paren_argument_expression_list($1,$3);
                  dprintf("postfix_expression ( argument_expression_list )\n");
             }
	| postfix_expression '(' ')' PRT_ALSO PRT_CUTS PRT_TO '(' identifier_list ')'
             {
                  $$ = new call_expression_postfix_expression_empty_paren($1,$8);
                  dprintf("postfix_expression ( )\n");
             }
	| postfix_expression '(' argument_expression_list ')' PRT_ALSO PRT_CUTS PRT_TO '(' identifier_list ')'
             {
                  $$ = new call_expression_postfix_expression_paren_argument_expression_list($1,$3,$9);
                  dprintf("postfix_expression ( argument_expression_list )\n");
             }
	| postfix_expression '(' ')' PRT_ALSO PRT_CUTS PRT_TO IDENTIFIER
             {
                  $$ = new call_expression_postfix_expression_empty_paren($1,new identifier_list_identifier($7));
                  dprintf("postfix_expression ( )\n");
             }
	| postfix_expression '(' argument_expression_list ')' PRT_ALSO PRT_CUTS PRT_TO IDENTIFIER
             {
                  $$ = new call_expression_postfix_expression_paren_argument_expression_list($1,$3,new identifier_list_identifier($8));
                  dprintf("postfix_expression ( argument_expression_list )\n");
             }
    | BUILTIN_OFFSETOF '(' type_name ',' IDENTIFIER ')'
             {
                  $$ = new call_expression_builtin_offsetof_type_name_identifier($3,$5);
                  dprintf("__builtin_offsetof\n");
             }
        ;
        
postfix_expression
	: primary_expression
             {
                  $$ = new postfix_expression_primary_expression($1);
                  dprintf("primary_expression\n");
             }
	| postfix_expression '[' expression ']'
             {
                  $$ = new postfix_expression_postfix_expression_brace_expression($1,$3);
                  dprintf("postfix_expression [ expression ]\n");
             }
	| call_expression
             {
                  $$ = new postfix_expression_call_expression($1);
                  dprintf("call_expression\n");
             }
	| postfix_expression '.' IDENTIFIER
             {
                  $$ = new postfix_expression_postfix_expression_dot_identifier($1,$3->get_name());
                  dprintf("postfix_expression . IDENTIFIER=%s\n",$3->get_name());
             }
	| postfix_expression PTR_OP IDENTIFIER
             {
                  $$ = new postfix_expression_postfix_expression_ptr_op_identifier($1,$3->get_name());
                  dprintf("postfix_expression PTR_OP IDENTIFIER=%s\n",$3->get_name());
             }
	| postfix_expression INC_OP
             {
                  $$ = new postfix_expression_postfix_expression_inc_op($1);
                  dprintf("postfix_expression INC_OP\n");
             }
	| postfix_expression DEC_OP
             {
                  $$ = new postfix_expression_postfix_expression_dec_op($1);
                  dprintf("postfix_expression DEC_OP\n");
             }
	| '(' type_name ')' '{' initializer_list '}'
             {
                  $$ = new postfix_expression_type_name_initializer_list($2,$5);
                  dprintf("( type_name ) { initializer_list }\n");
             }
	| '(' type_name ')' '{' initializer_list ',' '}'
             {
                  $$ = new postfix_expression_type_name_initializer_list_comma($2,$5);
                  dprintf("( type_name ) { initializer_list , }\n");
             }
	;

argument_expression_list
	: assignment_expression
             {
                  $$ = new argument_expression_list_assignment_expression($1);
                  dprintf("assignment_expression\n");
             }
	| argument_expression_list ',' assignment_expression
             {
                  $$ = new argument_expression_list_argument_expression_list_assignment_expression($1,$3);
                  dprintf("argument_expression_list , assignment_expression\n");
             }
	;

unary_expression
	: postfix_expression
             {
                  dprintf("postfix_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new unary_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new unary_expression_postfix_expression($1);
                      }
                  } else {
                      $$ = new unary_expression_postfix_expression($1);
                  }
             }
	| INC_OP unary_expression
             {
                  $$ = new unary_expression_inc_unary_expression($2);
                  dprintf("INC_OP unary_expression\n");
             }
	| DEC_OP unary_expression
             {
                  $$ = new unary_expression_dec_unary_expression($2);
                  dprintf("DEC_OP unary_expression\n");
             }
	| unary_operator cast_expression
             {
                  $$ = new unary_expression_unary_operator_cast_expression($1,$2);
                  dprintf("unary_operator cast_expression\n");
             }
	| SIZEOF unary_expression
             {
                  $$ = new unary_expression_sizeof_unary_expression($2);
                  dprintf("SIZEOF unary_expression\n");
             }
	| SIZEOF '(' type_name ')'
             {
                  $$ = new unary_expression_sizeof_type_name($3);
                  dprintf("SIZEOF ( type_name )\n");
             }
	;

unary_operator
	: '&'
             {
                  $$ = new unary_operator_and();
                  dprintf("&\n");
             }
	| '*'
             {
                  $$ = new unary_operator_star();
                  dprintf("*\n");
             }
	| '+'
             {
                  $$ = new unary_operator_plus();
                  dprintf("+\n");
             }
	| '-'
             {
                  $$ = new unary_operator_minus();
                  dprintf("-\n");
             }
	| '~'
             {
                  $$ = new unary_operator_tilde();
                  dprintf("~\n");
             }
	| '!'
             {
                  $$ = new unary_operator_bang();
                  dprintf("!\n");
             }
	;

cast_expression
	: unary_expression
             {
                  dprintf("unary_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new cast_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new cast_expression_unary_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      if(pe) {
                          $$ = new cast_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new cast_expression_unary_expression($1);
                      }
                  } else {
                      $$ = new cast_expression_unary_expression($1);
                  }
             }
	| '(' type_name ')' cast_expression
             {
                  $$ = new cast_expression_type_name_cast_expression($2,$4);
                  dprintf("( type_name ) cast_expression\n");
             }
	;

multiplicative_expression
	: cast_expression
             {
                  dprintf("cast_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new multiplicative_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new multiplicative_expression_cast_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new multiplicative_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new multiplicative_expression_cast_expression($1);
                      }
                  } else {
                      $$ = new multiplicative_expression_cast_expression($1);
                  }
             }
	| multiplicative_expression '*' cast_expression
             {
                  $$ = new multiplicative_expression_multiplicative_expression_times_cast_expression($1,$3);
                  dprintf("multiplicative_expression * cast_expression\n");
             }
	| multiplicative_expression '/' cast_expression
             {
                  $$ = new multiplicative_expression_multiplicative_expression_div_cast_expression($1,$3);
                  dprintf("multiplicative_expression / cast_expression\n");
             }
	| multiplicative_expression '%' cast_expression
             {
                  $$ = new multiplicative_expression_multiplicative_expression_mod_cast_expression($1,$3);
                  dprintf("multiplicative_expression %% cast_expression\n");
             }
	;

additive_expression
	: multiplicative_expression
             {
                  dprintf("multiplicative_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new additive_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new additive_expression_multiplicative_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new additive_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new additive_expression_multiplicative_expression($1);
                      }
                  } else {
                      $$ = new additive_expression_multiplicative_expression($1);
                  }
             }
	| additive_expression '+' multiplicative_expression
             {
                  $$ = new additive_expression_additive_expression_plus_multiplicative_expression($1,$3);
                  dprintf("additive_expression + multiplicative_expression\n");
             }
	| additive_expression '-' multiplicative_expression
             {
                  $$ = new additive_expression_additive_expression_minus_multiplicative_expression($1,$3);
                  dprintf("additive_expression - multiplicative_expression\n");
             }
	;

shift_expression
	: additive_expression
             {
                  dprintf("additive_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new shift_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new shift_expression_additive_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new shift_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new shift_expression_additive_expression($1);
                      }
                  } else {
                      $$ = new shift_expression_additive_expression($1);
                  }
             }
	| shift_expression LEFT_OP additive_expression
             {
                  $$ = new shift_expression_shift_expression_left_additive_expression($1,$3);
                  dprintf("shift_expression LEFT_OP additive_expression\n");
             }
	| shift_expression RIGHT_OP additive_expression
             {
                  $$ = new shift_expression_shift_expression_right_additive_expression($1,$3);
                  dprintf("shift_expression RIGHT_OP additive_expression\n");
             }
	;

relational_expression
	: shift_expression
             {
                  dprintf("shift_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new relational_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new relational_expression_shift_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new relational_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new relational_expression_shift_expression($1);
                      }
                  } else {
                      $$ = new relational_expression_shift_expression($1);
                  }
             }
	| relational_expression '<' shift_expression
             {
                  $$ = new relational_expression_relational_expression_less_shift_expression($1,$3);
                  dprintf("relational_expression < shift_expression\n");
             }
	| relational_expression '>' shift_expression
             {
                  $$ = new relational_expression_relational_expression_right_shift_expression($1,$3);
                  dprintf("relational_expression > shift_expression\n");
             }
	| relational_expression LE_OP shift_expression
             {
                  $$ = new relational_expression_relational_expression_le_shift_expression($1,$3);
                  dprintf("relational_expression LE_OP shift_expression\n");
             }
	| relational_expression GE_OP shift_expression
             {
                  $$ = new relational_expression_relational_expression_ge_shift_expression($1,$3);
                  dprintf("relational_expression GE_OP shift_expression\n");
             }
	;

equality_expression
	: relational_expression
             {
                  dprintf("relational_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new equality_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new equality_expression_relational_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new equality_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new equality_expression_relational_expression($1);
                      }
                  } else {
                      $$ = new equality_expression_relational_expression($1);
                  }
             }
	| equality_expression EQ_OP relational_expression
             {
                  $$ = new equality_expression_equality_expression_eq_op_relational_expression($1,$3);
                  dprintf("equality_expression EQ_OP relational_expression\n");
             }
	| equality_expression NE_OP relational_expression
             {
                  $$ = new equality_expression_equality_expression_ne_op_relational_expression($1,$3);
                  dprintf("equality_expression NE_OP relational_expression\n");
             }
	;

and_expression
	: equality_expression
             {
                  dprintf("equality_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new and_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new and_expression_equality_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new and_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new and_expression_equality_expression($1);
                      }
                  } else {
                      $$ = new and_expression_equality_expression($1);
                  }
             }
	| and_expression '&' equality_expression
             {
                  $$ = new and_expression_and_expression_equality_expression($1,$3);
                  dprintf("and_expression & equality_expression\n");
             }
	;

exclusive_or_expression
	: and_expression
             {
                  dprintf("and_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new exclusive_or_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new exclusive_or_expression_and_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new exclusive_or_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new exclusive_or_expression_and_expression($1);
                      }
                  } else {
                      $$ = new exclusive_or_expression_and_expression($1);
                  }
             }
	| exclusive_or_expression '^' and_expression
             {
                  $$ = new exclusive_or_expression_exclusive_or_expression_and_expression($1,$3);
                  dprintf("exclusive_or_expression ^ and_expression\n");
             }
	;

inclusive_or_expression
	: exclusive_or_expression
             {
                  dprintf("exclusive_or_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new inclusive_or_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new inclusive_or_expression_exclusive_or_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new inclusive_or_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new inclusive_or_expression_exclusive_or_expression($1);
                      }
                  } else {
                      $$ = new inclusive_or_expression_exclusive_or_expression($1);
                  }
             }
	| inclusive_or_expression '|' exclusive_or_expression
             {
                  $$ = new inclusive_or_expression_inclusive_or_expression_exclusive_or_expression($1,$3);
                  dprintf("inclusive_or_expression | exclusive_or_expression\n");
             }
	;

logical_and_expression
	: inclusive_or_expression
             {
                  dprintf("inclusive_or_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new logical_and_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new logical_and_expression_inclusive_or_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new logical_and_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new logical_and_expression_inclusive_or_expression($1);
                      }
                  } else {
                      $$ = new logical_and_expression_inclusive_or_expression($1);
                  }
             }
	| logical_and_expression AND_OP inclusive_or_expression
             {
                  $$ = new logical_and_expression_logical_and_expression_inclusive_or_expression($1,$3);
                  dprintf("logical_and_expression AND_OP inclusive_or_expression\n");
             }
	;

logical_or_expression
	: logical_and_expression
             {
                  dprintf("logical_and_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new logical_or_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new logical_or_expression_logical_and_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new logical_or_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new logical_or_expression_logical_and_expression($1);
                      }
                  } else {
                      $$ = new logical_or_expression_logical_and_expression($1);
                  }
             }
	| logical_or_expression OR_OP logical_and_expression
             {
                  $$ = new logical_or_expression_logical_or_expression_logical_and_expression($1,$3);
                  dprintf("logical_or_expression OR_OP logical_and_expression\n");
             }
	;

conditional_expression
	: logical_or_expression
             {
                  dprintf("logical_or_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new conditional_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new conditional_expression_logical_or_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new conditional_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new conditional_expression_logical_or_expression($1);
                      }
                  } else {
                      $$ = new conditional_expression_logical_or_expression($1);
                  }
             }
	| logical_or_expression '?' expression ':' conditional_expression
             {
                  $$ = new conditional_expression_logical_or_expression_expression_conditional_expression($1,$3,$5);
                  dprintf("logical_or_expression expression conditional_expression\n");
             }
	;

assignment_expression
	: conditional_expression
             {
                  dprintf("conditional_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new assignment_expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new assignment_expression_conditional_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new assignment_expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new assignment_expression_conditional_expression($1);
                      }
                  } else {
                      $$ = new assignment_expression_conditional_expression($1);
                  }
             }
	| unary_expression assignment_operator assignment_expression
             {
                  $$ = new assignment_expression_unary_expression_assignment_operator_assignment_expression($1,$2,$3);
                  dprintf("unary_expression assignment_operator assignment_expression\n");
             }
	| multiple_ret_expr '=' call_expression
             {
                  $$ = new assignment_expression_multiple_ret_expr_equal_postfix_expression($1,new postfix_expression_call_expression($3));
                  dprintf("multiple_ret_expr = call_expression\n");
             }
	;

assignment_operator
	: '='
             {
                  $$ = new assignment_operator_equal();
                  dprintf("=\n");
             }
	| MUL_ASSIGN
             {
                  $$ = new assignment_operator_mul();
                  dprintf("*=\n");
             }
	| DIV_ASSIGN
             {
                  $$ = new assignment_operator_div();
                  dprintf("/=\n");
             }
	| MOD_ASSIGN
             {
                  $$ = new assignment_operator_mod();
                  dprintf("%%=\n");
             }
	| ADD_ASSIGN
             {
                  $$ = new assignment_operator_add();
                  dprintf("+=\n");
             }
	| SUB_ASSIGN
             {
                  $$ = new assignment_operator_sub();
                  dprintf("-=\n");
             }
	| LEFT_ASSIGN
             {
                  $$ = new assignment_operator_left();
                  dprintf("<<=\n");
             }
	| RIGHT_ASSIGN
             {
                  $$ = new assignment_operator_right();
                  dprintf(">>=\n");
             }
	| AND_ASSIGN
             {
                  $$ = new assignment_operator_and();
                  dprintf("&=\n");
             }
	| XOR_ASSIGN
             {
                  $$ = new assignment_operator_xor();
                  dprintf("^=\n");
             }
	| OR_ASSIGN
             {
                  $$ = new assignment_operator_or();
                  dprintf("|=\n");
             }
	;

expression
	: assignment_expression
             {
                  dprintf("assignment_expression\n");
                  if(g_short_mainline == 2) {
                      AST_node *nonmain = $1->get_mainline();
                      if(nonmain != $1) {
                          assert(nonmain->get_parent() == $1);
                          $$ = new expression_mainline(nonmain);
                          $1->replace(nonmain,NULL);
                          delete $1;
                      } else {
                          $$ = new expression_assignment_expression($1);
                      }
                  } else if(g_short_mainline == 1) {
                      postfix_expression_primary_expression *pe = NULL;
                      if(g_short_mainline) {
                          pe = (postfix_expression_primary_expression*)$1->is_mainline(typeid(postfix_expression_primary_expression));
                      }
                      if(pe) {
                          $$ = new expression_postfix_expression(pe);
                          $1->replace(pe,NULL);
                          delete $1;
                      } else {
                          $$ = new expression_assignment_expression($1);
                      }
                  } else {
                      $$ = new expression_assignment_expression($1);
                  }
             }
	| expression ',' assignment_expression
             {
                  $$ = new expression_expression_assignment_expression($1,$3);
                  dprintf("expression , assignment_expression\n");
             }
	;

constant_expression
	: conditional_expression
             {
                  $$ = new constant_expression_conditional_expression($1);
                  dprintf("conditional_expression\n");
             }
	;

attr_or_uuasmuu
        : GCC_ATTRIBUTE
             {
                 $$ = new attr_or_uuasmuu_attribute($1);
				 free($1);
             }
        | attr_or_uuasmuu GCC_ATTRIBUTE
             {
                 $$ = new attr_or_uuasmuu_attr_or_uuasmuu_attribute($1,$2);
				 free($2);
             }
        | UUASMUU
             {
                 $$ = new attr_or_uuasmuu_uuasmuu($1);
				 free($1);
             }
        | attr_or_uuasmuu UUASMUU
             {
                 $$ = new attr_or_uuasmuu_attr_or_uuasmuu_uuasmuu($1,$2);
				 free($2);
             }
        ;

declaration
	: declaration_specifiers ';'
             {
                  $$ = new declaration_declaration_specifiers($1);
                  dprintf("declaration_specifiers\n");
                  declaration_specifiers *ds = $1;
                  ds->add_to_symbol_table();
             }
	| declaration_specifiers init_declarator_list ';'
             {
                  $$ = new declaration_declaration_specifiers_init_declarator_list($1,$2,"",NULL);
                  dprintf("declaration_specifiers init_declarator_list\n");

                  declaration_specifiers *ds = $1;
                  bool is_typedef = ds->is_typedef();
                  init_declarator_list *idl = $2;
                  $$->m_ii = idl->add_to_symbol_table(is_typedef);
                  unset_type_into_identifier();

                  std::list<std::pair<ii_ptr,declarator *> >::iterator ii_iter;
                  for(ii_iter  = $$->m_ii.begin();
                      ii_iter != $$->m_ii.end();
                    ++ii_iter) {
                      ii_ptr iitemp = (*ii_iter).first;
                      iitemp->set_declarator((*ii_iter).second);
                      if(iitemp->is_var() || iitemp->is_function_var()) {
                          type_specifier *ts = $1->get_type_specifier();
                          iitemp->set_type(ts);
                      }
                  }
             }
	| declaration_specifiers init_declarator_list attr_or_uuasmuu ';'
             {
                  $$ = new declaration_declaration_specifiers_init_declarator_list($1,$2,"",$3);
                  dprintf("declaration_specifiers init_declarator_list\n");

                  declaration_specifiers *ds = $1;
                  bool is_typedef = ds->is_typedef();
                  init_declarator_list *idl = $2;
                  $$->m_ii = idl->add_to_symbol_table(is_typedef);
                  unset_type_into_identifier();
//                  $$->set_symbol_table(ii);

                  std::list<std::pair<ii_ptr,declarator *> >::iterator ii_iter;
                  for(ii_iter  = $$->m_ii.begin();
                      ii_iter != $$->m_ii.end();
                    ++ii_iter) {
                      ii_ptr iitemp = (*ii_iter).first;
                      iitemp->set_declarator((*ii_iter).second);
                      if(iitemp->is_var()) {
                          type_specifier *ts = $1->get_type_specifier();
                          iitemp->set_type(ts);
                      }
                  }
             }
/*
	| declaration_specifiers init_declarator_list GCC_ATTRIBUTE ';'
             {
                  $$ = new declaration_declaration_specifiers_init_declarator_list($1,$2,"",$3);
                  dprintf("declaration_specifiers init_declarator_list\n");

                  declaration_specifiers *ds = $1;
                  bool is_typedef = ds->is_typedef();
//                  printf("typedef = %d\n",is_typedef);
                  init_declarator_list *idl = $2;
                  $$->m_ii = idl->add_to_symbol_table(is_typedef);
                  unset_type_into_identifier();
//                  $$->set_symbol_table(ii);

                  std::list<std::pair<ii_ptr,declarator *> >::iterator ii_iter;
                  for(ii_iter  = $$->m_ii.begin();
                      ii_iter != $$->m_ii.end();
                    ++ii_iter) {
                      ii_ptr iitemp = (*ii_iter).first;
                      iitemp->set_declarator((*ii_iter).second);
                      if(iitemp->is_var()) {
                          type_specifier *ts = $1->get_type_specifier();
                          iitemp->set_type(ts);
                      }
                  }
             }
*/
/*
	| declaration_specifiers GCC_ATTRIBUTE init_declarator_list ';'
             {
                  $$ = new declaration_declaration_specifiers_init_declarator_list($1,$3,$2,"");
                  dprintf("declaration_specifiers init_declarator_list\n");

                  declaration_specifiers *ds = $1;
                  bool is_typedef = ds->is_typedef();
//                  printf("typedef = %d\n",is_typedef);
                  init_declarator_list *idl = $3;
                  $$->m_ii = idl->add_to_symbol_table(is_typedef);
                  unset_type_into_identifier();
//                  $$->set_symbol_table(ii);

                  std::list<std::pair<ii_ptr,declarator *> >::iterator ii_iter;
                  for(ii_iter  = $$->m_ii.begin();
                      ii_iter != $$->m_ii.end();
                    ++ii_iter) {
                      ii_ptr iitemp = (*ii_iter).first;
                      iitemp->set_declarator((*ii_iter).second);
                      if(iitemp->is_var()) {
                          type_specifier *ts = $1->get_type_specifier();
                          iitemp->set_type(ts);
                      }
                  }
             }
	| declaration_specifiers GCC_ATTRIBUTE init_declarator_list GCC_ATTRIBUTE ';'
             {
                  $$ = new declaration_declaration_specifiers_init_declarator_list($1,$3,$2,$4);
                  dprintf("declaration_specifiers init_declarator_list\n");

                  declaration_specifiers *ds = $1;
                  bool is_typedef = ds->is_typedef();
//                  printf("typedef = %d\n",is_typedef);
                  init_declarator_list *idl = $3;
                  $$->m_ii = idl->add_to_symbol_table(is_typedef);
                  unset_type_into_identifier();
//                  $$->set_symbol_table(ii);

                  std::list<std::pair<ii_ptr,declarator *> >::iterator ii_iter;
                  for(ii_iter  = $$->m_ii.begin();
                      ii_iter != $$->m_ii.end();
                    ++ii_iter) {
                      ii_ptr iitemp = (*ii_iter).first;
                      iitemp->set_declarator((*ii_iter).second);
                      if(iitemp->is_var()) {
                          type_specifier *ts = $1->get_type_specifier();
                          iitemp->set_type(ts);
                      }
                  }
             }
*/
	;

ts_comma_list
    : type_name
             {
                  $$ = new ts_comma_list_ts($1);
             }
    | type_name ':' {unset_type_into_identifier();} ts_comma_list
             {
                  $$ = new ts_comma_list_ts_ts_comma_list($1,$4);
             }
    ;

multiple_ret_value
    : MULT_START type_name ':' {unset_type_into_identifier();} ts_comma_list MULT_END
             {
                  $$ = new multiple_ret_value_ts($2,$5);
             }
    ;

declaration_specifiers
	: storage_class_specifier
             {
                  $$ = new declaration_specifiers_storage_class_specifier($1);
                  dprintf("storage_class_specifier\n");
             }
	| storage_class_specifier declaration_specifiers
             {
                  $$ = new declaration_specifiers_storage_class_specifier_declaration_specifiers($1,$2);
                  dprintf("storage_class_specifier declaration_specifiers\n");
             }
	| type_specifier
             {
                  $$ = new declaration_specifiers_type_specifier($1);
                  dprintf("type_specifier\n");
             }
	| type_specifier declaration_specifiers
             {
				  if(dynamic_cast<type_specifier_SHORT*>($1)) {
					  // simplify "short int" to just "short"
					  if($2->is_this_int()) {
							declaration_specifiers *new_ds = $2->remove_first();
							delete $2;

							if(new_ds) {
							    $$ = new declaration_specifiers_type_specifier_declaration_specifiers($1,new_ds);
							    dprintf("type_specifier declaration_specifiers\n");
							} else {
						        $$ = new declaration_specifiers_type_specifier($1);
			                    dprintf("type_specifier declaration_specifiers\n");
							}
							break;
					  }
				  }
				  if(dynamic_cast<type_specifier_LONG*>($1)) {
				      // use one internal type of "long long"
					  if($2->is_this_long()) {
							type_specifier *new_ts = new type_specifier_LONG_LONG();
							declaration_specifiers *new_ds = $2->remove_first();
							delete $1;
							delete $2;

							if(new_ds) {
							    $$ = new declaration_specifiers_type_specifier_declaration_specifiers(new_ts,new_ds);
							    dprintf("type_specifier declaration_specifiers\n");
							} else {
						        $$ = new declaration_specifiers_type_specifier(new_ts);
			                    dprintf("type_specifier declaration_specifiers\n");
							}
							break;
				      }
					  // convert to one internal type for "long double"
					  if($2->is_this_double()) {
							type_specifier *new_ts = new type_specifier_LONG_DOUBLE();
							declaration_specifiers *new_ds = $2->remove_first();
							delete $1;
							delete $2;

							if(new_ds) {
							    $$ = new declaration_specifiers_type_specifier_declaration_specifiers(new_ts,new_ds);
							    dprintf("type_specifier declaration_specifiers\n");
							} else {
						        $$ = new declaration_specifiers_type_specifier(new_ts);
			                    dprintf("type_specifier declaration_specifiers\n");
							}
							break;
					  }
					  // simplify "long int" to just "long"
					  if($2->is_this_int()) {
							declaration_specifiers *new_ds = $2->remove_first();
							delete $2;

							if(new_ds) {
							    $$ = new declaration_specifiers_type_specifier_declaration_specifiers($1,new_ds);
							    dprintf("type_specifier declaration_specifiers\n");
							} else {
						        $$ = new declaration_specifiers_type_specifier($1);
			                    dprintf("type_specifier declaration_specifiers\n");
							}
							break;
					  }
				  }
                  $$ = new declaration_specifiers_type_specifier_declaration_specifiers($1,$2);
                  dprintf("type_specifier declaration_specifiers\n");
             }
    | multiple_ret_value
             {
                  $$ = new declaration_specifiers_mrv($1);
                  dprintf("multiple return types\n");
             }
    | multiple_ret_value declaration_specifiers
             {
                  $$ = new declaration_specifiers_mrv_declaration_specifiers($1,$2);
                  dprintf("multiple return types declaration_specifiers\n");
             }
	| type_qualifier
             {
                  $$ = new declaration_specifiers_type_qualifier($1);
                  dprintf("type_qualifier\n");
             }
	| type_qualifier declaration_specifiers
             {
                  $$ = new declaration_specifiers_type_qualifier_declaration_specifiers($1,$2);
                  dprintf("type_qualifier declaration_specifiers\n");
             }
	| function_specifier
             {
                  $$ = new declaration_specifiers_function_specifier($1);
                  dprintf("function_specifier\n");
             }
	| function_specifier declaration_specifiers
             {
                  $$ = new declaration_specifiers_function_specifier_declaration_specifiers($1,$2);
                  dprintf("function_specifier declaration_specifiers\n");
             }
    | ALIGN512
	         {
                  $$ = new declaration_specifiers_gcc_attribute("__attribute__((align((512))))");
			 }
    | ALIGN512 declaration_specifiers
	         {
                  $$ = new declaration_specifiers_gcc_attribute_declaration_specifiers("__attribute__((aligned(512)))",$2);
			 }
    | ALIGN256
	         {
                  $$ = new declaration_specifiers_gcc_attribute("__attribute__((align((256))))");
			 }
    | ALIGN256 declaration_specifiers
	         {
                  $$ = new declaration_specifiers_gcc_attribute_declaration_specifiers("__attribute__((aligned(256)))",$2);
			 }
    | ALIGN128
	         {
                  $$ = new declaration_specifiers_gcc_attribute("__attribute__((align((128))))");
			 }
    | ALIGN128 declaration_specifiers
	         {
                  $$ = new declaration_specifiers_gcc_attribute_declaration_specifiers("__attribute__((aligned(128)))",$2);
			 }
    | ALIGN64
	         {
                  $$ = new declaration_specifiers_gcc_attribute("__attribute__((align((64))))");
			 }
    | ALIGN64 declaration_specifiers
	         {
                  $$ = new declaration_specifiers_gcc_attribute_declaration_specifiers("__attribute__((aligned(64)))",$2);
			 }
    | ALIGN32
	         {
                  $$ = new declaration_specifiers_gcc_attribute("__attribute__((align((32))))");
			 }
    | ALIGN32 declaration_specifiers
	         {
                  $$ = new declaration_specifiers_gcc_attribute_declaration_specifiers("__attribute__((aligned(32)))",$2);
			 }
    | ALIGN16
	         {
                  $$ = new declaration_specifiers_gcc_attribute("__attribute__((align((16))))");
			 }
    | ALIGN16 declaration_specifiers
	         {
                  $$ = new declaration_specifiers_gcc_attribute_declaration_specifiers("__attribute__((aligned(16)))",$2);
			 }
    | ALIGN8
	         {
                  $$ = new declaration_specifiers_gcc_attribute("__attribute__((align((8))))");
			 }
    | ALIGN8 declaration_specifiers
	         {
                  $$ = new declaration_specifiers_gcc_attribute_declaration_specifiers("__attribute__((aligned(8)))",$2);
			 }
    | ALIGN4
	         {
                  $$ = new declaration_specifiers_gcc_attribute("__attribute__((align((4))))");
			 }
    | ALIGN4 declaration_specifiers
	         {
                  $$ = new declaration_specifiers_gcc_attribute_declaration_specifiers("__attribute__((aligned(4)))",$2);
			 }
/*
	| GCC_ATTRIBUTE
             {
                  $$ = new declaration_specifiers_gcc_attribute($1);
                  dprintf("gcc_attribute\n");
             }
	| GCC_ATTRIBUTE declaration_specifiers
             {
                  $$ = new declaration_specifiers_gcc_attribute_declaration_specifiers($1,$2);
                  dprintf("gcc_attribute declaration_specifiers\n");
             }
*/
	;

init_declarator_list
	: init_declarator
             {
                  $$ = new init_declarator_list_init_declarator($1);
                  dprintf("init_declarator\n");
             }
	| init_declarator_list ',' init_declarator
             {
                  $$ = new init_declarator_list_init_declarator_comma($1,$3);
                  dprintf("init_declarator_list , init_declarator\n");
             }
	;

init_declarator
	: declarator
             {
                  $$ = new init_declarator_declarator($1);
                  dprintf("declarator\n");
             }
	| declarator '=' initializer
             {
                  $$ = new init_declarator_declarator_initializer($1,$3);
                  dprintf("declarator = initializer\n");
             }
	;

storage_class_specifier
	: TYPEDEF
             {
                  $$ = new storage_class_specifier_TYPEDEF();
                  dprintf("TYPEDEF\n");
             }
	| EXTERN
             {
                  $$ = new storage_class_specifier_EXTERN();
                  dprintf("EXTERN\n");
             }
	| STATIC
             {
                  $$ = new storage_class_specifier_STATIC();
                  dprintf("STATIC\n");
             }
	| AUTO
             {
                  $$ = new storage_class_specifier_AUTO();
                  dprintf("AUTO\n");
             }
	| REGISTER
             {
                  $$ = new storage_class_specifier_REGISTER();
                  dprintf("REGISTER\n");
             }
	;

call_conv_specifier
        : CDECL 
             { 
                  $$ = new call_conv_specifier_CDECL(); 
                  dprintf("CDECL\n"); 
             }
        | UCDECL 
             { 
                  $$ = new call_conv_specifier_UCDECL(); 
                  dprintf("UCDECL\n"); 
             }
        | STDCALL 
             {
                  $$ = new call_conv_specifier_STDCALL(); 
                  dprintf("STDCALL\n"); 
             }
        | STDCALL STDCALL
             {
                  $$ = new call_conv_specifier_STDCALL(); 
                  dprintf("STDCALL\n"); 
             }
        | PASCAL     
             { 
                  $$ = new call_conv_specifier_PASCAL(); 
                  dprintf("PASCAL\n"); 
             }
        | PRT_PDECL 
             { 
                  $$ = new call_conv_specifier_PRT_PDECL(); 
                  dprintf("PRT_PDECL\n"); 
             }
        | PRT_PCDECL 
             { 
                  $$ = new call_conv_specifier_PRT_PCDECL(); 
                  dprintf("PRT_PCDECL\n"); 
             }
        ;

type_specifier
	: VOID 
             { 
                  $$ = new type_specifier_VOID(); 
                  dprintf("VOID\n"); 
                  set_type_into_identifier();
             }
	| CHAR 
             { 
                  $$ = new type_specifier_CHAR(); 
                  dprintf("CHAR\n"); 
                  set_type_into_identifier();
             }
	| SHORT 
             { 
                  $$ = new type_specifier_SHORT(); 
                  dprintf("SHORT\n"); 
                  set_type_into_identifier();
             }
	| INT 
             { 
                  $$ = new type_specifier_INT(); 
                  dprintf("INT\n"); 
                  set_type_into_identifier();
             }
	| INT64 
             { 
                  $$ = new type_specifier_INT64(); 
                  dprintf("INT64\n"); 
                  set_type_into_identifier();
             }
	| INT32 
             { 
                  $$ = new type_specifier_INT32(); 
                  dprintf("INT32\n"); 
                  set_type_into_identifier();
             }
	| INT16 
             { 
                  $$ = new type_specifier_INT16(); 
                  dprintf("INT16\n"); 
                  set_type_into_identifier();
             }
	| INT8 
             { 
                  $$ = new type_specifier_INT8(); 
                  dprintf("INT8\n"); 
                  set_type_into_identifier();
             }
	| LONG 
             {
                  $$ = new type_specifier_LONG(); 
                  dprintf("LONG\n"); 
                  set_type_into_identifier();
             }
	| FLOAT 
             { 
                  $$ = new type_specifier_FLOAT(); 
                  dprintf("FLOAT\n");
                  set_type_into_identifier();
             }
	| DOUBLE 
             { 
                  $$ = new type_specifier_DOUBLE(); 
                  dprintf("DOUBLE\n"); 
                  set_type_into_identifier();
             }
	| SIGNED 
             { 
                  $$ = new type_specifier_SIGNED(); 
                  dprintf("SIGNED\n"); 
                  set_type_into_identifier();
             }
	| UNSIGNED 
             {  
                  $$ = new type_specifier_UNSIGNED(); 
                  dprintf("UNSIGNED\n"); 
                  set_type_into_identifier();
             }
    | BUILTIN_VA_LIST 
             {  
                  $$ = new type_specifier_BUILTIN_VA_LIST(); 
                  dprintf("BUILTIN_VA_LIST\n"); 
                  set_type_into_identifier();
             }
	| BOOL 
             {   
                  $$ = new type_specifier_BOOL(); 
                  dprintf("BOOL\n"); 
                  set_type_into_identifier();
             }
	| COMPLEX 
             { 
                  $$ = new type_specifier_COMPLEX(); 
                  dprintf("COMPLEX\n"); 
                  set_type_into_identifier();
             }
	| IMAGINARY 
             { 
                  $$ = new type_specifier_IMAGINARY(); 
                  dprintf("IMAGINARY\n"); 
                  set_type_into_identifier();
             }
	| struct_or_union_specifier
             {
                  $$ = new type_specifier_struct_or_union_specifier($1);
                  dprintf("struct_or_union_specifier\n");
                  set_type_into_identifier();
             }
	| enum_specifier
             {
                  $$ = new type_specifier_enum_specifier($1);
                  dprintf("enum_specifier\n");
//                  set_type_into_identifier();
             }
    | TYPE_NAME
             {
                  $$ = new type_specifier_TYPE_NAME($1);
                  dprintf("TYPE_NAME=%s\n",$1->get_name());
                  set_type_into_identifier();
             }
    | TYPE_STRUCT_NAME
             {
                  $$ = new type_specifier_TYPE_NAME($1);
                  dprintf("TYPE_NAME=%s\n",$1->get_name());
                  set_type_into_identifier();
             }
	| PRT_REF 
             {
                  $$ = new type_specifier_PRT_REF(NULL,0); 
                  dprintf("PRT_REF\n"); 
                  set_type_into_identifier();
             }
	| PRT_REF '<' type_specifier '>'
             {
                  $$ = new type_specifier_PRT_REF($3,0); 
                  dprintf("PRT_REF (type)\n"); 
                  set_type_into_identifier();
             }
	| PRT_REF '<' type_specifier ',' CONSTANT_INT '>'
             {
                  $$ = new type_specifier_PRT_REF($3,atoi($5)); 
                  dprintf("PRT_REF (type)\n"); 
                  set_type_into_identifier();
                  delete $5;
             }
    | UUW64 type_specifier
             {
                  $$ = $2;
                  dprintf("__w64 ");
             }
    | continuation_var_type
             {
                  $$ = new type_specifier_continuation($1);
                  dprintf("continuation \n");
                  set_type_into_identifier();
             }
	;

struct_or_union_specifier
	: struct_or_union IDENTIFIER '{' {unset_type_into_identifier(); g_scope_stack.enter_scope();} struct_declaration_list '}' {$<scope>$ = g_scope_stack.leave_scope();}
             {
                  $$ = new struct_or_union_specifier_struct_or_union_identifier_struct_declaration_list($1,$2->get_name(),$5);
                  dprintf("struct_or_union IDENTIFIER=%s { struct_declaration_list  }\n",$2->get_name());
                  Scope *cs_scope = $<scope>7;
                  if(!cs_scope) {
                      printf("Problem.\n");
                      assert(0);
                  }
                  $$->set_scope(cs_scope);
                  $2->set_struct_scope(cs_scope);
             }
	| struct_or_union '{' {unset_type_into_identifier(); g_scope_stack.enter_scope();} struct_declaration_list '}' {$<scope>$ = g_scope_stack.leave_scope();}
             {
                  $$ = new struct_or_union_specifier_struct_or_union_struct_declaration_list($1,$4);
                  dprintf("struct_or_union { struct_declaration_list  }\n");
                  Scope *cs_scope = $<scope>6;
                  if(!cs_scope) {
                      printf("Problem.\n");
                      assert(0);
                  }
                  $$->set_scope(cs_scope);
             }
	| struct_or_union IDENTIFIER {set_type_into_identifier();}
             {
                  $$ = new struct_or_union_specifier_struct_or_union_identifier($1,$2->get_name());
                  dprintf("struct_or_union IDENTIFIER=%s\n",$2->get_name());
             }
	| struct_or_union TYPE_STRUCT_NAME '{' {unset_type_into_identifier(); g_scope_stack.enter_scope();} struct_declaration_list '}' {$<scope>$ = g_scope_stack.leave_scope();}
             {
                  $$ = new struct_or_union_specifier_struct_or_union_identifier_struct_declaration_list($1,$2->get_name(),$5);
                  dprintf("struct_or_union TYPE_STRUCT_NAME=%s { struct_declaration_list  }\n",$2->get_name());
                  Scope *cs_scope = $<scope>7;
                  if(!cs_scope) {
                      printf("Problem.\n");
                      assert(0);
                  }
                  $$->set_scope(cs_scope);
                  $2->set_struct_scope(cs_scope);
             }
	| struct_or_union TYPE_STRUCT_NAME {set_type_into_identifier();}
             {
                  $$ = new struct_or_union_specifier_struct_or_union_identifier($1,$2->get_name());
                  dprintf("struct_or_union TYPE_STRUCT_NAME=%s\n",$2->get_name());
             }
	;

uudeclspec_list
        : UUDECLSPEC '(' declspec_specifier ')'
             {
                  $$ = new uudeclspec_list_uudeclspec($3);
             }
        | uudeclspec_list UUDECLSPEC '(' declspec_specifier ')'
             {
                  $$ = new uudeclspec_list_uudeclspec_list_uudeclspec($1,$4);
             }
        ;

struct_or_union
	: STRUCT
             {
                  $$ = new struct_or_union_STRUCT();
                  dprintf("STRUCT\n");
                  set_type_into_identifier();
             }
	| STRUCT uudeclspec_list
             {
                  $$ = new struct_or_union_STRUCT_declspec($2);
                  dprintf("STRUCT declspec\n");
                  set_type_into_identifier();
             }
	| UNION
             {
                  $$ = new struct_or_union_UNION();
                  dprintf("UNION\n");
             }
	| UNION uudeclspec_list
             {
                  $$ = new struct_or_union_UNION_declspec($2);
                  dprintf("UNION\n");
             }
	;

struct_declaration_list
	: struct_declaration
             {
                  $$ = new struct_declaration_list_struct_declaration($1);
                  dprintf("struct_declaration\n");
                  unset_type_into_identifier();
             }
	| struct_declaration_list struct_declaration
             {
                  $$ = new struct_declaration_list_struct_declaration_list_struct_declaration($1,$2);
                  dprintf("struct_declaration_list struct_declaration\n");
                  unset_type_into_identifier();
             }
	| anonymous_struct_declaration
             {
                  $$ = new struct_declaration_list_anonymous_struct_declaration($1);
                  dprintf("anonymous_struct_declaration\n");
                  unset_type_into_identifier();
             }
	| struct_declaration_list anonymous_struct_declaration
             {
                  $$ = new struct_declaration_list_struct_declaration_list_anonymous_struct_declaration($1,$2);
                  dprintf("struct_declaration_list anonymous_struct_declaration\n");
                  unset_type_into_identifier();
             }
	;

anonymous_struct_declaration
        : specifier_qualifier_list ';'
             {
                  $$ = new anonymous_struct_declaration_specifier_qualifier_list($1);
                  dprintf("specifier_qualifier_list\n");
             }

struct_declaration
	: specifier_qualifier_list struct_declarator_list ';'
             {
                  $$ = new struct_declaration_specifier_qualifier_list_struct_declarator_list($1,$2);
                  dprintf("specifier_qualifier_list struct_declarator_list\n");

                  struct_declarator_list *sdl = $2;
                  std::list<std::pair<ii_ptr ,declarator*> > ii = sdl->add_to_symbol_table();
                  unset_type_into_identifier();

                  std::list<std::pair<ii_ptr ,declarator *> >::iterator ii_iter;
                  for(ii_iter  = ii.begin();
                      ii_iter != ii.end();
                    ++ii_iter) {
                      ii_ptr iitemp = (*ii_iter).first;
                      iitemp->set_declarator((*ii_iter).second);
                      type_specifier *ts = $1->get_type_specifier();
                      iitemp->set_type(ts);
                  }
             }
	| HASH
             {
                  $$ = new struct_declaration_HASH($1);
                  dprintf("HASH=%s\n",$1);
             }
        | POUND_LINE
             {
                  $$ = new struct_declaration_pound_line($1);
                  dprintf("POUND_LINE\n");
				  free($1);
             }
	;

specifier_qualifier_list
	: type_specifier specifier_qualifier_list
             {
				  if(dynamic_cast<type_specifier_LONG*>($1)) {
				      // use one internal type of "long long"
					  if($2->is_this_long()) {
							type_specifier *new_ts = new type_specifier_LONG_LONG();
							specifier_qualifier_list *new_sql = $2->remove_first();
							delete $1;
							delete $2;

							if(new_sql) {
							    $$ = new specifier_qualifier_list_type_specifier_specifier_qualifier_list(new_ts,new_sql);
							    dprintf("type_specifier specifier_qualifier_list\n");
							} else {
						        $$ = new specifier_qualifier_list_type_specifier(new_ts);
			                    dprintf("type_specifier specifier_qualifier_list\n");
							}
							break;
				      }
					  // convert to one internal type for "long double"
					  if($2->is_this_double()) {
							type_specifier *new_ts = new type_specifier_LONG_DOUBLE();
							specifier_qualifier_list *new_sql = $2->remove_first();
							delete $1;
							delete $2;

							if(new_sql) {
							    $$ = new specifier_qualifier_list_type_specifier_specifier_qualifier_list(new_ts,new_sql);
							    dprintf("type_specifier specifier_qualifier_list\n");
							} else {
						        $$ = new specifier_qualifier_list_type_specifier(new_ts);
			                    dprintf("type_specifier specifier_qualifier_list\n");
							}
							break;
					  }
					  // simplify "long int" to just "long"
					  if($2->is_this_int()) {
							specifier_qualifier_list *new_sql = $2->remove_first();
							delete $2;

							if(new_sql) {
							    $$ = new specifier_qualifier_list_type_specifier_specifier_qualifier_list($1,new_sql);
							    dprintf("type_specifier specifier_qualifier_list\n");
							} else {
						        $$ = new specifier_qualifier_list_type_specifier($1);
			                    dprintf("type_specifier specifier_qualifier_list\n");
							}
							break;
					  }
				  }
                  $$ = new specifier_qualifier_list_type_specifier_specifier_qualifier_list($1,$2);
                  dprintf("type_specifier specifier_qualifier_list\n");
             }
	| type_specifier
             {
                  $$ = new specifier_qualifier_list_type_specifier($1);
                  dprintf("type_specifier\n");
             }
	| type_qualifier specifier_qualifier_list
             {
                  $$ = new specifier_qualifier_list_type_qualifier_specifier_qualifier_list($1,$2);
                  dprintf("type_qualifier specifier_qualifier_list\n");
             }
	| type_qualifier
             {
                  $$ = new specifier_qualifier_list_type_qualifier($1);
                  dprintf("type_qualifier\n");
             }
	;

struct_declarator_list
	: struct_declarator
             {
                  $$ = new struct_declarator_list_struct_declarator($1);
                  dprintf("struct_declarator\n");
             }
	| struct_declarator_list ',' struct_declarator
             {
                  $$ = new struct_declarator_list_struct_declarator_list_struct_declarator($1,$3);
                  dprintf("struct_declarator_list struct_declarator\n");
             }
	;

struct_declarator
	: declarator
             {
                  $$ = new struct_declarator_declarator($1);
                  dprintf("declarator\n");
             }
	| ':' constant_expression
             {
                  $$ = new struct_declarator_constant_expression($2);
                  dprintf(": constant_expression\n");
             }
	| declarator ':' constant_expression
             {
                  $$ = new struct_declarator_declarator_constant_expression($1,$3);
                  dprintf("declarator : constant_expression\n");
             }
	;

enum_specifier
	: ENUM '{' enumerator_list '}'
             {
                  $$ = new enum_specifier_enumerator_list($3);
                  dprintf("ENUM { enumerator_list }\n");
             }
	| ENUM IDENTIFIER '{' enumerator_list '}'
             {
                  $$ = new enum_specifier_identifier_enumerator_list($2->get_name(),$4);
                  dprintf("ENUM IDENTIFIER=%s { enumerator_list }\n",$2->get_name());
             }
	| ENUM '{' enumerator_list ',' '}'
             {
                  $$ = new enum_specifier_enumerator_list_comma($3);
                  dprintf("ENUM { enumerator_list , }\n");
             }
/*
	| ENUM '{' enumerator_list ',' POUND_LINE '}'
             {
                  $$ = new enum_specifier_enumerator_list_comma_pound_line($3,$5);
                  dprintf("ENUM { enumerator_list , }\n");
             }
*/
	| ENUM IDENTIFIER '{' enumerator_list ',' '}'
             {
                  $$ = new enum_specifier_identifier_enumerator_list_comma($2->get_name(),$4);
                  dprintf("ENUM IDENTIFIER=%s { enumerator_list , }\n",$2->get_name());
             }
/*
	| ENUM IDENTIFIER '{' enumerator_list ',' POUND_LINE '}'
             {
                  $$ = new enum_specifier_identifier_enumerator_list_comma_pound_line($2->get_name(),$4,$6);
                  dprintf("ENUM IDENTIFIER=%s { enumerator_list , }\n",$2->get_name());
             }
*/
	| ENUM IDENTIFIER
             {
                  $$ = new enum_specifier_identifier($2->get_name());
                  dprintf("ENUM IDENTIFIER=%s \n",$2->get_name());
             }
	;

enumerator_list
	: enumerator
             {
                  $$ = new enumerator_list_enumerator($1);
                  dprintf("enumerator\n");
             }
	| enumerator_list ',' enumerator
             {
                  $$ = new enumerator_list_enumerator_list_enumerator($1,$3);
                  dprintf("enumerator_list , enumerator\n");
             }
	;

enumerator
	: IDENTIFIER
             {
                  $$ = new enumerator_identifier($1->get_name());
                  dprintf("IDENTIFIER=%s\n",$1->get_name());
                  $1->add(IIT_ENUM_LABEL);
             }
	| IDENTIFIER '=' constant_expression
             {
                  $$ = new enumerator_identifier_constant_expression($1->get_name(),$3);
                  dprintf("IDENTIFIER=%s = constant_expression\n",$1->get_name());
                  $1->add(IIT_ENUM_LABEL);
             }
        | POUND_LINE
             {
                  $$ = new enumerator_pound_line($1);
             }
	;

type_qualifier
	: CONST
             {
                  $$ = new type_qualifier_const();
                  dprintf("CONST\n");
             }
	| RESTRICT
             {
                  $$ = new type_qualifier_restrict();
                  dprintf("RESTRICT\n");
             }
	| UURESTRICT
             {
                  $$ = new type_qualifier_uurestrict();
                  dprintf("UURESTRICT\n");
             }
	| VOLATILE
             {
                  $$ = new type_qualifier_volatile();
                  dprintf("VOLATILE\n");
             }
	;

declspec_specifier
        : NORETURN
             {
                  $$ = new declspec_specifier_NORETURN();
                  dprintf("NORETURN\n");
             }
        | ALIGN512
             {
                  $$ = new declspec_specifier_ALIGN512();
                  dprintf("ALIGN512\n");
             }
        | ALIGN256
             {
                  $$ = new declspec_specifier_ALIGN256();
                  dprintf("ALIGN256\n");
             }
        | ALIGN128
             {
                  $$ = new declspec_specifier_ALIGN128();
                  dprintf("ALIGN128\n");
             }
        | ALIGN64
             {
                  $$ = new declspec_specifier_ALIGN64();
                  dprintf("ALIGN64\n");
             }
        | ALIGN32
             {
                  $$ = new declspec_specifier_ALIGN32();
                  dprintf("ALIGN32\n");
             }
        | ALIGN16
             {
                  $$ = new declspec_specifier_ALIGN16();
                  dprintf("ALIGN16\n");
             }
        | ALIGN8
             {
                  $$ = new declspec_specifier_ALIGN8();
                  dprintf("ALIGN8\n");
             }
        | ALIGN4
             {
                  $$ = new declspec_specifier_ALIGN4();
                  dprintf("ALIGN4\n");
             }
        | ALIGN2
             {
                  $$ = new declspec_specifier_ALIGN2();
                  dprintf("ALIGN2\n");
             }
        | ALIGN1
             {
                  $$ = new declspec_specifier_ALIGN1();
                  dprintf("ALIGN1\n");
             }
        | DLLIMPORT
             {
                  $$ = new declspec_specifier_DLLIMPORT();
                  dprintf("DLLIMPORT\n");
             }
        | DLLEXPORT
             {
                  $$ = new declspec_specifier_DLLEXPORT();
                  dprintf("DLLEXPORT\n");
             }
        | NAKED
             {
                  $$ = new declspec_specifier_NAKED();
                  dprintf("NAKED\n");
             }
        | NOALIAS
             {
                  $$ = new declspec_specifier_NOALIAS();
                  dprintf("NOALIAS\n");
             }
        | RESTRICT
             {
                  $$ = new declspec_specifier_RESTRICT();
                  dprintf("RESTRICT\n"); 
             }
        | UURESTRICT
             {
                  $$ = new declspec_specifier_UURESTRICT();
                  dprintf("UURESTRICT\n"); 
             }
        | DEPRECATED '(' STRING_LITERAL ')'
             {
                  $$ = new declspec_specifier_DEPRECATED($3);
                  dprintf("DEPRECATED ( LITERAL )\n");
             }
        | DEPRECATED
             {
                  $$ = new declspec_specifier_DEPRECATED(NULL);
                  dprintf("DEPRECATED\n");
             }
        | NOINLINE
             {
                  $$ = new declspec_specifier_NOINLINE();
                  dprintf("NOINLINE\n");
             }
        | INTRIN_TYPE
             {
                  $$ = new declspec_specifier_INTRIN_TYPE();
                  dprintf("INTRIN_TYPE\n");
             }
        ;

function_specifier
	: INLINE
             {
                  $$ = new function_specifier_INLINE();
                  dprintf("INLINE\n");
             }
	| UINLINE
             {
                  $$ = new function_specifier_UINLINE();
                  dprintf("UINLINE\n");
             }
	| UUINLINE
             {
                  $$ = new function_specifier_UUINLINE();
                  dprintf("UUINLINE\n");
             }
	| UUINLINEUU
             {
                  $$ = new function_specifier_UUINLINEUU();
                  dprintf("UUINLINEUU\n");
	     }
	| FORCEINLINE
             {
                  $$ = new function_specifier_FORCEINLINE();
                  dprintf("FORCEINLINE\n");
             }
    | UUDECLSPEC '(' declspec_specifier ')'
             {
                  $$ = new function_specifier_uudeclspec_specifier($3);
                  dprintf("UUDECLSPEC ( declspec_specifier )\n");
             }
    | UDECLSPEC '(' declspec_specifier ')'
             {
                  $$ = new function_specifier_udeclspec_specifier($3);
                  dprintf("UDECLSPEC ( declspec_specifier )\n");
             }
	;

declarator
	: pointer direct_declarator
             {
                  $$ = new declarator_pointer_direct_declarator($1,$2);
                  dprintf("declarator_pointer_direct_declarator\n");
             }
	| GCC_ATTRIBUTE pointer direct_declarator
             {
                  $$ = new declarator_pointer_direct_declarator($2,$3,$1);
                  dprintf("declarator_pointer_direct_declarator\n");
             }
	| pointer GCC_ATTRIBUTE direct_declarator
             {
                  $$ = new declarator_pointer_direct_declarator($1,$3,$2);
                  dprintf("declarator_pointer_direct_declarator\n");
             }
	| GCC_ATTRIBUTE direct_declarator
             {
                  $$ = new declarator_direct_declarator($2,$1);
                  dprintf("declarator_direct_declarator\n");
             }
	| direct_declarator
             {
                  $$ = new declarator_direct_declarator($1);
                  dprintf("declarator_direct_declarator\n");
             }
	| pointer call_conv_specifier direct_declarator
             {
                  $$ = new declarator_pointer_call_conv_specifier_direct_declarator($1,$2,$3);
                  dprintf("declarator_pointer_call_conv_specifier_direct_declarator\n");
             }
	| call_conv_specifier pointer direct_declarator
             {
                  $$ = new declarator_call_conv_specifier_pointer_direct_declarator($1,$2,$3);
                  dprintf("declarator_call_conv_specifier_pointer_direct_declarator\n");
             }
	| call_conv_specifier direct_declarator
             {
                  $$ = new declarator_call_conv_specifier_direct_declarator($1,$2);
                  dprintf("declarator_call_conv_specifier_direct_declarator\n");
             }
	;


direct_declarator
	: IDENTIFIER
             {
                  $$ = new direct_declarator_IDENTIFIER($1);
                  dprintf("direct_declarator_IDENTIFIER=%s\n",$1->get_name());
//                  unset_type_into_identifier();
             }
	| '(' declarator ')'
             {
                  $$ = new direct_declarator_paren_declarator($2);
                  dprintf("direct_declarator_paren_declarator\n");
             }
/*
	| '(' GCC_ATTRIBUTE declarator ')'
             {
                  $$ = new direct_declarator_paren_declarator($3);
                  dprintf("direct_declarator_paren_declarator\n");
             }
*/
	| direct_declarator '[' type_qualifier_list assignment_expression ']'
             {
                  $$ = new direct_declarator_direct_declarator_type_qualifier_list_assignment_expression($1,$3,$4);
                  dprintf("direct_declarator_direct_declarator_type_qualifier_list_assignment_expression\n");
             }
	| direct_declarator '[' type_qualifier_list ']'
             {
                  $$ = new direct_declarator_direct_declarator_type_qualifier_list($1,$3);
                  dprintf("direct_declarator_direct_declarator_type_qualifier_list\n");
             }
	| direct_declarator '[' assignment_expression ']'
             {
                  $$ = new direct_declarator_direct_declarator_assignment_expression($1,$3);
                  dprintf("direct_declarator_direct_declarator_assignment_expression\n");
             }
	| direct_declarator '[' STATIC type_qualifier_list assignment_expression ']'
             {
                  $$ = new direct_declarator_direct_declarator_STATIC_type_qualifier_list_assignment_expression($1,$4,$5);
                  dprintf("direct_declarator_direct_declarator_STATIC_type_qualifier_list_assignment_expression\n");
             }
	| direct_declarator '[' type_qualifier_list STATIC assignment_expression ']'
             {
                  $$ = new direct_declarator_direct_declarator_type_qualifier_list_STATIC_assignment_expression($1,$3,$5);
                  dprintf("direct_declarator_direct_declarator_type_qualifier_list_STATIC_assignment_expression\n");
             }
	| direct_declarator '[' type_qualifier_list '*' ']'
             {
                  $$ = new direct_declarator_direct_declarator_type_qualifier_list_star($1,$3);
                  dprintf("direct_declarator_direct_declarator_type_qualifier_list_star\n");
             }
	| direct_declarator '[' '*' ']'
             {
                  $$ = new direct_declarator_direct_declarator_star($1);
                  dprintf("direct_declarator_direct_declarator_star\n");
             }
	| direct_declarator '[' ']'
             {
                  $$ = new direct_declarator_direct_declarator_empty_brace($1);
                  dprintf("direct_declarator_direct_declarator_empty_brace\n");
             }
	| direct_declarator '(' { unset_type_into_identifier(); g_scope_stack.enter_scope(); } parameter_type_list ')' { $<scope>$ = g_scope_stack.leave_scope(); }
             {
                  $$ = new direct_declarator_direct_declarator_parameter_type_list($1,$4);
                  dprintf("direct_declarator_direct_declarator_parameter_type_list\n");

                  Scope *cs_scope = $<scope>6;
                  if(!cs_scope) {
                      printf("Problem.\n");
                      assert(0);
                  }
                  $$->set_scope(cs_scope);
             }
	| direct_declarator '(' identifier_list ')'
             {
                  $$ = new direct_declarator_direct_declarator_identifier_list($1,$3);
                  dprintf("direct_declarator_direct_declarator_identifier_list\n");
             }
	| direct_declarator '(' ')'
             {
                  $$ = new direct_declarator_direct_declarator_empty_paren($1);
                  dprintf("direct_declarator_direct_declarator_empty_paren\n");
             }
	;

pointer
	: '*'
             { 
                  $$ = new pointer_star(); 
                  dprintf("*\n"); 
             }
	| '*' type_qualifier_list
             { 
                  $$ = new pointer_star_type_qualifier_list($2); 
                  dprintf("* type_qualifier_list\n"); 
             }
	| '*' pointer
             { 
                  $$ = new pointer_star_pointer($2); 
                  dprintf("* pointer\n"); 
             }
	| '*' type_qualifier_list pointer
             { 
                  $$ = new pointer_star_type_qualifier_list_pointer($2,$3); 
                  dprintf("* type_qualifier_list pointer\n"); 
             }
	| '*' UUPTR64
             { 
                  $$ = new pointer_star(); 
                  dprintf("*\n"); 
             }
	| '*' UUPTR64 type_qualifier_list
             { 
                  $$ = new pointer_star_type_qualifier_list($3); 
                  dprintf("* type_qualifier_list\n"); 
             }
	| '*' UUPTR64 pointer
             { 
                  $$ = new pointer_star_pointer($3); 
                  dprintf("* pointer\n"); 
             }
	| '*' UUPTR64 type_qualifier_list pointer
             { 
                  $$ = new pointer_star_type_qualifier_list_pointer($3,$4); 
                  dprintf("* type_qualifier_list pointer\n"); 
             }
	;

type_qualifier_list
	: type_qualifier
             { 
                  $$ = new type_qualifier_list_type_qualifier($1); 
                  dprintf("type_qualifier\n"); 
             }
	| type_qualifier_list type_qualifier
             { 
                  $$ = new type_qualifier_list_type_qualifier_list_type_qualifier($1,$2); 
                  dprintf("type_qualifier_list type_qualifier\n"); 
             }
	;


parameter_type_list
	: parameter_list
             { 
                  $$ = new parameter_type_list_parameter_list($1); 
                  dprintf("parameter_list\n"); 
             }
	| parameter_list ',' ELLIPSIS
             { 
                  $$ = new parameter_type_list_parameter_list_ellipsis($1); 
                  dprintf("parameter_list, ELLIPSIS\n"); 
             }
	;

parameter_list
	: parameter_declaration
             { 
                  $$ = new parameter_list_parameter_declaration($1); 
                  dprintf("parameter_list_parameter_declaration\n"); 
             }
	| parameter_list ',' parameter_declaration
             { 
                  $$ = new parameter_list_parameter_list_parameter_declaration($1,$3); 
                  dprintf("parameter_list_parameter_list_parameter_declaration\n"); 
             }
	;

parameter_declaration
	: declaration_specifiers declarator
             { 
                  $$ = new parameter_declaration_declaration_specifiers_declarator($1,$2); 
                  dprintf("parameter_declaration_declaration_specifiers_declarator\n"); 
                  unset_type_into_identifier();
                  ii_ptr ii = $2->add_to_symbol_table(IIT_PARAM);
                  $$->set_symbol_table(ii);
                  type_specifier *ts = $1->get_type_specifier();
                  $$->set_type(ts);
                  ii->set_type(ts);
                  ii->set_parameter_declaration($$);
                  ii->set_declarator($2);
             }
	| declaration_specifiers abstract_declarator
             { 
                  $$ = new parameter_declaration_declaration_specifiers_abstract_declarator($1,$2); 
                  dprintf("parameter_declaration_declaration_specifiers_abstract_declarator\n"); 
                  unset_type_into_identifier();
             }
	| declaration_specifiers
             { 
                  $$ = new parameter_declaration_declaration_specifiers($1); 
                  dprintf("parameter_declaration_declaration_specifiers\n"); 
                  unset_type_into_identifier();
             }
	;

identifier_list
	: IDENTIFIER
             { 
                  $$ = new identifier_list_identifier($1); 
                  dprintf("IDENTIFIER=%s\n",$1->get_name()); 
             }
	| identifier_list ',' IDENTIFIER
             { 
                  $$ = new identifier_list_identifier_list_identifier($1,$3); 
                  dprintf("identifier_list , IDENTIFIER=%s\n",$3->get_name()); 
             }
	;

type_name
	: specifier_qualifier_list
             { 
                  $$ = new type_name_specifier_qualifier_list($1); 
                  dprintf("specifier_qualifier_list\n"); 
                  unset_type_into_identifier();
             }
	| specifier_qualifier_list abstract_declarator
             { 
                  $$ = new type_name_specifier_qualifier_list_abstract_declarator($1,$2); 
                  dprintf("specifier_qualifier_list abstract_declarator\n"); 
                  unset_type_into_identifier();
             }
	;

abstract_declarator
	: pointer
             { 
                  $$ = new abstract_declarator_pointer($1); 
                  dprintf("abstract_declarator_pointer\n"); 
             }
	| direct_abstract_declarator
             { 
                  $$ = new abstract_declarator_direct_abstract_declarator($1); 
                  dprintf("abstract_declarator_direct_abstract_declarator\n"); 
             }
	| pointer direct_abstract_declarator
             { 
                  $$ = new abstract_declarator_pointer_direct_abstract_declarator($1,$2); 
                  dprintf("abstract_declarator_pointer_direct_abstract_declarator\n"); 
             }
	| call_conv_specifier pointer
             { 
                  $$ = new abstract_declarator_call_conv_specifier_pointer($1,$2); 
                  dprintf("abstract_declarator_call_conv_specifier_pointer\n"); 
             }
	| call_conv_specifier direct_abstract_declarator
             { 
                  $$ = new abstract_declarator_call_conv_specifier_direct_abstract_declarator($1,$2); 
                  dprintf("abstract_declarator_call_conv_specifier_direct_abstract_declarator\n"); 
             }
	| call_conv_specifier pointer direct_abstract_declarator
             { 
                  $$ = new abstract_declarator_call_conv_specifier_pointer_direct_abstract_declarator($1,$2,$3); 
                  dprintf("abstract_declarator_call_conv_specifier_pointer_direct_abstract_declarator\n"); 
             }
	;

direct_abstract_declarator
	: '(' abstract_declarator ')'
             { 
                  $$ = new direct_abstract_declarator_abstract_declarator($2); 
                  dprintf("direct_abstract_declarator_abstract_declarator\n"); 
                  unset_type_into_identifier();
             }
	| '[' ']'
             { 
                  $$ = new direct_abstract_declarator_empty_braces(); 
                  dprintf("direct_abstract_declarator_empty_braces\n"); 
             }
	| '[' assignment_expression ']'
             { 
                  $$ = new direct_abstract_declarator_assignment_expression($2); 
                  dprintf("direct_abstract_declarator_assignment_expression\n"); 
             }
	| direct_abstract_declarator '[' ']'
             { 
                  $$ = new direct_abstract_declarator_direct_abstract_declarator_empty_braces($1); 
                  dprintf("direct_abstract_declarator_direct_abstract_declarator_empty_braces\n"); 
             }
	| direct_abstract_declarator '[' assignment_expression ']'
             { 
                  $$ = new direct_abstract_declarator_direct_abstract_declarator_assignment_expression($1,$3); 
                  dprintf("direct_abstract_declarator_direct_abstract_declarator_assignment_expression\n"); 
             }
	| '[' '*' ']'
             { 
                  $$ = new direct_abstract_declarator_brace_star(); 
                  dprintf("direct_abstract_declarator_brace_star\n"); 
             }
	| direct_abstract_declarator '[' '*' ']'
             { 
                  $$ = new direct_abstract_declarator_direct_abstract_declarator_brace_star($1); 
                  dprintf("direct_abstract_declarator_direct_abstract_declarator_brace_star\n"); 
             }
	| '(' ')'
             { 
                  $$ = new direct_abstract_declarator_empty_paren(); 
                  dprintf("direct_abstract_declarator_empty_paren\n"); 
             }
	| '(' parameter_type_list ')'
             { 
                  $$ = new direct_abstract_declarator_paren_parameter_type_list($2); 
                  dprintf("direct_abstract_declarator_paren_parameter_type_list\n"); 
             }
	| direct_abstract_declarator '(' ')'
             { 
                  $$ = new direct_abstract_declarator_direct_abstract_declarator_empty_parens($1); 
                  dprintf("direct_abstract_declarator_direct_abstract_declarator_empty_parens\n"); 
             }
	| direct_abstract_declarator '(' parameter_type_list ')'
             { 
                  $$ = new direct_abstract_declarator_direct_abstract_declarator_paren_parameter_type_list($1,$3); 
                  dprintf("direct_abstract_declarator_direct_abstract_declarator_paren_parameter_type_list\n"); 
             }
	;

initializer
	: assignment_expression
             { 
                  $$ = new initializer_assignment_expression($1); 
                  dprintf("assignment_expression\n"); 
             }
	| '{' initializer_list '}'
             { 
                  $$ = new initializer_initializer_list($2); 
                  dprintf("{ initializer_list }\n"); 
             }
	| '{' initializer_list ',' '}'
             { 
                  $$ = new initializer_initializer_list_comma($2); 
                  dprintf("{ initializer_list , }\n"); 
             }
	;

initializer_list
	: initializer
             { 
                  $$ = new initializer_list_initializer($1); 
                  dprintf("initializer\n"); 
             }
	| designation initializer
             { 
                  $$ = new initializer_list_designation_initializer($1,$2); 
                  dprintf("designation initializer\n"); 
             }
	| initializer_list ',' initializer
             { 
                  $$ = new initializer_list_initializer_list_initializer($1,$3); 
                  dprintf("initializer_list , initializer\n"); 
             }
	| initializer_list ',' designation initializer
             { 
                  $$ = new initializer_list_initializer_list_designation_initializer($1,$3,$4); 
                  dprintf("initializer_list , designation initializer\n"); 
             }
	;

designation
	: designator_list '='
             { 
                  $$ = new designation_designator_list($1); 
                  dprintf("designator_list =\n"); 
             }
	;

designator_list
	: designator
             { 
                  $$ = new designator_list_designator($1); 
                  dprintf("designator\n"); 
             }
	| designator_list designator
             { 
                  $$ = new designator_list_designator_list_designator($1,$2); 
                  dprintf("designator_list designator\n"); 
             }
	;

designator
	: '[' constant_expression ']'
             { 
                  $$ = new designator_braces_constant_expression($2); 
                  dprintf("[ constant_expression ]\n"); 
             }
	| '.' IDENTIFIER
             { 
                  $$ = new designator_dot_identifier($2->get_name()); 
                  dprintf(". IDENTIFIER=%s\n",$2->get_name()); 
             }
	;

statement
	: labeled_statement
             { 
                  $$ = new statement_labeled_statement($1); 
                  dprintf("labeled_statement\n"); 
             }
	| {g_scope_stack.enter_scope();} compound_statement {$<scope>$ = g_scope_stack.leave_scope();}
             { 
                  $$ = new statement_compound_statement($2); 
                  dprintf("compound_statement\n"); 
                  Scope *cs_scope = $<scope>3;
                  if(!cs_scope) {
                      printf("Problem.\n");
                      assert(0);
                  }
                  $$->set_scope(cs_scope);
             }
	| expression_statement
             { 
                  $$ = new statement_expression_statement($1); 
                  dprintf("expression_statement\n"); 
             }
	| selection_statement
             { 
                  $$ = new statement_selection_statement($1); 
                  dprintf("selection_statement\n"); 
             }
	| iteration_statement
             { 
                  $$ = new statement_iteration_statement($1); 
                  dprintf("iteration_statement\n"); 
             }
	| jump_statement
             { 
                  $$ = new statement_jump_statement($1); 
                  dprintf("jump_statement\n"); 
             }
	| PRT_TAILCALL call_expression ';'
             { 
                  $$ = new statement_prt_tailcall($2); 
                  dprintf("PRT_TAILCALL call_expression ;\n"); 
             }
	| UUASM
             { 
                  $$ = new statement_UUASM($1); 
                  dprintf("UUASM\n"); 
				  free($1);
             }
	| UUASMUU
             { 
                  $$ = new statement_UUASMUU($1); 
                  dprintf("UUASMUU\n"); 
				  free($1);
             }
	| UASM
             { 
                  $$ = new statement_UASM($1); 
                  dprintf("UASM\n"); 
				  free($1);
             }
	| PRT_NOYIELD {g_scope_stack.enter_scope();} compound_statement {$<scope>$ = g_scope_stack.leave_scope();}
             { 
                  $$ = new statement_PRT_NOYIELD_compound_statement($3); 
                  dprintf("PRT_NOYIELD compound_statement\n"); 

                  Scope *cs_scope = $<scope>4;
                  if(!cs_scope) {
                      printf("Problem.\n");
                      assert(0);
                  }
                  $$->set_scope(cs_scope);
             }
	| PRT_VSE '(' IDENTIFIER ')' {g_scope_stack.enter_scope();} compound_statement {$<scope>$ = g_scope_stack.leave_scope();}
             { 
                  $$ = new statement_PRT_VSE_compound_statement($3,$6); 
                  dprintf("PRT_VSE compound_statement\n"); 

                  Scope *cs_scope = $<scope>7;
                  if(!cs_scope) {
                      printf("Problem.\n");
                      assert(0);
                  }
                  $$->set_scope(cs_scope);
             }
	| PRT_PCALL call_expression ';'
             { 
                  $$ = new statement_pcall_call_expression($2); 
                  dprintf("PCALL postfix_expression ( )\n"); 
             }
	;

type_list
    : type_specifier
            {
                $$ = new type_list_type($1);
            }
    | type_list ',' {unset_type_into_identifier();} type_specifier
            {
                $$ = new type_list_type_list_type($1,$4);
            }
    ;
    
continuation_var_type
    : PRT_CONTINUATION_VAR
            {
                $$ = new continuation_var_type(NULL);
            }
    | PRT_CONTINUATION_VAR '<' '>'
            {
                $$ = new continuation_var_type(NULL);
            }
    | PRT_CONTINUATION_VAR '<' type_list '>'
            {
                $$ = new continuation_var_type($3);
            }
    ;
    
continuation_type
    : PRT_CONTINUATION
            {
                $$ = new continuation_type(NULL);
            }
    | PRT_CONTINUATION '<' type_list '>'
            {
                $$ = new continuation_type($3);
            }
    ;
    
labeled_statement
	: IDENTIFIER ':' {$1->add(IIT_LABEL);} statement
             { 
                  $$ = new labeled_statement_identifier($1->get_name(),$4); 
                  dprintf("IDENTIFIER=%s : statement\n",$1->get_name()); 
             }
	| CASE constant_expression ':' statement
             { 
                  $$ = new labeled_statement_case($2,$4); 
                  dprintf("case constant_expression : statement\n"); 
             }
	| DEFAULT ':' statement
             { 
                  $$ = new labeled_statement_default($3); 
                  dprintf("DEFAULT : statement\n"); 
             }
    | continuation_type IDENTIFIER '(' identifier_list ')' ':' {$2->add(IIT_CONTINUATION);} statement
             { 
                  $$ = new labeled_statement_continuation($1,$2,$4,$8); 
                  dprintf("continuation IDENTIFIER(param_list) : statement\n"); 
                  $2->set_type(new type_specifier_continuation($1->to_continuation_var()),true);
             }
    | continuation_type IDENTIFIER '(' ')' ':' {$2->add(IIT_CONTINUATION);} statement
             { 
                  $$ = new labeled_statement_continuation($1,$2,NULL,$7); 
                  dprintf("continuation IDENTIFIER() : statement\n"); 
                  $2->set_type(new type_specifier_continuation($1->to_continuation_var()),true);
             }
	;

compound_statement
	: '{' '}'
             { 
                  $$ = new compound_statement_empty(); 
                  dprintf("{ }\n"); 
             }
	| '{' block_item_list '}'
             { 
                  $$ = new compound_statement_block_item_list($2); 
                  dprintf("{ block_item_list }\n"); 
             }
	;

block_item_list
	: block_item
             { 
                  $$ = new block_item_list_block_item($1); 
                  dprintf("block_item\n"); 
             }
	| block_item_list block_item
             { 
                  $$ = new block_item_list_block_item_list_block_item($1,$2); 
                  dprintf("block_item_list block_item\n"); 
             }
	;

block_item
	: declaration
             { 
                  $$ = new block_item_declaration($1,yylineno); 
                  dprintf("declaration\n"); 
             }
	| statement
             { 
                  $$ = new block_item_statement($1,yylineno); 
                  dprintf("statement\n"); 
             }
    | POUND_LINE
             {
                  $$ = new block_item_pound_line($1);
             }
	;

expression_statement
	: ';'
             { 
                  $$ = new expression_statement_empty(); 
                  dprintf("empty expression\n"); 
             }
	| expression ';'
             { 
                  $$ = new expression_statement_expression($1); 
                  dprintf("expression\n"); 
             }
	;

selection_statement
	: IF '(' expression ')' statement %prec LOWER_THAN_ELSE
             { 
                  $$ = new selection_statement_if($3,$5); 
                  dprintf("IF\n"); 
             }
	| IF '(' expression ')' statement ELSE statement
             { 
                  $$ = new selection_statement_if_else($3,$5,$7); 
                  dprintf("IFELSE\n"); 
             }
	| IF '(' BUILTIN_EXPECT '(' expression ',' CONSTANT_INT ')' ')' statement
             { 
                  $$ = new selection_statement_if_builtin($5,$7,$10,true); 
                  dprintf("IF builtin_expect\n"); 
             }
/*
	| IF '(' '!' BUILTIN_EXPECT '(' constant_expression ',' CONSTANT_INT ')' ')' statement
             { 
                  $$ = new selection_statement_if_builtin($6,$8,$11,false); 
                  dprintf("IF !builtin_expect\n"); 
             }
*/
	| IF '(' BUILTIN_EXPECT '(' expression ',' CONSTANT_INT ')' ')' statement ELSE statement
             { 
                  $$ = new selection_statement_if_else_builtin($5,$7,$10,$12,true); 
                  dprintf("IF builtin_expect\n"); 
             }
/*
	| IF '(' '!' BUILTIN_EXPECT '(' constant_expression ',' CONSTANT_INT ')' ')' statement ELSE statement
             { 
                  $$ = new selection_statement_if_else_builtin($6,$8,$11,$13,false); 
                  dprintf("IF !builtin_expect\n"); 
             }
*/
	| SWITCH '(' expression ')' statement
             { 
                  $$ = new selection_statement_switch($3,$5); 
                  dprintf("SWITCH\n"); 
             }
	;

iteration_statement
	: WHILE '(' expression ')' statement
             { 
                  $$ = new iteration_statement_while($3,$5); 
                  dprintf("WHILE\n"); 
             }
	| DO statement WHILE '(' expression ')' ';'
             { 
                  $$ = new iteration_statement_do($2,$5); 
                  dprintf("DO\n"); 
             }
	| FOR '(' expression_statement expression_statement ')' statement
             { 
                  $$ = new iteration_statement_for_eses($3,$4,$6); 
                  dprintf("FOR\n"); 
             }
	| FOR '(' expression_statement expression_statement expression ')' statement
             { 
                  $$ = new iteration_statement_for_esese($3,$4,$5,$7); 
                  dprintf("FOR\n"); 
             }
	| FOR '(' declaration expression_statement ')' statement
             { 
                  $$ = new iteration_statement_for_des($3,$4,$6); 
                  dprintf("FOR\n"); 
             }
	| FOR '(' declaration expression_statement expression ')' statement
             { 
                  $$ = new iteration_statement_for_dese($3,$4,$5,$7); 
                  dprintf("FOR\n"); 
             }
	;

ae_list
	: assignment_expression
	         {
                  $$ = new assignment_expression_list_assignment_expression($1);
				  dprintf("ae_list_ae\n");
             }
	| assignment_expression ':' ae_list
	         {
                  $$ = new assignment_expression_list_assignment_expression_assignment_expression_list($1,$3);
				  dprintf("ae_list_ae_ae_list\n");
	         }
	;

multiple_ret_expr
	: MULT_START assignment_expression ':' ae_list MULT_END 
	         {
                  $$ = new multiple_ret_expr_assignment_expression_assignment_expression_list($2,$4);
                  dprintf("multiple_ret_expr\n");
             }
        ;

jump_statement
	: GOTO IDENTIFIER ';'
             { 
                  jump_statement_goto *jsg = new jump_statement_goto($2->get_name()); 
                  $$ = jsg;
                  if($2->is_label()) {
                      jsg->set_backward();
                      dprintf("GOTO IDENTIFIER(backward)=%s\n",$2->get_name()); 
                  } else {
                      dprintf("GOTO IDENTIFIER=%s\n",$2->get_name()); 
                  }
             }
	| CONTINUE ';'
             { 
                  $$ = new jump_statement_continue(); 
                  dprintf("CONTINUE\n"); 
             }
	| BREAK ';'
             { 
                  $$ = new jump_statement_break(); 
                  dprintf("BREAK\n"); 
             }
	| RETURN ';'
             { 
                  $$ = new jump_statement_return(); 
                  dprintf("RETURN\n"); 
             }
	| RETURN expression ';'
             { 
                  $$ = new jump_statement_return_expression($2); 
                  dprintf("RETURN expression\n"); 
             }
	| RETURN multiple_ret_expr ';'
             {
                  $$ = new jump_statement_return_mre($2);
                  dprintf("RETURN multiple\n"); 
             }
	| RETURN PRT_TAILCALL call_expression ';'
             { 
                  $$ = new jump_statement_prt_tailcall($3); 
                  dprintf("RETURN PRT_TAILCALL call_expression ;\n"); 
             }
	| PRT_CUT PRT_TO assignment_expression ';'
             { 
                  $$ = new jump_statement_prt_cut_to_argument_expression_list($3,NULL); 
                  dprintf("PRT_CUT PRT_TO assignment_expression\n"); 
             }
	| PRT_CUT PRT_TO assignment_expression PRT_ALSO PRT_CUTS PRT_TO '(' identifier_list ')' ';'
             { 
                  $$ = new jump_statement_prt_cut_to_argument_expression_list($3,NULL,$8); 
                  dprintf("PRT_CUT PRT_TO assignment_expression also cuts to ( identifier_list )\n"); 
             }
	| PRT_CUT PRT_TO assignment_expression PRT_ALSO PRT_CUTS PRT_TO IDENTIFIER ';'
             { 
                  $$ = new jump_statement_prt_cut_to_argument_expression_list($3,NULL,new identifier_list_identifier($7)); 
                  dprintf("PRT_CUT PRT_TO assignment_expression also cuts to identifier\n"); 
             }
    | PRT_CUT PRT_TO assignment_expression PRT_WITH '(' argument_expression_list ')' ';'
             { 
                  $$ = new jump_statement_prt_cut_to_argument_expression_list($3,$6); 
                  dprintf("PRT_CUT PRT_TO assignment_expression PRT_WITH ( argument_expression_list )\n"); 
             }
    | PRT_CUT PRT_TO assignment_expression PRT_WITH '(' argument_expression_list ')' PRT_ALSO PRT_CUTS PRT_TO '(' identifier_list ')'  ';'
             { 
                  $$ = new jump_statement_prt_cut_to_argument_expression_list($3,$6,$12); 
                  dprintf("PRT_CUT PRT_TO assignment_expression PRT_WITH ( argument_expression_list ) also cuts to ( identifier_list )\n"); 
             }
    | PRT_CUT PRT_TO assignment_expression PRT_WITH '(' argument_expression_list ')' PRT_ALSO PRT_CUTS PRT_TO IDENTIFIER ';'
             { 
                  $$ = new jump_statement_prt_cut_to_argument_expression_list($3,$6,new identifier_list_identifier($11)); 
                  dprintf("PRT_CUT PRT_TO assignment_expression PRT_WITH ( argument_expression_list ) also cuts to identifier\n"); 
             }
	;

translation_unit
	: external_declaration 
      { 
          if($1) {
		      switch(phase_selector) {
				case FULLFILE:
#ifndef NO_TOP_LIST
                  $1->set_iter(g_ed_list.insert(g_ed_list.end(),$1));
                  dprintf("external_declaration\n"); 
                  dprintf("==============================================\n"); 
                  $$ = NULL;
#else
                  $$ = new translation_unit_external_declaration($1); 
                  dprintf("external_declaration\n"); 
                  dprintf("==============================================\n"); 
                  g_ast_tree = (translation_unit*)$$;
#endif
			      break;
				case CALLGRAPH:
				  ed_two_phase_callgraph($1);
				  $$ = NULL;
				  break;
				case ONEEDATATIME:
				  ed_two_phase_final($1);
				  $$ = NULL;
				  break;
				default:
				  printf("unknown phase selector\n");
				  exit(-1);
			  }
          }
      }
	| translation_unit external_declaration 
      { 
		  if($2) {
		      switch(phase_selector) {
				case FULLFILE:
#ifndef NO_TOP_LIST
                  $2->set_iter(g_ed_list.insert(g_ed_list.end(),$2));
                  dprintf("translation_unit external_declaration\n"); 
                  dprintf("==============================================\n"); 
                  $$ = NULL;
#else
                  $$ = new translation_unit_translation_unit_external_declaration($1,$2); 
                  dprintf("translation_unit external_declaration\n"); 
                  dprintf("==============================================\n"); 
                  g_ast_tree = (translation_unit*)$$;
#endif
			      break;
				case CALLGRAPH:
				  ed_two_phase_callgraph($2);
				  $$ = NULL;
				  break;
				case ONEEDATATIME:
				  ed_two_phase_final($2);
				  $$ = NULL;
				  break;
				default:
				  printf("unknown phase selector\n");
				  exit(-1);
			  }
           }
       }
	;

saa_expr
	: SAA_PARAM
	    {
	    }
	| SAA_RETVAL
	    {
	    }
	| SAA_METHOD
	    {
	    }
	;

saa_expr_list
	: saa_expr
	    {
	    }
	| saa_expr_list '|' saa_expr
	    {
	    }
	;
 
external_declaration
	: function_definition 
             {
                  $$ = new external_declaration_function_definition($1);
                  dprintf("function_definition\n");
                  unset_type_into_identifier();
             }
	| declaration 
             {
                  $$ = new external_declaration_declaration($1);
                  dprintf("declaration\n");
                  unset_type_into_identifier();
             }
	| HASH
             {
                  $$ = new external_declaration_hash($1);
                  dprintf("HASH=%s\n",$1);
             }
	| ';'
             {
                  $$ = new external_declaration_semicolon();
                  dprintf(";\n");
             }
	| PRT_MANAGED_OFF
             {
                  $$ = new external_declaration_prt_managed_off();
                  dprintf("PRT_MANAGED_OFF\n");
                  //g_pillar_mode = 0;
                  g_cc_stack.push(ST_CDECL);
             }
	| PRT_MANAGED_ON
             {
                  $$ = new external_declaration_prt_managed_on();
                  dprintf("PRT_MANAGED_ON\n");
                  //g_pillar_mode = 1;
                  g_cc_stack.pop();
                  if(g_cc_stack.size() == 0) {
                      printf("Imbalanced default cc push/pop.\n");
                      exit(-1);
                  }
             }
        | PRT_PUSH_CC '(' call_conv_specifier ')'
             {
                  $$ = new external_declaration_prt_push_cc($3);
                  dprintf("PRT_PUSH_CC\n");
                  g_cc_stack.push($3->get_call_conv());
             }
        | PRT_POP_CC '(' ')'
             {
                  $$ = new external_declaration_prt_pop_cc();
                  dprintf("PRT_POP_CC\n");
                  g_cc_stack.pop();
                  if(g_cc_stack.size() == 0) {
                      printf("Imbalanced default cc push/pop.\n");
                      exit(-1);
                  }
             }
        | PRT_POP_CC
             {
                  $$ = new external_declaration_prt_pop_cc();
                  dprintf("PRT_POP_CC\n");
                  g_cc_stack.pop();
                  if(g_cc_stack.size() == 0) {
                      printf("Imbalanced default cc push/pop.\n");
                      exit(-1);
                  }
             }
/*
	| MAINTAIN
             {
                  $$ = new external_declaration_pound_line($1);
             }
*/
	| '[' SOURCE_ANNOTATION_ATTRIBUTE '(' saa_expr_list ')' ']'
             {
		  $$ = NULL;
             }
        | POUND_LINE
             {
                  $$ = new external_declaration_pound_line($1);
                  dprintf("POUND_LINE\n");
             }
	;

function_definition
	: declaration_specifiers declarator declaration_list {Scope *s = $2->get_param_scope(); g_scope_stack.reenter_scope(s,true); } compound_statement {$<scope>$ = g_scope_stack.leave_scope();}
             {
                  switch(phase_selector) {
				    case ONEEDATATIME:
					  {
					  dprintf("declaration_specifiers declarator declaration_list compound_statement\n");
					  declarator *d = $2;

					  std::string name = d->get_one_name();
					  if(name == DEBUG_FUNC) {
						printf("Found %s in function_definition.\n",DEBUG_FUNC);
					  }
		              std::map<std::string,function_definition*>::iterator fmiter = g_func_map.find(name);
					  assert(fmiter != g_func_map.end());

					  $$ = fmiter->second;

					  function_definition_with_decl_list *fdwdl = dynamic_cast<function_definition_with_decl_list *>(fmiter->second);
					  assert(fdwdl);
					  fdwdl->reset($1,$2,$3,$5);

					  Scope *cs_scope = $<scope>6;
					  if(!cs_scope) {
						  printf("Problem.\n");
						  assert(0);
					  }
					  $$->set_scope(cs_scope);
					  }
				      break;
				    default:
					  {
					  $$ = new function_definition_with_decl_list($1,$2,$3,$5);
					  dprintf("declaration_specifiers declarator declaration_list compound_statement\n");
					  declarator *d = $2;
					  ii_ptr ii = d->add_to_symbol_table(IIT_FUNCTION);
					  ii->set_function_cc(fix_cc_default(d->get_call_conv()));
					  ii->set_func_definition($$);
					  $$->set_symbol(ii);

					  Scope *cs_scope = $<scope>6;
					  if(!cs_scope) {
						  printf("Problem.\n");
						  assert(0);
					  }
					  $$->set_scope(cs_scope);
					  }
				      break;
				  }
             }
	| declaration_specifiers declarator {Scope *s = $2->get_param_scope(); g_scope_stack.reenter_scope(s,true);} compound_statement  {$<scope>$ = g_scope_stack.leave_scope();}
             {
                  switch(phase_selector) {
				    case ONEEDATATIME:
					  {
					  dprintf("declaration_specifiers declarator compound_statement\n");
					  declarator *d = $2;

					  std::string name = d->get_one_name();
					  if(name == DEBUG_FUNC) {
						printf("Found %s in function_definition.\n",DEBUG_FUNC);
					  }
		              std::map<std::string,function_definition*>::iterator fmiter = g_func_map.find(name);
					  assert(fmiter != g_func_map.end());

					  $$ = fmiter->second;
					  
					  function_definition_no_decl_list *fdndl = dynamic_cast<function_definition_no_decl_list *>(fmiter->second);
					  assert(fdndl);
					  fdndl->reset($1,$2,$4);
					  Scope *cs_scope = $<scope>5;
					  if(!cs_scope) {
						  printf("Problem.\n");
						  assert(0);
					  }
					  $$->set_scope(cs_scope);
					  }
				      break;
				    default:
					  {
					  $$ = new function_definition_no_decl_list($1,$2,$4);
					  dprintf("declaration_specifiers declarator compound_statement\n");
					  declarator *d = $2;
					  ii_ptr ii = d->add_to_symbol_table(IIT_FUNCTION);
					  ii->set_function_cc(fix_cc_default(d->get_call_conv()));
					  ii->set_func_definition($$);
					  $$->set_symbol(ii);

					  Scope *cs_scope = $<scope>5;
					  if(!cs_scope) {
						  printf("Problem.\n");
						  assert(0);
					  }
					  $$->set_scope(cs_scope);
					  }
					  break;
			     }
             }
	;

declaration_list
	: declaration 
             { 
                  $$ = new declaration_list_declaration($1);
                  dprintf("declaration\n"); 
             }
	| declaration_list declaration 
             { 
                  $$ = new declaration_list_declaration_list_declaration($1,$2);
                  dprintf("declaration_list declaration\n"); 
             }
	;


%%
#include <stdio.h>

//extern char yytext[];
extern "C" int column;

extern "C" int yyerror(char const *s)
{
    fflush(stdout);
    dprintf("\n%*s\n%*s\n", column, "^", column, s);
    return 0;
}

extern "C" int check_type(char *id, unsigned free_it) {
    if(strcmp(id,"_Complex")==0) {
	    free(id);
        id = yylval.str = "_pillar2c_Complex";
    }

    std::string s(id);
	if(free_it) {
		free(id);
    }
    
	if(s == "PrtTaskHandle") {
	   s = s;
	}

	ii_ptr_release ipr;
	{
      ii_ptr ii = g_scope_stack.find_or_add(s);
	  ipr = ii;
	}
    yylval.symbol = ipr.get();

    if(!g_make_type_into_identifier && yylval.symbol->is_typedef()) {
        if(yylval.symbol->is_struct_enum()) {
            return TYPE_STRUCT_NAME;
        } else {
            return TYPE_NAME;
        }
    }
    return IDENTIFIER;
}

extern "C" int pillar_or_check_type(int token) {
    if(g_cc_stack.top() == ST_PDECL) {
        return token;
    } else { 
        switch(token) {
        case PRT_ALSO:
            return check_type("also",0);
            break;
        case PRT_CONTINUATION:
            return check_type("continuation",0);
            break;
        case PRT_CONTINUATION_VAR:
            return check_type("continuation_type",0);
            break;
        case PRT_CUT:
            return check_type("cut",0);
            break;
        case PRT_CUTS:
            return check_type("cuts",0);
            break;
        case PRT_TO:
            return check_type("to",0);
            break;
        case PRT_TAILCALL:
            return check_type("tailcall",0);
            break;
        case PRT_REF:
            return check_type("ref",0);
            break;
        case PRT_NOYIELD:
            return check_type("noyield",0);
            break;
        case PRT_PCALL:
            return check_type("pcall",0);
            break;
        case PRT_WITH:
            return check_type("with",0);
            break;
        case PRT_VSE:
            return check_type("vse",0);
            break;
        default:
            printf("Unknown token in pillar_or_check_type.\n");
            exit(-2);
        }
    }
}
