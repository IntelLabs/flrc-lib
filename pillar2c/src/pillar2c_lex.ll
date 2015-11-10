D			[0-9]
L			[a-zA-Z_]
H			[a-fA-F0-9]
E			[Ee][+-]?{D}+
P                       [Pp][+-]?{D}+
FS			(f|F|l|L)
FFS			(f|F)
LFS			(l|L)
IS                      ((u|U)|(u|U)?(l|L|ll|LL)|(l|L|ll|LL)(u|U)|ui64)
UIS                     ((u|U))
LIS                     ((l|L|ll|LL))
ULIS                    ((u|U)(l|L|ll|LL)|(l|L|ll|LL)(u|U))
ULLIS                   ((ui64|i64))

%{
/*
 * COPYRIGHT_NOTICE_1
 */

#include <stdio.h>
#include "pillar2c/pillar2c.ast_c.h"
#include "pillar2c/pillar2c.tab.hh"
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif
void count(void);
void comment(void);
void uuasmuu_get(void);
int attribute_get(int *);
void consume_uupragma(void);
int check_type(char *,unsigned);
int pillar_or_check_type(int token);
void set_type_into_identifier();
void unset_type_into_identifier();
#ifdef __cplusplus
}
#endif

char * strdupn(char *str, unsigned len) {
    char *ret = (char *)malloc(len+1);
    if(ret) {
        strncpy(ret,str,len);
        ret[len] = '\0';
    }
    return ret;
}

#define SAVE_TOKEN yylval.str = strdupn(yytext,yyleng)

#ifdef __cplusplus
// extern "C" int isatty(int);
static int yyinput();
int input() {
    return yyinput();
}
#endif

int g_pillar2c_debug_level = 0;
int column = 0;

// "_Complex"		{ count(); return(COMPLEX); }
//"#"[^\n]*               { printf("Using generic # rule %s\n",yytext); /* count(); SAVE_TOKEN; return(HASH); */ }

%}

%%
"/*"			{ comment(); }
"//"[^\n]*              { /* consume //-comment */ }
"#"[ ]*"pragma"[ ]*"pillar_push_cc" { count(); return PRT_PUSH_CC; }
"#"[ ]*"pragma"[ ]*"pillar_pop_cc" { count(); return PRT_POP_CC; }
"#"[ ]*"pragma"[ ]*"pillar_managed"[ ]*"("[]*"off"[ ]*")" { count(); return PRT_MANAGED_OFF; }
"#"[ ]*"pragma"[ ]*"pillar_managed"[ ]*"("[]*"on"[ ]*")" { count(); return PRT_MANAGED_ON; }
"#"[ ]*"pragma"[ ]*"once" { count(); }
"#"[ ]*"pragma"[ ]*"pack"[^\n]* { count(); SAVE_TOKEN; return POUND_LINE; }
"#"[ ]*"pragma"[ ]*"warning"[^\n]* { count(); SAVE_TOKEN; return POUND_LINE; }
"#"[ ]*"pragma"[ ]*"intrinsic"[^\n]* { count(); SAVE_TOKEN; return POUND_LINE; }
"#"[ ]*"pragma"[ ]*"function"[^\n]* { count(); SAVE_TOKEN; return POUND_LINE; }
"#"[ ]*"pragma"[ ]*"deprecated"[^\n]* { count(); }
"#"[ ]*"pragma"[ ]*"comment"[^\n]* { count(); }
"#"[ ]*"pragma"[ ]*"region"[^\n]* { count(); }
"#"[ ]*"pragma"[ ]*"endregion"[^\n]* { count(); }
"#"[ ]*"line"[^\n]*     { count(); SAVE_TOKEN; return POUND_LINE; }
"#"[ ]*{D}[^\n]* { }
"__asm__"               { uuasmuu_get(); return(UUASMUU); }
"__asm"[ ]*"{"[^}]*"}"  { count(); SAVE_TOKEN; return(UUASM); }
"__asm"[^_][^}\n]*          { count(); SAVE_TOKEN; return(UUASM); }
"_asm"[ ]*"{"[^}]*"}"   { count(); SAVE_TOKEN; return(UASM); }
"_asm"[^}\n]*           { count(); SAVE_TOKEN; return(UASM); }
"__attribute__"         { int do_ret=0; int ret = attribute_get(&do_ret); if(do_ret) return ret; }
"__pragma"		{ consume_uupragma(); }

