/*
 * COPYRIGHT_NOTICE_1
 */

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else /* !__cplusplus */
#define EXTERN_C
#endif /* !__cplusplus */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <prtcodegenerator.h>

EXTERN_C void __prt_code_start(void);
EXTERN_C void __prt_code_end(void);

#ifdef __x86_64__
#define PILLAR2C_RT_STDCALL
#else  // __x86_64__
#ifdef __GNUC__
#define PILLAR2C_RT_STDCALL __attribute__((stdcall))
#else  // __GNUC__
#define PILLAR2C_RT_STDCALL __stdcall
#endif // __GNUC__
#endif // __x86_64__

#ifdef NO_P2C_TH
EXTERN_C void PILLAR2C_RT_STDCALL pillar_main(void *frame,int,char**);
#else
EXTERN_C void PILLAR2C_RT_STDCALL pillar_main(PrtTaskHandle task,void *frame,int,char**);
#endif
EXTERN_C void PILLAR2C_RT_STDCALL pillar2c_main(int argc, char **argv);

#ifdef __GNUC__
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
void pillar2cInvokeUnmanagedStart(void);
void pillar2cInvokeUnmanagedDestructor(void);
void pillar2cYieldStart(void);
void pillar2cYieldDestructor(void);
#ifdef __cplusplus
}
#endif // __cplusplus
#else  // __GNUC__
EXTERN_C unsigned char pillar2cInvokeUnmanagedStart;
EXTERN_C unsigned char pillar2cInvokeUnmanagedDestructor;
EXTERN_C unsigned char pillar2cYieldStart;
EXTERN_C unsigned char pillar2cYieldDestructor;
#endif // __GNUC__

void __pillar2c_log_m2u(char *function) {
    static FILE *m2u_log_file = NULL;
    if(!m2u_log_file) {
        m2u_log_file = fopen("pillar2c_m2u.log","w");
    }
    fprintf(m2u_log_file,"%s\n",function);
}

EXTERN_C unsigned g_pillar2c_tls_offset;
EXTERN_C unsigned g_pillar_tls_offset;

#define PILLAR2C_TLS_SLOTS 80
#define PILLAR2C_MAX_TAILCALL (PILLAR2C_TLS_SLOTS-2)
typedef struct {
    unsigned cur_alloc;
    void *tc_mem;
} __pillar2c_tls_info;

void * __pillar2c_get_tailcall(void *task_handle, unsigned num_slots_needed) {
    __pillar2c_tls_info *pti = (__pillar2c_tls_info*)(*(char **)((char*)task_handle + g_pillar_tls_offset));
    if(num_slots_needed <= PILLAR2C_MAX_TAILCALL) {
        return (void*)(pti + 1); // advance past cur_alloc and tc_mem to the bulk of the pillar2c tls space
    } else {
        if(pti->cur_alloc >= num_slots_needed) {
            return pti->tc_mem;
        } else {
            pti->tc_mem = realloc(pti->tc_mem,num_slots_needed * sizeof(void*));
            pti->cur_alloc = num_slots_needed;
            return pti->tc_mem;
        }
    }
}

unsigned long long g_pillar2c_managed_calls = 0;
unsigned long long g_pillar2c_managed_call_stmt_overhead = 0;

typedef struct {
    short active;
    short tag;
    void *id;
} pillar2c_gen_ref_info;

typedef struct {
    unsigned length;
    pillar2c_gen_ref_info refs[];
} pillar2c_generic_gen_ref;

//#ifndef __x86_64__
//#define PREV_OPT
//#endif // __x86_64__

typedef struct _pillar2c_pseudo_stack_entry {
#ifndef PREV_OPT
    struct _pillar2c_pseudo_stack_entry * prev;
#endif
    PrtCodeAddress *fake_eip_target;
    pillar2c_generic_gen_ref *ref_mask_ptr;
    void *refs[];
} pillar2c_pseudo_stack_entry;

#if 0
typedef struct {
    void * arg0;
    void * arg1;
    void * arg2;
} wb_part;

typedef struct {
    wb_part from_esp;
    void * orig_ebx;
    PrtCodeAddress return_eip;
    void * taskHandle;
    void * prevPseudoFrame;
} write_barrier_stack_structure;
#endif

#ifdef __x86_64__

