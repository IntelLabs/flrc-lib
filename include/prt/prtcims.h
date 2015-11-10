/*
 * COPYRIGHT_NOTICE_1
 */

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/include/prtcims.h,v 1.4 2012/01/17 21:09:12 taanders Exp $

#ifndef _PRTCIMS_H
#define _PRTCIMS_H

#include "prt/prt.h"

//////////////////////////////////////////////////////////////////////////////////
// A variety of different VSEs are used to record information about the different
// frames on a Pillar task's stack.
//////////////////////////////////////////////////////////////////////////////////

extern PrtVseType PRT_VSE_M2U, PRT_VSE_PCALL;

typedef PrtRegister Prt_StackArgument;


// The structures corresponding to frames for the regular ia32 stubs.
// The format of these structures MUST MATCH that of the stubs contained in prtstubs.asm.

struct Prt_SmallVseProlog
{
    // Callee-saved registers.
#ifdef __x86_64__
    PrtRegister r15;
    PrtRegister r14;
    PrtRegister r13;
    PrtRegister r12;
    PrtRegister rbx;
    PrtRegister rbp;
#else  // __x86_64__
    PrtRegister edi;
    PrtRegister esi;
    PrtRegister ebx;
    PrtRegister ebp;
#endif // __x86_64__
}; // struct Prt_SmallProlog


struct Prt_DefaultVseProlog
{
    struct Prt_SmallVseProlog calleeSaves;
    PrtCodeAddress eip; // return address...get this implicitly and not with an explicit push...also popped automatically
}; // struct Prt_DefaultVseProlog


struct Prt_PcallVse : public Prt_Vse
{
    struct Prt_DefaultVseProlog prolog;
#ifndef __x86_64__
    Prt_StackArgument           initialProc;   // PrtCodeAddress
    Prt_StackArgument           argStart;      // void *
    Prt_StackArgument           argSize;       // unsigned
    Prt_StackArgument           affinity;      // PrtAffinityProcessorId
#endif // !__x86_64__
}; // struct Prt_PcallVse


struct Prt_M2uUnwindContinuation
{
    PrtCodeAddress eip;
    PrtVseHandle   vsh;
#ifdef __x86_64__
    PrtRegister    arg;     // 64 bits for passing a return value
#else  // __x86_64__
    PrtRegister    args[2]; // 64 bits for passing a return value
#endif // __x86_64__
}; //struct Prt_M2uUnwindContinuation


struct Prt_M2uFrame : public Prt_Vse
{
//    PrtRegister                      callsite_id;      // use this field if you have multiple callsites in M2U function
    void (*realM2uUnwinder)(PrtStackIterator *,Prt_Vse *);
    struct Prt_M2uUnwindContinuation unwindCont;
    struct Prt_DefaultVseProlog      prolog;
#ifndef __x86_64__
    Prt_StackArgument                initialProc;      // PrtCodeAddress
    Prt_StackArgument                argStart;         // void *
    Prt_StackArgument                argSize;          // unsigned
    Prt_StackArgument                targetIsStdcall;  // PrtCallingConvention
#endif // !__x86_64__
}; //struct Prt_M2uFrame


void prt_registerBuiltinCodeInfoManagers(void);
void prt_unwindToLatestM2u(PRT_INOUT PrtStackIterator *si, PRT_IN Prt_Vse *initialVsh);


////////////////////////////////////////////////////////////////////////////////
// Externs for stubs and related addresses in prtstubs.asm
// When using these symbols you probably want to take the address (&)
// of the symbol since this is probably what you really wanted.
////////////////////////////////////////////////////////////////////////////////

extern "C" {
    // instruction addresses in the prtInvokeManagedFunc function.
    extern void * prt_InvokeManagedFuncStart;
    extern void * prt_InvokeManagedFuncUnwindContinuation;
    extern void * prt_InvokeManagedFuncEnd;

    // instruction addresses in the prtInvokeUnmanagedFunc function.
    extern void * prt_InvokeUnmanagedFuncStart;
    extern void * prt_InvokeUnmanagedFuncPostCall;
    extern void * prt_InvokeUnmanagedFuncDestructor;
    extern void * prt_InvokeUnmanagedFuncUnwindContinuation;
    extern void * prt_InvokeUnmanagedFuncEnd;

    // instruction addresses for pcall (VSE destructor only)
    extern void * prt_PcallDestructor;

    extern void * prt_YieldStart;
    extern void * prt_YieldEnd;

    extern void * prt_bootstrapTaskAsmCall;

	extern void * prt_WatermarkPrototypeStart;
	extern void * prt_WatermarkPostTopIndex1;
#ifndef __x86_64__
	extern void * prt_WatermarkPostTopIndex2;
#endif // __x86_64__
	extern void * prt_WatermarkPostStubStackStart;
	extern void * prt_WatermarkPostStubStart;
	extern void * prt_WatermarkPostRealEipMove;
	extern void * prt_WatermarkPrototypeEnd;
}

#endif // !_PRTCIMS_H
