
/* A Bison parser, made by GNU Bison 2.4.1.  */

/* Skeleton interface for Bison's Yacc-like parsers in C
   
      Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.
   
   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */


/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     IDENTIFIER = 258,
     TYPE_NAME = 259,
     TYPE_STRUCT_NAME = 260,
     SYMBOL = 261,
     HASH = 262,
     UUASM = 263,
     UASM = 264,
     UUASMUU = 265,
     POUND_LINE = 266,
     MAINTAIN = 267,
     CONSTANT = 268,
     STRING_LITERAL = 269,
     CONSTANT_INT = 270,
     CONSTANT_UNSIGNED = 271,
     CONSTANT_LONG = 272,
     CONSTANT_ULONG = 273,
     CONSTANT_FLOAT = 274,
     CONSTANT_DOUBLE = 275,
     CONSTANT_LONG_DOUBLE = 276,
     CONSTANT_ULONGLONG = 277,
     PTR_OP = 278,
     INC_OP = 279,
     DEC_OP = 280,
     LEFT_OP = 281,
     RIGHT_OP = 282,
     LE_OP = 283,
     GE_OP = 284,
     EQ_OP = 285,
     NE_OP = 286,
     AND_OP = 287,
     OR_OP = 288,
     MUL_ASSIGN = 289,
     DIV_ASSIGN = 290,
     MOD_ASSIGN = 291,
     ADD_ASSIGN = 292,
     SUB_ASSIGN = 293,
     LEFT_ASSIGN = 294,
     RIGHT_ASSIGN = 295,
     AND_ASSIGN = 296,
     XOR_ASSIGN = 297,
     OR_ASSIGN = 298,
     SIZEOF = 299,
     BUILTIN_EXPECT = 300,
     BUILTIN_OFFSETOF = 301,
     TYPEDEF = 302,
     EXTERN = 303,
     STATIC = 304,
     AUTO = 305,
     REGISTER = 306,
     INLINE = 307,
     RESTRICT = 308,
     UURESTRICT = 309,
     UUINLINE = 310,
     UINLINE = 311,
     UUINLINEUU = 312,
     CHAR = 313,
     SHORT = 314,
     INT = 315,
     LONG = 316,
     SIGNED = 317,
     UNSIGNED = 318,
     FLOAT = 319,
     DOUBLE = 320,
     CONST = 321,
     VOLATILE = 322,
     VOID = 323,
     BUILTIN_VA_LIST = 324,
     INT64 = 325,
     INT32 = 326,
     INT16 = 327,
     INT8 = 328,
     BOOL = 329,
     COMPLEX = 330,
     IMAGINARY = 331,
     STRUCT = 332,
     UNION = 333,
     ENUM = 334,
     ELLIPSIS = 335,
     FORCEINLINE = 336,
     CASE = 337,
     DEFAULT = 338,
     IF = 339,
     ELSE = 340,
     SWITCH = 341,
     WHILE = 342,
     DO = 343,
     FOR = 344,
     GOTO = 345,
     CONTINUE = 346,
     BREAK = 347,
     RETURN = 348,
     UCDECL = 349,
     CDECL = 350,
     STDCALL = 351,
     PASCAL = 352,
     UDECLSPEC = 353,
     UUDECLSPEC = 354,
     ALIGN512 = 355,
     ALIGN256 = 356,
     ALIGN128 = 357,
     ALIGN64 = 358,
     ALIGN32 = 359,
     ALIGN16 = 360,
     ALIGN8 = 361,
     ALIGN4 = 362,
     ALIGN2 = 363,
     ALIGN1 = 364,
     DLLIMPORT = 365,
     DLLEXPORT = 366,
     NORETURN = 367,
     NOINLINE = 368,
     NAKED = 369,
     DEPRECATED = 370,
     NOALIAS = 371,
     UUW64 = 372,
     UUPTR64 = 373,
     INTRIN_TYPE = 374,
     PRT_CUT = 375,
     PRT_TO = 376,
     PRT_TAILCALL = 377,
     PRT_MANAGED_OFF = 378,
     PRT_MANAGED_ON = 379,
     PRT_REF = 380,
     PRT_PDECL = 381,
     PRT_PCDECL = 382,
     PRT_NOYIELD = 383,
     PRT_PCALL = 384,
     PRT_WITH = 385,
     PRT_ALSO = 386,
     PRT_CUTS = 387,
     PRT_CONTINUATION = 388,
     PRT_CONTINUATION_VAR = 389,
     PRT_VSE = 390,
     SOURCE_ANNOTATION_ATTRIBUTE = 391,
     SAA_PARAM = 392,
     SAA_RETVAL = 393,
     SAA_METHOD = 394,
     MULT_START = 395,
     MULT_END = 396,
     PRT_PUSH_CC = 397,
     PRT_POP_CC = 398,
     GCC_ATTRIBUTE = 399,
     LOWER_THAN_ELSE = 400
   };
#endif



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
{

/* Line 1676 of yacc.c  */
#line 94 "../../pillar2c.y"

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



/* Line 1676 of yacc.c  */
#line 287 "../../pillar2c.tab.hh"
} YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif

extern YYSTYPE yylval;