typedef struct {
    void * for_alignment; // this just keeps the stack 16-byte aligned, shouldn't ever have to look at this value
    void * prevPseudoFrame;
    void * orig_rbx;
    PrtCodeAddress return_eip;
} m2u_stack_structure;

typedef struct {
    void * for_alignment; // this just keeps the stack 16-byte aligned, shouldn't ever have to look at this value
    void * prevPseudoFrame;
    void * orig_rbx;
    PrtCodeAddress return_eip;
} yield_stack_structure;

#else  // __x86_64__

typedef struct {
    void * unmanagedFunc;
    void * argStart;
    void * argSize;
    void * callingConvention;
} m2u_part;

typedef struct {
    m2u_part from_esp;
    void * orig_ebx;
    PrtCodeAddress return_eip;
#ifndef NO_P2C_TH
    void * taskHandle;
#endif
    void * prevPseudoFrame;
} m2u_stack_structure;

typedef struct {
    void * unmanagedFunc;
    void * argStart;
    void * argSize;
    void * callingConvention;
} y_part;

typedef struct {
    y_part from_esp;
    void * orig_ebx;
    PrtCodeAddress return_eip;
#ifndef NO_P2C_TH
    void * taskHandle;
#endif
    void * prevPseudoFrame;
} yield_stack_structure;
#endif // __x86_64__

typedef struct {
    char prt_reserved[PILLAR_VSE_SIZE];
    void (*realM2uUnwinder)(void*,void*);
    void *latest_pseudo_frame;
    void *rip_estimate;
} p2c_runtime_vse;

void pillar2c_m2u_unwinder(struct PrtStackIterator *si, void *lvse) {
    p2c_runtime_vse *prv = (p2c_runtime_vse*)lvse;

    // Note: the callee-save register values are incorrect, but they will be fixed after the M2U unwind.
#ifdef __x86_64__
    pillar2c_pseudo_stack_entry *prev_pseudo;
    prev_pseudo = (pillar2c_pseudo_stack_entry*)prv->latest_pseudo_frame;
//    PrtCodeAddress *new_rip = NULL;
//    new_rip     = (unsigned char**)(prev_pseudo->fake_eip_target);

// The problem is that we need the rip of the current call site, not the one from the pseudo-frame.

    prtSetStackIteratorFields(si,
                              prv->rip_estimate,
                              /*esp*/    (PrtRegister)prv->latest_pseudo_frame,
                              /*rbxPtr*/ si->rbxPtr,
                              /*rbpPtr*/ si->rbpPtr,
                              /*r12Ptr*/ si->r12Ptr,
                              /*r13Ptr*/ si->r13Ptr,
                              /*r14Ptr*/ si->r14Ptr,
                              /*r15Ptr*/ si->r15Ptr,
                              /*vsh*/    (PrtVseHandle)lvse,
                              /*virtualFrameNumber*/ 0);
#else  // __x86_64__
    prtSetStackIteratorFields(si,
                              prv->rip_estimate,
                              /*esp*/    (PrtRegister)prv->latest_pseudo_frame,
                              /*ebxPtr*/ si->ebxPtr,
                              /*ebpPtr*/ si->ebpPtr,
                              /*esiPtr*/ si->esiPtr,
                              /*ediPtr*/ si->ediPtr,
                              /*vsh*/    (PrtVseHandle)lvse,
                              /*virtualFrameNumber*/ 0);
#endif // __x86_64__
}

#ifdef __x86_64__

#ifdef PREV_OPT
#error __x86_64__ mode does not support the prev optimization.
#endif // PREV_OPT

#else // __x86_64__

#ifdef NO_P2C_TH
#define PREV_OFFSET_FROM_RIP_ADDR 4
#else  // NO_P2C_TH
#define PREV_OFFSET_FROM_RIP_ADDR 8
#endif // NO_P2C_TH

#endif // __x86_64