"align"[ ]*"("[ ]*"512"[ ]*")"             { count(); return(ALIGN512); }
"align"[ ]*"("[ ]*"256"[ ]*")"             { count(); return(ALIGN256); }
"align"[ ]*"("[ ]*"128"[ ]*")"             { count(); return(ALIGN128); }
"align"[ ]*"("[ ]*"64"[ ]*")"             { count(); return(ALIGN64); }
"align"[ ]*"("[ ]*"32"[ ]*")"             { count(); return(ALIGN32); }
"align"[ ]*"("[ ]*"16"[ ]*")"             { count(); return(ALIGN16); }
"align"[ ]*"("[ ]*"8"[ ]*")"              { count(); return(ALIGN8); }
"align"[ ]*"("[ ]*"4"[ ]*")"              { count(); return(ALIGN4); }
"align"[ ]*"("[ ]*"2"[ ]*")"              { count(); return(ALIGN2); }
"align"[ ]*"("[ ]*"1"[ ]*")"              { count(); return(ALIGN1); }
"also"                  { count(); return pillar_or_check_type(PRT_ALSO); }
"auto"			{ count(); return(AUTO); }
"_Bool"			{ count(); return(BOOL); }
"break"			{ count(); return(BREAK); }
"__builtin_expect"      { count(); return (BUILTIN_EXPECT); }
"__builtin_offsetof"    { count(); return (BUILTIN_OFFSETOF); }
"case"			{ count(); return(CASE); }
"__cdecl"               { count(); return(CDECL); }
"_cdecl"                { count(); return(UCDECL); }
"char"			{ count(); return(CHAR); }
"const"			{ count(); return(CONST); }
"__const"		{ count(); return(CONST); }
"continue"		{ count(); return(CONTINUE); }
"continuation"          { count(); return pillar_or_check_type(PRT_CONTINUATION); }
"continuation_type"     { count(); return pillar_or_check_type(PRT_CONTINUATION_VAR); }
"cut"                   { count(); return pillar_or_check_type(PRT_CUT); }
"cuts"                  { count(); return pillar_or_check_type(PRT_CUTS); }
"to"                    { count(); return pillar_or_check_type(PRT_TO); }
"_declspec"             { count(); return(UDECLSPEC); }
"__declspec"            { count(); return(UUDECLSPEC); }
"default"		{ count(); return(DEFAULT); }
"deprecated"            { count(); return(DEPRECATED); }
"dllexport"             { count(); return(DLLEXPORT); }
"dllimport"             { count(); return(DLLIMPORT); }
"do"			{ count(); return(DO); }
"double"		{ count(); return(DOUBLE); }
"else"			{ count(); return(ELSE); }
"enum"			{ count(); return(ENUM); }
"extern"		{ count(); return(EXTERN); }
"float"			{ count(); return(FLOAT); }
"for"			{ count(); return(FOR); }
"goto"			{ count(); return(GOTO); }
"if"			{ count(); return(IF); }
"_Imaginary"		{ count(); return(IMAGINARY); }
"inline"		{ count(); return(INLINE); }
"_inline"		{ count(); return(UINLINE); }
"__inline"		{ count(); return(UUINLINE); }
"__inline__"		{ count(); return(UUINLINEUU); }
"__forceinline"         { count(); return(FORCEINLINE); }
"int"			{ count(); return(INT); }
"__int64"		{ count(); return(INT64); }
"__int32"		{ count(); return(INT32); }
"__int16"		{ count(); return(INT16); }
"__int8"		{ count(); return(INT8); }
"intrin_type"           { count(); return(INTRIN_TYPE); }
"long"			{ count(); return(LONG); }
"naked"                 { count(); return(NAKED); }
"noalias"               { count(); return(NOALIAS); }
"noinline"              { count(); return(NOINLINE); }
"noreturn"              { count(); return(NORETURN); }
"noyield"               { count(); return pillar_or_check_type(PRT_NOYIELD); }
"__pascal"              { count(); return(PASCAL); }
"__pdecl"               { count(); return(PRT_PDECL); }
"__pcdecl"              { count(); return(PRT_PCDECL); }
"pcall"                 { count(); return pillar_or_check_type(PRT_PCALL); }
"__ptr64"               { count(); return(UUPTR64); }
"ref"                   { count(); return pillar_or_check_type(PRT_REF); }
"register"		{ count(); return(REGISTER); }
"restrict"		{ count(); return(RESTRICT); }
"__restrict"		{ count(); return(UURESTRICT); }
"return"		{ count(); return(RETURN); }
"short"			{ count(); return(SHORT); }
"signed"		{ count(); return(SIGNED); }
"sizeof"		{ count(); return(SIZEOF); }
"source_annotation_attribute"	{ count(); return(SOURCE_ANNOTATION_ATTRIBUTE); }
"SA_Parameter"		{ count(); return(SAA_PARAM); }
"SA_Method"		{ count(); return(SAA_METHOD); }
"SA_ReturnValue"	{ count(); return(SAA_PARAM); }
"static"		{ count(); return(STATIC); }
"__stdcall"             { count(); return(STDCALL); }
"struct"		{ count(); return(STRUCT); }
"switch"		{ count(); return(SWITCH); }
"tailcall"              { count(); return pillar_or_check_type(PRT_TAILCALL); }
"typedef"		{ count(); return(TYPEDEF); }
"union"			{ count(); return(UNION); }
"unsigned"		{ count(); return(UNSIGNED); }
"__builtin_va_list"     { count(); return(BUILTIN_VA_LIST); }
"void"			{ count(); return(VOID); }
"volatile"		{ count(); return(VOLATILE); }
"VSE"                   { count(); return pillar_or_check_type(PRT_VSE); }
"with"                  { count(); return pillar_or_check_type(PRT_WITH); }
"while"			{ count(); return(WHILE); }
"__w64"                 { count(); return(UUW64); }

