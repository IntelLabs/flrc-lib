/*
 * COPYRIGHT_NOTICE_1
 */

#ifdef _MSC_VER
#if 0
#include <stdlib.h>
#include <crtdbg.h>
#endif
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <string>
#ifndef WIN32
#include <sys/resource.h>
#endif

#if 0
#ifndef LEX_CPP
extern "C" int yyparse(void);
#else
int yyparse(void);
#endif
#endif
int yyparse(void);

extern "C" void print_ast_tree(void);
extern "C" void start_translate_pillar(void);
extern "C" void start_two_phase(char *filename);
extern "C" void pre_parse(void);
extern "C" void free_ast(void);

#ifndef LEX_CPP
extern "C" int g_pillar2c_debug_level;
extern "C" FILE *yyin;
extern "C" int yy_flex_debug;
#else
extern int g_pillar2c_debug_level;
extern FILE *yyin;
extern int yy_flex_debug;
#endif

extern bool g_gen_prolog_yields;
extern bool g_gen_backward_yields;
#if 0
extern char * g_output_file_base;
#else
extern std::string g_output_file_base;
#endif
extern bool g_prettify_code;
extern bool g_prt_args_in_pseudo;
extern bool g_retain_assign_ref;
extern FILE * g_output_file;
extern unsigned g_pseudo_pad;
extern bool g_spill_rare;
extern bool g_output_builtin_expect;
extern bool g_loop_hoists;
extern bool g_compile_as_cpp;
extern bool g_collect_read_stats;
extern bool g_gcc_compatible;
extern bool g_mic_compatible;
extern bool g_expression_type;
int g_short_mainline = 2;
extern bool g_use_try_except;
extern unsigned g_tailcall_implementation;
extern bool g_instrument;
extern bool g_ref_param_in_pseudo;
extern bool g_gc_at_call;
extern bool g_verbose;
extern bool g_eliminate_dead_code;
extern bool g_intel64;
extern bool g_log_m2u;
extern unsigned g_restrict_limit;
extern int  g_mode;
extern bool g_check_ref_write_barrier;
extern bool g_use_topo_tailcall;
extern unsigned g_topo_limit;
extern unsigned g_bypass_limit;
extern bool g_convert_prtGetTaskHandle;
extern bool g_use_th_param;

#ifdef WIN32
#include <windows.h>

LARGE_INTEGER g_whole_start, g_whole_end;
LARGE_INTEGER g_timer_start, g_timer_end;
LARGE_INTEGER g_timer_start2, g_timer_end2;
LARGE_INTEGER g_timer_start3, g_timer_end3;

extern "C" void StartTimer(void) {
    QueryPerformanceCounter(&g_timer_start);
}

extern "C" float StopTimer(void) {
    LARGE_INTEGER freq;

    QueryPerformanceCounter(&g_timer_end);
    QueryPerformanceFrequency(&freq);
    return ((g_timer_end.QuadPart - g_timer_start.QuadPart) / (float)(freq.QuadPart));
}

extern "C" void StartTimer2(void) {
    QueryPerformanceCounter(&g_timer_start2);
}

extern "C" double StopTimer2(void) {
    LARGE_INTEGER freq;

    QueryPerformanceCounter(&g_timer_end2);
    QueryPerformanceFrequency(&freq);
    return ((g_timer_end2.QuadPart - g_timer_start2.QuadPart) / (float)(freq.QuadPart));
}

extern "C" void StartTimer3(void) {
    QueryPerformanceCounter(&g_timer_start3);
}

extern "C" double StopTimer3(void) {
    LARGE_INTEGER freq;

    QueryPerformanceCounter(&g_timer_end3);
    QueryPerformanceFrequency(&freq);
    return ((g_timer_end3.QuadPart - g_timer_start3.QuadPart) / (float)(freq.QuadPart));
}

#endif