static void pillar2c_get_prev_frame(struct PrtStackIterator * si,
                                    PrtCimSpecificDataType opaque) {
    pillar2c_pseudo_stack_entry *cur_pseudo, *prev_pseudo;
    PrtCodeAddress *new_rip = NULL;

#ifdef __x86_64__

    cur_pseudo  = (pillar2c_pseudo_stack_entry*)si->rsp;
    new_rip     = (unsigned char**)(cur_pseudo->fake_eip_target);
    prev_pseudo = cur_pseudo->prev;
    if(prev_pseudo > (void*)1 && prev_pseudo < cur_pseudo) {
        printf("Previous pseudo-frame %p has a lower address than the current pseudo-frame %p.\n",prev_pseudo,cur_pseudo);
        exit(-1);
    }
    if(!prev_pseudo) {
        prtSetStackIteratorFields(si,
              /*ripPtr*/ NULL,
              /*rsp*/    NULL,
              /*rbxPtr*/ NULL,
              /*rbpPtr*/ NULL,
              /*r12Ptr*/ NULL,
              /*r13Ptr*/ NULL,
              /*r14Ptr*/ NULL,
              /*r15Ptr*/ NULL,
              /*vsh*/    si->vsh,
              /*virtualFrameNumber*/ 0);
        return;
    }
    prtSetStackIteratorFields(si,
                  /*ripPtr*/ (unsigned char**)new_rip,
                  /*rsp*/    (char*)prev_pseudo,
                  /*rbxPtr*/ NULL,
                  /*rbpPtr*/ NULL,
                  /*r12Ptr*/ NULL,
                  /*r13Ptr*/ NULL,
                  /*r14Ptr*/ NULL,
                  /*r15Ptr*/ NULL,
                  /*vsh*/    si->vsh,
                  /*virtualFrameNumber*/ 0);

#else  // __x86_64__

    cur_pseudo  = (pillar2c_pseudo_stack_entry*)si->esp;
    new_rip     = (unsigned char**)(cur_pseudo->fake_eip_target);
#ifdef PREV_OPT
    prev_pseudo = *((pillar2c_pseudo_stack_entry **)(((char*)cur_pseudo->fake_eip_target) + PREV_OFFSET_FROM_RIP_ADDR));
#else
    prev_pseudo = cur_pseudo->prev;
#endif
    if(prev_pseudo > (void*)1 && prev_pseudo < cur_pseudo) {
        printf("Previous pseudo-frame %p has a lower address than the current pseudo-frame %p.\n",prev_pseudo,cur_pseudo);
        exit(-1);
    }
    if(!prev_pseudo) {
        prtSetStackIteratorFields(si,
              /*eipPtr*/ NULL,
              /*esp*/    NULL,
              /*ebxPtr*/ NULL,
              /*ebpPtr*/ NULL,
              /*esiPtr*/ NULL,
              /*ediPtr*/ NULL,
              /*vsh*/    si->vsh,
              /*virtualFrameNumber*/ 0);
        return;
    }
    prtSetStackIteratorFields(si,
                  /*eipPtr*/ (unsigned char**)new_rip,
                  /*esp*/    (char*)prev_pseudo,
                  /*ebxPtr*/ NULL,
                  /*ebpPtr*/ NULL,
                  /*esiPtr*/ NULL,
                  /*ediPtr*/ NULL,
                  /*vsh*/    si->vsh,
                  /*virtualFrameNumber*/ 0);
#endif // __x86_64__
} // pillar2c_get_prev_frame

static void pillar2c_enumerate_roots(struct PrtStackIterator *si,
                                     struct PrtRseInfo *rootSetInfo,
                                     PrtCimSpecificDataType opaqueData) {
    unsigned ref_index;

    pillar2c_pseudo_stack_entry *pseudo;
#ifdef __x86_64__
    pseudo = (pillar2c_pseudo_stack_entry*)si->rsp;
    if(pseudo->ref_mask_ptr) {
        unsigned ref_len = pseudo->ref_mask_ptr->length;
        for(ref_index = 0; ref_index < ref_len; ++ref_index) {
            if(pseudo->ref_mask_ptr->refs[ref_index].active == 1) {
                rootSetInfo->callback(rootSetInfo->env,
	                &(pseudo->refs[ref_index]),
	                pseudo->ref_mask_ptr->refs[ref_index].tag,
                    pseudo->ref_mask_ptr->refs[ref_index].id);
            } else if(pseudo->ref_mask_ptr->refs[ref_index].active != 0) {
                printf("Root enumeration of refs on the stack is not supported in __x86_64__.\n");
                exit(-1);
            }
        }
    }
#else  // __x86_64__
    pseudo = (pillar2c_pseudo_stack_entry*)si->esp;
    if(pseudo->ref_mask_ptr) {
        unsigned ref_len = pseudo->ref_mask_ptr->length;
        for(ref_index = 0; ref_index < ref_len; ++ref_index) {
            if(pseudo->ref_mask_ptr->refs[ref_index].active == 1) {
                rootSetInfo->callback(rootSetInfo->env,
	                &(pseudo->refs[ref_index]),
	                pseudo->ref_mask_ptr->refs[ref_index].tag,
                    pseudo->ref_mask_ptr->refs[ref_index].id);
            }
            if(pseudo->ref_mask_ptr->refs[ref_index].active > PREV_OFFSET_FROM_RIP_ADDR) {
                void **ref_param_ptr = (void **) (   ((char*)pseudo->fake_eip_target) + pseudo->ref_mask_ptr->refs[ref_index].active );
                rootSetInfo->callback(rootSetInfo->env,
	                ref_param_ptr,
	                pseudo->ref_mask_ptr->refs[ref_index].tag,
                    pseudo->ref_mask_ptr->refs[ref_index].id);
            }
        }
    }
#endif // __x86_64__
} // pillar2c_enumerate_roots