{L}({L}|{D})*		{ count(); SAVE_TOKEN; return(check_type(yylval.str,1)); }

L?'(\\.|[^\\'\n])+'	{ count(); SAVE_TOKEN; return(CONSTANT); }

0[xX]{H}+		{ count(); SAVE_TOKEN; return(CONSTANT_INT); }
0{D}+			{ count(); SAVE_TOKEN; return(CONSTANT_INT); }
{D}+			{ count(); SAVE_TOKEN; return(CONSTANT_INT); }

0[xX]{H}+{UIS}		{ count(); SAVE_TOKEN; return(CONSTANT_UNSIGNED); }
0{D}+{UIS}		{ count(); SAVE_TOKEN; return(CONSTANT_UNSIGNED); }
{D}+{UIS}		{ count(); SAVE_TOKEN; return(CONSTANT_UNSIGNED); }

0[xX]{H}+{LIS}		{ count(); SAVE_TOKEN; return(CONSTANT_LONG); }
0{D}+{LIS}		{ count(); SAVE_TOKEN; return(CONSTANT_LONG); }
{D}+{LIS}		{ count(); SAVE_TOKEN; return(CONSTANT_LONG); }

0[xX]{H}+{ULIS}?	{ count(); SAVE_TOKEN; return(CONSTANT_ULONG); }
0{D}+{ULIS}?		{ count(); SAVE_TOKEN; return(CONSTANT_ULONG); }
{D}+{ULIS}?		{ count(); SAVE_TOKEN; return(CONSTANT_ULONG); }