void print_help(void) {
    printf("pillar2c translator options\n");
    printf("------------------------------------------------------------------\n");
    printf("-p2c-debug[=N]           - turn on debugging information, higher N's = more information, default = 1\n");
    printf("-p2c-[no-]verbose        - turn on basic output, DEFAULT=OFF\n");
    printf("-p2c-outbase:<filename>  - send translated output to the given filename breaking it into 50Kloc chunks.\n");
    printf("-p2c-no-yields           - do not emit prolog or backward branch yield checks.\n");
    printf("-p2c-no-prolog-yields    - do not emit prolog yield checks.\n");
    printf("-p2c-[no-]pretty-code    - [do not] remove useless syntax to make the output more attractive, DEFAULT=ON\n");
    printf("-p2c-[no-]args-in-pseudo - [do not] store prt m2u/pcall arg structures in a union in the pseudo-frame, DEFAULT=ON\n");
    printf("-p2c-[no-]ret-equal-ref  - [do not] retain refs not active at a call site to combine with other refs, ala, ref1=ref2, DEFAULT=OFF\n");
    printf("-p2c-[no-]spill-rare     - [do not] using spill/restores for ref only alive across rare calls, DEFAULT=OFF\n");
    printf("-p2c-[no-]output-builtin-expect - [do not] output __builtin_expect clauses rather than translating them to the expression, DEFAULT=OFF\n");
    printf("-p2c-[no-]loop-hoists    - [do not] try to hoist refs used in loops but only at rare callsites, DEFAULT=OFF\n");
    printf("-p2c-[no-]read-stats     - [do not] collect statistics on objects reads, DEFAULT=OFF\n");
    printf("-p2c-[no-]gcc            - [do not] produce gcc compatible code, DEFAULT=OFF\n");
    printf("-p2c-[no-]mic            - [do not] produce mic compatible code, DEFAULT=OFF\n");
    printf("-p2c-[no-]expr_type      - [do not] track all expression types, DEFAULT=OFF\n");
    printf("-p2c-force-pcdecl:<filename> - file contains a list of function names whose calling convention will be overridden to __pcdecl\n");
    printf("-p2c-[no-]short-mainline - [do not] create grammatical nodes that simply connect expression mainlines together, DEFAULT=OFF\n");
    printf("-p2c-[no-]use-try        - [do not] use try-except in Windows for continuations,DEFAULT=ON\n");
    printf("-p2c-bypass-tailcall     - transform tailcalls into regular calls\n");
    printf("-p2c-[no-]instrument     - [do not] collect runtime info such as number of managed calls,DEFAULT=OFF\n");
    printf("-p2c-[no-]log-m2u        - [do not] log each m2u transition to the file pillar2c_m2u.log,DEFAULT=OFF\n");
    printf("-p2c-[no-]ref-param-pseudo - [do not] put live ref params in the pseudo-frame,DEFAULT=OFF\n");
    printf("-p2c-[no-]gc-at-call     - [do not] force a GC at every call site,DEFAULT=OFF\n");
    printf("-p2c-[no-]dead-code      - [do not] leave dead code in the output,DEFAULT=ON\n");
    printf("-p2c-[no-]intel64        - [do not] generate code for Intel64 (aka EMT64, X86_64),DEFAULT=OFF\n");
    printf("-p2c-[no-]restrict[=N]   - [do not] use the pseudo-frame via a restrict pointer,DEFAULT=ON(500)\n");
    printf("-p2c-[no-]check-ref-wb   - [do not] check that there are no unprotected writes to non-stack refs,DEFAULT=OFF\n");
    printf("-p2c-[no-]topo-tailcall  - [do not] use topological sort tailcall optimization,DEFAULT=OFF\n");
    printf("-p2c-[no-]repTaskHandle  - [do not] convert prtGetTaskHandle() to use pillar2c task handle parameter,DEFAULT=ON\n");
//    printf("-p2c-[no-]th-param       - [do not] add a PrtTaskHandle param to each unsafe managed method,DEFAULT=ON\n");
    exit(0);
}

void process_pcdecl_file(const std::string &file);

extern int yylineno;
extern char *yytext;

bool two_phase = false;
unsigned diff_stack_size = 0;