// ===============================================================================

pillar2c_pseudo_stack_entry * pillar2c_get_last_pseudo(void) {
    struct PrtStackIterator _si;
    struct PrtStackIterator *si = &_si;
    m2u_stack_structure *m2uss;

    prtYoungestActivationFromUnmanagedInTask(si,prtGetTaskHandle());

#ifdef __x86_64__
    m2uss = (m2u_stack_structure*)(((char *)si->rsp));
    return m2uss->prevPseudoFrame;
#else  // __x86_64__
    m2uss = (m2u_stack_structure*)(((char *)si->esp) - sizeof(m2u_part));
    return m2uss->prevPseudoFrame;
#endif // __x86_64__
} // pillar2c_get_last_pseudo

static void pillar2c_m2u_get_prev_frame(struct PrtStackIterator * si,
                                       PrtCimSpecificDataType opaque) {
#ifdef __x86_64__
    m2u_stack_structure *m2uss = (m2u_stack_structure*)(((char *)si->rsp));
    prtSetStackIteratorFields(si,
                  /*ripPtr*/ (unsigned char**)&(m2uss->return_eip),
                  /*rsp*/    (char*)m2uss->prevPseudoFrame,
                  /*rbxPtr*/ NULL,
                  /*rbpPtr*/ NULL,
                  /*r12Ptr*/ NULL,
                  /*r13Ptr*/ NULL,
                  /*r14Ptr*/ NULL,
                  /*r15Ptr*/ NULL,
                  /*vsh*/    si->vsh,
                  /*virtualFrameNumber*/ 0);
#else  // __x86_64__
    m2u_stack_structure *m2uss = (m2u_stack_structure*)(((char *)si->esp) - sizeof(m2u_part));
    prtSetStackIteratorFields(si,
                  /*eipPtr*/ (unsigned char**)&(m2uss->return_eip),
                  /*esp*/    (char*)m2uss->prevPseudoFrame,
                  /*ebxPtr*/ NULL,
                  /*ebpPtr*/ NULL,
                  /*esiPtr*/ NULL,
                  /*ediPtr*/ NULL,
                  /*vsh*/    si->vsh,
                  /*virtualFrameNumber*/ 0);
#endif // __x86_64__
} // pillar2c_m2u_get_prev_frame

static void pillar2c_m2u_enumerate_roots(struct PrtStackIterator *si,
                                         struct PrtRseInfo *rootSetInfo,
                                         PrtCimSpecificDataType opaqueData) {
    return;
} // pillar2c_m2u_enumerate_roots

// ===============================================================================

static void pillar2c_yield_get_prev_frame(struct PrtStackIterator * si,
                                       PrtCimSpecificDataType opaque) {
#ifdef __x86_64__
    yield_stack_structure *yss = (yield_stack_structure*)(((char *)si->rsp));
    prtSetStackIteratorFields(si,
                  /*ripPtr*/ (unsigned char**)&(yss->return_eip),
                  /*rsp*/    (char*)yss->prevPseudoFrame,
                  /*rbxPtr*/ NULL,
                  /*rbpPtr*/ NULL,
                  /*r12Ptr*/ NULL,
                  /*r13Ptr*/ NULL,
                  /*r14Ptr*/ NULL,
                  /*r15Ptr*/ NULL,
                  /*vsh*/    si->vsh,
                  /*virtualFrameNumber*/ 0);
#else  // __x86_64__
    yield_stack_structure *yss = (yield_stack_structure*)(((char *)si->esp) - sizeof(y_part));
    prtSetStackIteratorFields(si,
                  /*eipPtr*/ (unsigned char**)&(yss->return_eip),
                  /*esp*/    (char*)yss->prevPseudoFrame,
                  /*ebxPtr*/ NULL,
                  /*ebpPtr*/ NULL,
                  /*esiPtr*/ NULL,
                  /*ediPtr*/ NULL,
                  /*vsh*/    si->vsh,
                  /*virtualFrameNumber*/ 0);
#endif // __x86_64__
} // pillar2c_yield_get_prev_frame