0[xX]{H}+{ULLIS}?	{ count(); SAVE_TOKEN; return(CONSTANT_ULONGLONG); }
0{D}+{ULLIS}?		{ count(); SAVE_TOKEN; return(CONSTANT_ULONGLONG); }
{D}+{ULLIS}?		{ count(); SAVE_TOKEN; return(CONSTANT_ULONGLONG); }

{D}+{E}			{ count(); SAVE_TOKEN; return(CONSTANT_DOUBLE); }
{D}*"."{D}+({E})?	{ count(); SAVE_TOKEN; return(CONSTANT_DOUBLE); }
{D}+"."{D}*({E})?	{ count(); SAVE_TOKEN; return(CONSTANT_DOUBLE); }
0[xX]{H}+{P}            { count(); SAVE_TOKEN; return(CONSTANT_DOUBLE); }
0[xX]{H}*"."{H}+({P})?  { count(); SAVE_TOKEN; return(CONSTANT_DOUBLE); }
0[xX]{H}+"."{H}*({P})?  { count(); SAVE_TOKEN; return(CONSTANT_DOUBLE); }

{D}+{E}{FFS}		{ count(); SAVE_TOKEN; return(CONSTANT_FLOAT); }
{D}*"."{D}+({E})?{FFS}  { count(); SAVE_TOKEN; return(CONSTANT_FLOAT); }
{D}+"."{D}*({E})?{FFS}	{ count(); SAVE_TOKEN; return(CONSTANT_FLOAT); }
0[xX]{H}+{P}{FFS}               { count(); SAVE_TOKEN; return(CONSTANT_FLOAT); }
0[xX]{H}*"."{H}+({P})?{FFS}     { count(); SAVE_TOKEN; return(CONSTANT_FLOAT); }
0[xX]{H}+"."{H}*({P})?{FFS}     { count(); SAVE_TOKEN; return(CONSTANT_FLOAT); }

{D}+{E}{LFS}		{ count(); SAVE_TOKEN; return(CONSTANT_LONG_DOUBLE); }
{D}*"."{D}+({E})?{LFS}  { count(); SAVE_TOKEN; return(CONSTANT_LONG_DOUBLE); }
{D}+"."{D}*({E})?{LFS}	{ count(); SAVE_TOKEN; return(CONSTANT_LONG_DOUBLE); }
0[xX]{H}+{P}{LFS}       { count(); SAVE_TOKEN; return(CONSTANT_LONG_DOUBLE); }
0[xX]{H}*"."{H}+({P})?{LFS}     { count(); SAVE_TOKEN; return(CONSTANT_LONG_DOUBLE); }
0[xX]{H}+"."{H}*({P})?{LFS}     { count(); SAVE_TOKEN; return(CONSTANT_LONG_DOUBLE); }

L?\"(\\.|[^\\"\n])*\"	{ count(); SAVE_TOKEN; return(STRING_LITERAL); }