int main(int argc, char *argv[]) {
    int filename_index = 0;


#ifdef WIN32
    QueryPerformanceCounter(&g_whole_start);
#endif

#ifdef _MSC_VER
#ifdef _DEBUG
#if 0
    HANDLE hLogFile;
    hLogFile = CreateFile("c:\\leaks.txt", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, hLogFile);
#endif
#endif
#endif

#ifdef __x86_64__
    g_ref_param_in_pseudo = true;
#endif //

    int arg_index;
    for(arg_index = 0; arg_index < argc; ++arg_index) {
        if(argv[arg_index][0] == '-') {
            if(strncmp(argv[arg_index],"-p2c-debug",strlen("-p2c-debug")) == 0) {
                if(argv[arg_index][10] == '=') {
                    g_pillar2c_debug_level = atoi(argv[arg_index]+11);
                } else {
                    g_pillar2c_debug_level = 1;
                }
            } else if(strncmp(argv[arg_index],"-p2c-outbase",strlen("-p2c-outbase")) == 0) {
                if(argv[arg_index][12] == ':') {
                    g_output_file_base = argv[arg_index]+13;
                } else {
                    printf("Malformed outbase option.  Should be -p2c-outbase:<filename>.\n");
                }
            } else if(strncmp(argv[arg_index],"-p2c-force-pcdecl:",strlen("-p2c-force-pcdecl:")) == 0) {
                std::string pcdecl_file = argv[arg_index]+strlen("-p2c-force-pcdecl:");
                process_pcdecl_file(pcdecl_file);
            } else if(strcmp("-p2c-no-yields",argv[arg_index]) == 0) {
                g_gen_prolog_yields   = false;
                g_gen_backward_yields = false;
            } else if(strcmp("-p2c-no-prolog-yields",argv[arg_index]) == 0) {
                g_gen_prolog_yields = false;
            } else if(strcmp("-p2c-verbose",argv[arg_index]) == 0) {
                g_verbose = true;
            } else if(strcmp("-p2c-no-verbose",argv[arg_index]) == 0) {
                g_verbose = false;
            } else if(strcmp("-p2c-no-pretty-code",argv[arg_index]) == 0) {
                g_prettify_code = false;
            } else if(strcmp("-p2c-pretty-code",argv[arg_index]) == 0) {
                g_prettify_code = true;
            } else if(strcmp("-p2c-no-args-in-pseudo",argv[arg_index]) == 0) {
                g_prt_args_in_pseudo = false;
            } else if(strcmp("-p2c-args-in-pseudo",argv[arg_index]) == 0) {
                g_prt_args_in_pseudo = true;
            } else if(strcmp("-p2c-no-ret-equal-ref",argv[arg_index]) == 0) {
                g_retain_assign_ref = false;
            } else if(strcmp("-p2c-ret-equal-ref",argv[arg_index]) == 0) {
                g_retain_assign_ref = true;
            } else if(strcmp("-p2c-no-spill-rare",argv[arg_index]) == 0) {
                g_spill_rare = false;
            } else if(strcmp("-p2c-spill-rare",argv[arg_index]) == 0) {
                g_spill_rare = true;
            } else if(strcmp("-p2c-no-loop-hoists",argv[arg_index]) == 0) {
                g_loop_hoists = false;
            } else if(strcmp("-p2c-loop-hoists",argv[arg_index]) == 0) {
                g_loop_hoists = true;
            } else if(strcmp("-p2c-no-read-stats",argv[arg_index]) == 0) {
                g_collect_read_stats = false;
            } else if(strcmp("-p2c-read-stats",argv[arg_index]) == 0) {
                g_collect_read_stats = true;
                g_expression_type = true;
            } else if(strcmp("-p2c-no-expr-type",argv[arg_index]) == 0) {
                g_expression_type = false;
            } else if(strcmp("-p2c-expr-type",argv[arg_index]) == 0) {
                g_expression_type = true;
            } else if(strcmp("-p2c-no-short-mainline",argv[arg_index]) == 0) {
                g_short_mainline = 0;
            } else if(strcmp("-p2c-short-mainline",argv[arg_index]) == 0) {
                g_short_mainline = 2;
            } else if(strcmp("-p2c-no-gcc",argv[arg_index]) == 0) {
                g_gcc_compatible = false;
            } else if(strcmp("-p2c-gcc",argv[arg_index]) == 0) {
                g_gcc_compatible = true;
                g_use_try_except = false;
            } else if(strcmp("-p2c-two-phase",argv[arg_index]) == 0) {
                two_phase = true;
            } else if(strcmp("-p2c-no-mic",argv[arg_index]) == 0) {
                g_mic_compatible = false;
            } else if(strcmp("-p2c-mic",argv[arg_index]) == 0) {
                g_mic_compatible = true;
                g_gcc_compatible = true;
                g_use_try_except = false;
            } else if(strcmp("-p2c-no-use-try",argv[arg_index]) == 0) {
                g_use_try_except = false;
            } else if(strcmp("-p2c-use-try",argv[arg_index]) == 0) {
                g_use_try_except = true;
            } else if(strncmp("-p2c-bypass-tailcall",argv[arg_index],20) == 0) {
                if(argv[arg_index][20] == '=') {
                    g_bypass_limit = atoi(argv[arg_index]+21);
                } else {
                    g_tailcall_implementation = 0;
                }
            } else if(strcmp("-p2c-no-output-builtin-expect",argv[arg_index]) == 0) {
                g_output_builtin_expect = false;
            } else if(strcmp("-p2c-output-builtin-expect",argv[arg_index]) == 0) {
                g_output_builtin_expect = true;
            } else if(strncmp("-p2c-pad",argv[arg_index],8) == 0) {
                if(argv[arg_index][8] == '=') {
                    g_pseudo_pad = atoi(argv[arg_index]+9);
                } else {
                    printf("Malformed pad option.  Should be -pad=N.\n");
                }
            } else if(strcmp("/?",argv[arg_index]) == 0 || strcmp("-help",argv[arg_index]) == 0 || strcmp("--help",argv[arg_index]) == 0) {
                print_help();
            } else if(strcmp("-p2c-no-instrument",argv[arg_index]) == 0) {
                g_instrument = false;
            } else if(strcmp("-p2c-instrument",argv[arg_index]) == 0) {
                g_instrument = true;
            } else if(strcmp("-p2c-no-ref-param-pseudo",argv[arg_index]) == 0) {
#ifdef __x86_64__
                printf("Warning: ref params must go in the pseudo-frame in 64-bit.  Ignoring -p2c-no-ref-param-pseudo.\n");
#endif //
                g_ref_param_in_pseudo = false;
            } else if(strcmp("-p2c-ref-param-pseudo",argv[arg_index]) == 0) {
                g_ref_param_in_pseudo = true;
            } else if(strcmp("-p2c-no-gc-at-call",argv[arg_index]) == 0) {
                g_gc_at_call = false;
            } else if(strcmp("-p2c-gc-at-call",argv[arg_index]) == 0) {
                g_gc_at_call = true;
            } else if(strcmp("-p2c-no-dead-code",argv[arg_index]) == 0) {
                g_eliminate_dead_code = true;
            } else if(strcmp("-p2c-dead-code",argv[arg_index]) == 0) {
                g_eliminate_dead_code = false;
            } else if(strcmp("-p2c-no-intel64",argv[arg_index]) == 0) {
                g_intel64 = false;
            } else if(strcmp("-p2c-intel64",argv[arg_index]) == 0) {
                g_intel64 = true;
            } else if(strcmp("-p2c-no-restrict",argv[arg_index]) == 0) {
                g_restrict_limit = 0;
            } else if(strncmp("-p2c-restrict",argv[arg_index],13) == 0) {
                if(argv[arg_index][13] == '=') {
                    g_restrict_limit = atoi(argv[arg_index]+14);
                } else {
                    g_restrict_limit = 500;
                }
            } else if(strncmp("-p2c-stack-size",argv[arg_index],15) == 0) {
                if(argv[arg_index][15] != '=') {
                    printf("-p2c-stack-size option must be followed by =<N>.\n");
                    exit(-1);
                }
                diff_stack_size = atoi(argv[arg_index]+16);
            } else if(strcmp("-p2c-no-log-m2u",argv[arg_index]) == 0) {
                g_log_m2u = false;
            } else if(strcmp("-p2c-log-m2u",argv[arg_index]) == 0) {
                g_log_m2u = true;
            } else if(strcmp("-p2c-no-repTaskHandle",argv[arg_index]) == 0) {
                g_convert_prtGetTaskHandle = false;
            } else if(strcmp("-p2c-repTaskHandle",argv[arg_index]) == 0) {
                g_convert_prtGetTaskHandle = true;
            } else if(strcmp("-p2c-no-th-param",argv[arg_index]) == 0) {
                g_use_th_param = false;
                g_convert_prtGetTaskHandle = false;
            } else if(strcmp("-p2c-th-param",argv[arg_index]) == 0) {
                g_use_th_param = true;
            } else if(strcmp("-p2c-no-check-ref-wb",argv[arg_index]) == 0) {
                g_check_ref_write_barrier = false;
            } else if(strcmp("-p2c-check-ref-wb",argv[arg_index]) == 0) {
                g_check_ref_write_barrier = true;
            } else if(strcmp("-p2c-no-topo-tailcall",argv[arg_index]) == 0) {
                g_use_topo_tailcall = false;
            } else if(strncmp("-p2c-topo-tailcall",argv[arg_index],18) == 0) {
#ifdef __x86_64__
                printf("Topographical tailcalls cannot be used in 64-bit mode.\n");
                exit(-1);
#else
                g_use_topo_tailcall = true;
                if(argv[arg_index][18] == '=') {
                    g_topo_limit = atoi(argv[arg_index]+19);
                } else {
                    g_topo_limit = 4000000000;
                }
#endif
            } else {
                printf("Unrecognized command-line option %s.\n",argv[arg_index]);
                exit(-1);
            }
        } else {
            // Must be the filename to process.
            if(filename_index) {
                printf("Invalid command line.\n");
                exit(-1);
            } else {
                filename_index = arg_index;
            }
        }
    }

#ifndef WIN32
    if(diff_stack_size) {
        rlim_t new_stack_size = diff_stack_size;
        struct rlimit rl;
        if(getrlimit(RLIMIT_STACK, &rl) == 0) {
            if(rl.rlim_cur < new_stack_size) {
                rl.rlim_cur = new_stack_size;
                if(setrlimit(RLIMIT_STACK, &rl) != 0) {
                    printf("Couldn't set the stack limit.\n");
                } else {
                    printf("Successfully increased the stack limit.\n");
                }
            }
        } else {
            printf("Couldn't get the current stack limit.\n");
        }
    }
#endif

#if 0
    if(filename_index == 0) {
        printf("Filename not specified on the command-line.\n");
        exit(-1);
    }
#endif

#if 0
    if(g_output_file_base) {
        size_t len = strlen(g_output_file_base);
        char *complete_name = (char*)malloc(len+10);
        assert(complete_name);
        sprintf(complete_name,"%s.c",g_output_file_base);
        g_output_file = fopen(complete_name,"w");
        assert(g_output_file);
        free(complete_name);
    } else {
        g_output_file = stdout;
    }
#else
    if(g_output_file_base != "") {
        std::string complete_name = g_output_file_base + ".c";
        g_output_file = fopen(complete_name.c_str(),"w");
        assert(g_output_file);
    } else {
        g_output_file = stdout;
    }
#endif

	if(filename_index && two_phase) {
		start_two_phase(argv[filename_index]);
	} else {
      if(filename_index) {
        yyin = fopen(argv[filename_index],"r");
      } else {
        yyin = stdin;
      }

	  if(yyin) {
        if(g_pillar2c_debug_level) {
            printf("#if 0\n");
            printf("Setting debug level to %d.\n",g_pillar2c_debug_level);
        }
        if(g_pillar2c_debug_level > 3) {
            printf("Opened file %s\n",argv[1]);
        }
        g_mode = 0;
        pre_parse();
        g_mode = 1;
#ifdef WIN32
        StartTimer();
#endif
        int parse_result = yyparse();
#ifdef WIN32
        if(g_verbose) {
            printf("Parse time = %fs\n",StopTimer());
        }
#endif
        fclose(yyin);

        if(g_pillar2c_debug_level) {
            printf("\n#endif // end of parser comments\n");
            fflush(stdout);
        }

        g_mode = 2;

        if(!parse_result) {
            start_translate_pillar();
#ifdef WIN32
            StartTimer();
#endif
            print_ast_tree();
#ifdef WIN32
            if(g_verbose) {
                printf("Output time = %fs\n",StopTimer());
            }
#endif

#if 0
            free(g_output_file_base);
#endif

//            free_ast();

#ifdef WIN32
            LARGE_INTEGER freq;

            if(g_verbose) {
                QueryPerformanceCounter(&g_whole_end);
                QueryPerformanceFrequency(&freq);
                printf("Total time = %fs\n",((g_whole_end.QuadPart - g_whole_start.QuadPart) / (float)(freq.QuadPart)));
            }
#endif
        } else {
            printf("Program failed to parse at line %d.\nUnparseable symbol at that point => %s.\n",yylineno, yytext);
            assert(0);
            exit(-3);
        }
    } else {
        printf("Failed to open file %s\n",argv[filename_index]);
        perror("");
        assert(0);
        exit(-4);
    }
	}
} // main

unsigned get_num_procs(void) {
#ifdef WIN32
	SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    return system_info.dwNumberOfProcessors;
#else
    // FIX FIX FIX
    return 1;
#endif
}