static void pillar2c_yield_enumerate_roots(struct PrtStackIterator *si,
                                         struct PrtRseInfo *rootSetInfo,
                                         PrtCimSpecificDataType opaqueData) {
    return;
} // pillar2c_yield_enumerate_roots

// ===============================================================================
#if 0

static void pillar2c_wb_get_prev_frame(struct PrtStackIterator * si,
                                       PrtCimSpecificDataType opaque) {
    write_barrier_stack_structure *wbss = (write_barrier_stack_structure*)(((char*)si->esp) - sizeof(wb_part));
    prtSetStackIteratorFields(si,
                  /*eipPtr*/ (unsigned char**)&(wbss->return_eip),
                  /*esp*/    (char*)wbss->prevPseudoFrame,
                  /*ebxPtr*/ NULL,
                  /*ebpPtr*/ NULL,
                  /*esiPtr*/ NULL,
                  /*ediPtr*/ NULL,
                  /*vsh*/    si->vsh,
                  /*virtualFrameNumber*/ 0);
} // pillar2c_wb_get_prev_frame

static void pillar2c_wb_enumerate_roots(struct PrtStackIterator *si,
                                        struct PrtRseInfo *rootSetInfo,
                                        PrtCimSpecificDataType opaqueData) {
    // intentionally empty
} // pillar2c_wb_enumerate_roots

// ===============================================================================

static void pillar2c_wbi_get_prev_frame(struct PrtStackIterator * si,
                                        PrtCimSpecificDataType opaque) {
    write_barrier_stack_structure *wbss = (write_barrier_stack_structure*)(((char*)si->esp) - sizeof(wb_part));
    prtSetStackIteratorFields(si,
                  /*eipPtr*/ (unsigned char**)&(wbss->return_eip),
                  /*esp*/    (char*)wbss->prevPseudoFrame,
                  /*ebxPtr*/ NULL,
                  /*ebpPtr*/ NULL,
                  /*esiPtr*/ NULL,
                  /*ediPtr*/ NULL,
                  /*vsh*/    si->vsh,
                  /*virtualFrameNumber*/ 0);
} // pillar2c_wbi_get_prev_frame

static void pillar2c_wbi_enumerate_roots(struct PrtStackIterator *si,
                                         struct PrtRseInfo *rootSetInfo,
                                         PrtCimSpecificDataType opaqueData) {
    // intentionally empty
} // pillar2c_wbi_enumerate_roots

#endif // 0

// ===============================================================================

typedef struct {
    void * func;
    void * argStart;
    unsigned argSize;
    PrtPcallArgEnumerator enumerator;
}  pillar2c_pcall_target_args;

void pillar2c_pcall_arg_enumerator(PrtCodeAddress func, pillar2c_pcall_target_args *args, struct PrtRseInfo *rse, PrtCimSpecificDataType opaque) {
	if(args->enumerator) {
		args->enumerator(func, args->argStart, rse, opaque);
	}
}

void pillar2cAtExit(void) {
    if(g_pillar2c_managed_calls) {
        printf("Pillar2c: number of managed calls = %lld\n", g_pillar2c_managed_calls);
    }
    if(g_pillar2c_managed_call_stmt_overhead) {
        printf("Pillar2c: number of prolog statements executed in managed calls = %lld\n", g_pillar2c_managed_call_stmt_overhead);
    }
}