"..."			{ count(); return(ELLIPSIS); }
">>="			{ count(); return(RIGHT_ASSIGN); }
"<<="			{ count(); return(LEFT_ASSIGN); }
"+="			{ count(); return(ADD_ASSIGN); }
"-="			{ count(); return(SUB_ASSIGN); }
"*="			{ count(); return(MUL_ASSIGN); }
"/="			{ count(); return(DIV_ASSIGN); }
"%="			{ count(); return(MOD_ASSIGN); }
"&="			{ count(); return(AND_ASSIGN); }
"^="			{ count(); return(XOR_ASSIGN); }
"|="			{ count(); return(OR_ASSIGN); }
">>"			{ count(); return(RIGHT_OP); }
"<<"			{ count(); return(LEFT_OP); }
"++"			{ count(); return(INC_OP); }
"--"			{ count(); return(DEC_OP); }
"->"			{ count(); return(PTR_OP); }
"&&"			{ count(); return(AND_OP); }
"||"			{ count(); return(OR_OP); }
"<="			{ count(); return(LE_OP); }
">="			{ count(); return(GE_OP); }
"=="			{ count(); return(EQ_OP); }
"!="			{ count(); return(NE_OP); }
";"			{ count(); return(';'); }
("{"|"<%")		{ count(); return('{'); }
("}"|"%>")		{ count(); return('}'); }
","			{ count(); return(','); }
":"			{ count(); return(':'); }
"="			{ count(); return('='); }
"("			{ count(); unset_type_into_identifier(); return('('); }
")"			{ count(); return(')'); }
("["|"<:")		{ count(); return('['); }
("]"|":>")		{ count(); return(']'); }
"."			{ count(); return('.'); }
"&"			{ count(); return('&'); }
"!"			{ count(); return('!'); }
"~"			{ count(); return('~'); }
"-"			{ count(); return('-'); }
"+"			{ count(); return('+'); }
"*"			{ count(); return('*'); }
"/"			{ count(); return('/'); }
"%"			{ count(); return('%'); }
"<"			{ count(); return('<'); }
">"			{ count(); return('>'); }
"^"			{ count(); return('^'); }
"|"			{ count(); return('|'); }
"?"			{ count(); return('?'); }
"@["                    { count(); return(MULT_START); }
"]@"                    { count(); return(MULT_END); }

[ \t\v\n\f]		{ count(); }
.			{ /* printf("Unlexable token %s.\n",yytext); */ }

%%

int yywrap(void) {
    return 1;
}

void append_char(char *dest,char source,unsigned *cur_count,unsigned max) {
    *cur_count = *cur_count + 1;
    if(*cur_count >= max) {
        fprintf(stderr,"attribute string too long.\n");
        exit(-1);
    }
    unsigned len = strlen(dest);
    dest[len] = source;
    dest[len+1] = '\0';
}

#define input_test(test_char) { ++yyleng; c = input(); append_char(agbuf,c,&cur_count,1000); if(c != test_char) { fprintf(stderr,"malformed uuasmuu\n"); fprintf(stderr,"%s\n",agbuf); exit(-1); }}

void uuasmuu_get(void) {
    char c;
    unsigned paren_count = 1;
    unsigned cur_count = yyleng;
    char agbuf[1000];
    strcpy(agbuf,yytext);

//    printf("uuasmuu_get\n");
//    exit(-1);

    while(1) {
	++yyleng;
        c = input();
	append_char(agbuf,c,&cur_count,1000);
	if (c == '\n') {
		column = 0;
                ++yylineno;
        }
	else if (c == '\t')
		column += 8 - (column % 8);
	else
		column++;
        if(c == ' ') continue;
        if(c == '(') break;
	if(c == '_') {
            input_test((char)'_');
            input_test((char)'v');
            input_test((char)'o');
            input_test((char)'l');
            input_test((char)'a');
            input_test((char)'t');
            input_test((char)'i');
            input_test((char)'l');
            input_test((char)'e');
            input_test((char)'_');
            input_test((char)'_');
            continue;
        }
        fprintf(stderr,"malformed uuasmuu\n");
        fprintf(stderr,"%s\n",agbuf);
        exit(-1);
    }

    while(paren_count) {
	++yyleng;
        c = input();
	append_char(agbuf,c,&cur_count,1000);
	if (c == '\n') {
		column = 0;
                ++yylineno;
        }
	else if (c == '\t')
		column += 8 - (column % 8);
	else
		column++;
        if(c == '(') {
            ++paren_count;
        }
        if(c == ')') {
            --paren_count;
        }
    }
    yylval.str = strdupn(agbuf,yyleng);
//    printf("uuasmuu_get: %s, %d\n",yylval.str,yyleng);
}