int main(int argc, char ** argv) {
    PrtCodeInfoManager manager;
    PrtCodeInfoManager m2u_manager;
    PrtCodeInfoManager yield_manager;
//    PrtCodeInfoManager wb_manager;
//    PrtCodeInfoManager wbi_manager;
    struct PrtCodeInfoManagerFunctions cimfuncs;
    struct PrtCodeInfoManagerFunctions cim_m2u_funcs;
    struct PrtCodeInfoManagerFunctions cim_yield_funcs;
//    struct PrtCodeInfoManagerFunctions cim_wb_funcs;
//    struct PrtCodeInfoManagerFunctions cim_wbi_funcs;

    atexit(pillar2cAtExit);

#if 0
    static int count = 0;
    if(count == 0) {
        ++count;
    } else {
        ++count;
        printf("main, count = %d\n",count);
        __asm { int 3 }
    }
#endif

    prtInit();

    memset(&cimfuncs, 0, sizeof(cimfuncs));
    cimfuncs.cimGetPreviousFrame = pillar2c_get_prev_frame;
    cimfuncs.cimEnumerateRoots   = pillar2c_enumerate_roots;

    manager = prtRegisterCodeInfoManager("pillar2c_translator", cimfuncs);
    prtAddCodeRegion(manager,
                    (PrtCodeAddress)__prt_code_start,
                    (PrtCodeAddress)__prt_code_end, 0);

    // ===============================================================

    memset(&cim_m2u_funcs, 0, sizeof(cim_m2u_funcs));
    cim_m2u_funcs.cimGetPreviousFrame = pillar2c_m2u_get_prev_frame;
    cim_m2u_funcs.cimEnumerateRoots   = pillar2c_m2u_enumerate_roots;

    m2u_manager = prtRegisterCodeInfoManager("pillar2c_m2u", cim_m2u_funcs);
    prtAddCodeRegion(m2u_manager,
                    (PrtCodeAddress)&pillar2cInvokeUnmanagedStart,
                    (PrtCodeAddress)((char*)&pillar2cInvokeUnmanagedDestructor - 1), 0);

    // ===============================================================

    memset(&cim_yield_funcs, 0, sizeof(cim_yield_funcs));
    cim_yield_funcs.cimGetPreviousFrame = pillar2c_yield_get_prev_frame;
    cim_yield_funcs.cimEnumerateRoots   = pillar2c_yield_enumerate_roots;

    yield_manager = prtRegisterCodeInfoManager("pillar2c_yield", cim_yield_funcs);
    prtAddCodeRegion(yield_manager,
                    (PrtCodeAddress)&pillar2cYieldStart,
                    (PrtCodeAddress)((char*)&pillar2cYieldDestructor - 1), 0);

    // ===============================================================

#if 0
    memset(&cim_wb_funcs, 0, sizeof(cim_wb_funcs));
    cim_wb_funcs.cimGetPreviousFrame = pillar2c_wb_get_prev_frame;
    cim_wb_funcs.cimEnumerateRoots   = pillar2c_wb_enumerate_roots;

    wb_manager = prtRegisterCodeInfoManager("pillar2c_wb", cim_wb_funcs);
    prtAddCodeRegion(wb_manager,
                    (PrtCodeAddress)&pillar2c_write_barrier_start,
                    (PrtCodeAddress)((char*)&pillar2c_write_barrier_destructor - 1), 0);

    // ===============================================================

    memset(&cim_wbi_funcs, 0, sizeof(cim_wbi_funcs));
    cim_wbi_funcs.cimGetPreviousFrame = pillar2c_wbi_get_prev_frame;
    cim_wbi_funcs.cimEnumerateRoots   = pillar2c_wbi_enumerate_roots;

    wbi_manager = prtRegisterCodeInfoManager("pillar2c_wbi", cim_wbi_funcs);
    prtAddCodeRegion(wbi_manager,
                    (PrtCodeAddress)&pillar2c_write_barrier_interior_start,
                    (PrtCodeAddress)((char*)&pillar2c_write_barrier_interior_destructor - 1), 0);
#endif
    // ===============================================================

    g_pillar2c_tls_offset = ptkGetNextTlsOffset(PILLAR2C_TLS_SLOTS);

    prtStart(pillar2c_main, argc, argv);
    return 0;
}

// Must be the last function in the file.
void __prt_code_start(void) {}

void PILLAR2C_RT_STDCALL pillar2c_main(int argc, char **argv) {
    g_pillar_tls_offset = 4;
#ifdef NO_P2C_TH
    pillar_main(NULL,argc,argv);
#else
    pillar_main(prtGetTaskHandle(),NULL,argc,argv);
#endif
}