int attribute_get(int *do_ret) {
    char c;
    unsigned paren_count = 2;
    unsigned cur_count = yyleng;
    char agbuf[200];
    strcpy(agbuf,yytext);

    while(1) {
        ++yyleng;
        c = input();

		if (c == '\n') {
			column = 0;
                        ++yylineno;
                }
		else if (c == '\t')
			column += 8 - (column % 8);
		else
			column++;

        if(isspace(c)) continue;
        append_char(agbuf,c,&cur_count,200);
        if(c == '(') break;
        fprintf(stderr,"malformed attribute\n");
    }
    while(1) {
        ++yyleng;

        c = input();
		if (c == '\n') {
			column = 0;
                        ++yylineno;
                }
		else if (c == '\t')
			column += 8 - (column % 8);
		else
			column++;
        if(isspace(c)) continue;
        append_char(agbuf,c,&cur_count,200);
        if(c == '(') break;
        fprintf(stderr,"malformed attribute\n");
    }

    while(paren_count) {
        ++yyleng;
        c = input();
		if (c == '\n') {
			column = 0;
                        ++yylineno;
                }
		else if (c == '\t')
			column += 8 - (column % 8);
		else
			column++;

        if(isspace(c)) continue;

        append_char(agbuf,c,&cur_count,200);
        if(c == '(') {
            ++paren_count;
        }
        if(c == ')') {
            --paren_count;
        }
    }
    yylval.str = strdupn(agbuf,yyleng);
//    printf("attribute_get: %s, %d\n",yylval.str,yyleng);

    *do_ret = 1;

    if(strcmp(agbuf,"__attribute__((cdecl))") == 0) {
        return CDECL;
    } else if(strcmp(agbuf,"__attribute__((stdcall))") == 0) {
        return STDCALL;
    } else if(strcmp(agbuf,"__attribute__((aligned(512)))") == 0) {
	    return ALIGN512;
    } else if(strcmp(agbuf,"__attribute__((aligned(256)))") == 0) {
	    return ALIGN256;
    } else if(strcmp(agbuf,"__attribute__((aligned(128)))") == 0) {
	    return ALIGN128;
    } else if(strcmp(agbuf,"__attribute__((aligned(64)))") == 0) {
	    return ALIGN64;
    } else if(strcmp(agbuf,"__attribute__((aligned(32)))") == 0) {
	    return ALIGN32;
    } else if(strcmp(agbuf,"__attribute__((aligned(16)))") == 0) {
	    return ALIGN16;
    } else if(strcmp(agbuf,"__attribute__((aligned(8)))") == 0) {
	    return ALIGN8;
    } else if(strcmp(agbuf,"__attribute__((aligned(4)))") == 0) {
	    return ALIGN4;
	}

    *do_ret = 0;
    return 0;
}

void comment(void) {
    char c, prev = 0;

    while ((c = input()) != 0)      /* (EOF maps to 0) */
    {
        if(c == '\n') {
           ++yylineno;
	}
        if (c == '/' && prev == '*')
            return;
        prev = c;
    }
    fprintf(stderr,"unterminated comment\n");
}

void consume_uupragma(void) {
    char c;
    unsigned paren_nest_level = 1;

    // find first paren
    while ((c = input()) != 0) {
        if(c == '(') break;
    }

    if(c == 0) {
        fprintf(stderr,"Didn't find parentheses for __pragma\n");
    }

    while ((c = input()) != 0) {
        if(c == '(') {
            ++paren_nest_level;
	} else if(c == ')') {
            --paren_nest_level;
	}
        if(paren_nest_level == 0) return;
    }

    if(c == 0) {
        fprintf(stderr,"Unmatched parentheses for __pragma\n");
    }
}

void count(void) {
    int i;

    for (i = 0; yytext[i] != '\0'; i++)
		if (yytext[i] == '\n') {
			column = 0;
                        ++yylineno;
                }
		else if (yytext[i] == '\t')
			column += 8 - (column % 8);
		else
			column++;

    if(g_pillar2c_debug_level) {
        ECHO;
    }
}
