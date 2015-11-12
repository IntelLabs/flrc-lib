/*
 * COPYRIGHT_NOTICE_1
 */

/* $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/interface/prtcodegenerator.h,v 1.9 2013/02/15 21:21:01 taanders Exp $ */

/*
 * This interface header file is a companion to the main Pillar Runtime interface header file prt.h. It is intended for
 * use only by code generators: components that create Pillar code such as compilers. Code generators emit (managed) code
 * that use functions that (normally unmanaged) LSRs never use such as prtGetStackLimit() and prtInvokeUnmanagedFunc().
 */

#ifndef _PRTCODEGENERATOR_H
#define _PRTCODEGENERATOR_H

/* The following #ifdef __cplusplus/extern "C" tries to enforce having only C code in the interface files. */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#pragma pillar_managed(off)
# include <setjmp.h>
#pragma pillar_managed(on)

#include "prt/prt.h"

typedef enum {
    PrtCcCdecl = 0,
    PrtCcStdcall = 1
} PrtCallingConvention; /* enum PrtCallingConvention */

PILLAR_EXPORT PRT_CALL_FROM_MANAGED void __pdecl prtInvokeUnmanagedFunc(
                                                      PRT_IN PrtCodeAddress unmanagedFunc,
                                                      PRT_IN void *argStart,
                                                      PRT_IN unsigned argSize,  // in pointer-sized words
                                                      PRT_IN PrtCallingConvention callingConvention);

/* Must be called from managed code. */
PILLAR_EXPORT PRT_CALL_FROM_MANAGED void __pdecl prtYield(void);

typedef PrtBool CDECL_FUNC_OUT (CDECL_FUNC_IN *PrtPredicate)(volatile void *location, void *data);
#ifdef _WINDOWS
typedef unsigned __int64 PrtTimeCycles64;
#else
typedef unsigned long long PrtTimeCycles64;
#endif // _WINDOWS

typedef unsigned PrtSyncResult;

#define PrtSyncResultSuccess 0
#define PrtSyncResultTimeout 1

#define PrtInfiniteWaitCycles64  (0xFFFFFFFFFFFFFFFFLL)

PILLAR_EXPORT PRT_CALL_FROM_MANAGED PrtSyncResult PRT_CDECL prtYieldUntilMovable(PrtPredicate predicate,
                                                               volatile void *location,
                                                               void *value,
                                                               PrtTimeCycles64 timeout,
                                                               PrtGcTag tag,
                                                               void *gcTagParameter);

PILLAR_EXPORT PRT_CALL_FROM_PILLAR PrtSyncResult PRT_CDECL prtYieldUntil(PrtPredicate predicate,
                                                               volatile void *location,
                                                               void *value,
                                                               PrtTimeCycles64 timeout);

/*
 ***********************************************************************************************
 * Code Info Manager functions
 ***********************************************************************************************
 */

typedef struct PrtCodeInfoManagerStruct *PrtCodeInfoManager;
/* Type of client-specified data that a client can associate with each code range with a prtCodeInfoManager. */

/* This enum allows code to refer to specific registers, particularly during
 * stack unwinding.  The specific values are chosen to correspond to the
 * registers' offsets into the PrtStackIterator structure.
 */

enum PrtStackIteratorRegister {
#ifdef __x86_64__
    PrtSiRegisterBaseRsp = offsetof(struct PrtStackIterator,rsp),
    PrtSiRegisterBaseVsh = offsetof(struct PrtStackIterator,vsh),
    PrtSiRegisterBaseRbp = offsetof(struct PrtStackIterator,rbpPtr),
    PrtSiRegisterBaseRbx = offsetof(struct PrtStackIterator,rbxPtr),
    PrtSiRegisterBaseR12 = offsetof(struct PrtStackIterator,r12Ptr),
    PrtSiRegisterBaseR13 = offsetof(struct PrtStackIterator,r13Ptr),
    PrtSiRegisterBaseR14 = offsetof(struct PrtStackIterator,r14Ptr),
    PrtSiRegisterBaseR15 = offsetof(struct PrtStackIterator,r15Ptr),
    PrtSiRegisterBaseRip = offsetof(struct PrtStackIterator,ripPtr),
#else  // __x86_64__
    PrtSiRegisterBaseEsp = offsetof(struct PrtStackIterator,esp),
    PrtSiRegisterBaseVsh = offsetof(struct PrtStackIterator,vsh),
    PrtSiRegisterBaseEbp = offsetof(struct PrtStackIterator,ebpPtr),
    PrtSiRegisterBaseEbx = offsetof(struct PrtStackIterator,ebxPtr),
    PrtSiRegisterBaseEsi = offsetof(struct PrtStackIterator,esiPtr),
    PrtSiRegisterBaseEdi = offsetof(struct PrtStackIterator,ediPtr),
    PrtSiRegisterBaseEip = offsetof(struct PrtStackIterator,eipPtr),
#endif // __x86_64__
    PrtSiRegisterBaseNone = -1
}; /* enum PrtStackIteratorRegister */

struct PrtRegisterAdjustment {
    enum PrtStackIteratorRegister base;
    int adjustment; /* How many bytes to add to the base register */
}; /* struct PrtRegisterAdjustment */

struct PrtEipUnwinder {
#ifdef __x86_64__
    struct PrtRegisterAdjustment rspAdjustment;
    struct PrtRegisterAdjustment rbpAdjustment;
    struct PrtRegisterAdjustment ripAdjustment;
    struct PrtRegisterAdjustment r12Adjustment;
    struct PrtRegisterAdjustment r13Adjustment;
    struct PrtRegisterAdjustment r14Adjustment;
    struct PrtRegisterAdjustment r15Adjustment;
    struct PrtRegisterAdjustment rbxAdjustment;
    int vshFramesToPop; /* How many VSEs to pop from vsh */
#else  // __x86_64__
    struct PrtRegisterAdjustment espAdjustment;
    struct PrtRegisterAdjustment ebpAdjustment;
    struct PrtRegisterAdjustment eipAdjustment;
    struct PrtRegisterAdjustment ediAdjustment;
    struct PrtRegisterAdjustment esiAdjustment;
    struct PrtRegisterAdjustment ebxAdjustment;
    int vshFramesToPop; /* How many VSEs to pop from vsh */
#endif // __x86_64__
}; /* struct PrtEipUnwinder */

struct PrtRseDescriptor {
    union { enum PrtStackIteratorRegister registerId; int stackOffset; } offset;
    PrtGcTag tag;
    void *parameter;
}; /* struct PrtRseDescriptor */

struct PrtEipRse {
    unsigned numRegisterRoots;
    unsigned numStackEspRoots;
    struct PrtRseDescriptor *registerRoots;
    struct PrtRseDescriptor *stackEspRoots;
}; /* struct PrtEipRse */

 /*
  * A set of callback functions implemented by a producer of managed code (e.g., a JIT or static compiler) that the PRT
  * can call as part of its implementation of stack walking, root set enumeration, exception handling, etc. These are
  * used to define a CodeInfoManager.
  */
struct PrtCodeInfoManagerFunctions {
    /* Returns a string describing the current frame. */
    char * CDECL_FUNC_OUT (CDECL_FUNC_IN *cimGetStringForFrame)(PRT_IN struct PrtStackIterator *si,
                                          PRT_OUT char *buffer,
                                          PRT_IN size_t bufferSize,
                                          PRT_IN PrtCimSpecificDataType opaqueData);
    /* Modifies si to hold information about the frame previous to the current one. Returns True if this was successful,
       and False if not. opaqueData is the same as the opaqueData that was passed to Pillar when the CIM registered
       this code region. */
    void CDECL_FUNC_OUT (CDECL_FUNC_IN *cimGetPreviousFrame)(PRT_INOUT struct PrtStackIterator *si,
                                        PRT_IN    PrtCimSpecificDataType opaqueData);
    /* Enumerates the live GC roots of the current stack frame.  The activation is guaranteed to be
       suspended at a call site. */
    void CDECL_FUNC_OUT (CDECL_FUNC_IN *cimEnumerateRoots)(PRT_IN struct PrtStackIterator *si,
                                      PRT_IN struct PrtRseInfo *rootSetInfo,
                                      PRT_IN PrtCimSpecificDataType opaqueData);
    /* Enumerates the live GC roots that are arguments to a closure or to a continuation.
     * If the starting address of a func is given then closureArgs may point to a structure that contains a root.
     * If a continuation address if given then closureArgs points to the first arg of the continuation and the roots
     * there are likely to be directly there rather than via another structure. */
    void CDECL_FUNC_OUT (CDECL_FUNC_IN *cimEnumerateClosureRoots)(PRT_IN PrtCodeAddress closureFunc,
                                             PRT_IN void *closureArgs,
                                             PRT_IN struct PrtRseInfo *rootSetInfo,
                                             PRT_IN PrtCimSpecificDataType opaqueData);
    /* Returns a pointer to an initialized continuation corresponding to the nth (n=continuationIndex) continuation
       listed in the "also unwinds to" clause of the code corresponding to the given stack iterator.
       The vsh field of the continuation should be taken from the stack iterator, and the callee-save register
       values from the stack iterator can also be stored somewhere in the continuation structure so that
       the corresponding registers can be restored in the continuation code.  Note that this function does
       not set any other continuation arguments. */
    struct PrtContinuation * CDECL_FUNC_OUT (CDECL_FUNC_IN *cimGetUnwindContinuation)(PRT_IN struct PrtStackIterator *si,
                                                                PRT_IN unsigned continuationIndex,
                                                                PRT_IN PrtCimSpecificDataType opaqueData);
	/* Returns the value associated with the smallest span tagged with "key" and containing the program point
       where "si" is suspended.  "opaqueData" is the same data that the CIM passed to Pillar when it registered
       the code region containing si's program point.  Returns (PrtDataPointer)NULL if there is no such span. */
	PrtDataPointer CDECL_FUNC_OUT (CDECL_FUNC_IN *cimGetSpanDescriptor)(PRT_IN struct PrtStackIterator *si,
                                                   PRT_IN unsigned key,
                                                   PRT_IN PrtCimSpecificDataType opaqueData);
}; /* struct PrtCodeInfoManagerFunctions */


/* Creates a new PrtCodeInfoManager and registers a set of callback functions belonging to it. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtCodeInfoManager PRT_CDECL prtRegisterCodeInfoManager(
                                                           PRT_IN const char *theCimName,
                                                           PRT_IN struct PrtCodeInfoManagerFunctions theCimFns);

/*
 * Register the block of code in [start..end] with the given PrtCodeInfoManager.
 * Returns True on success and False on failure (e.g., if the code's region overlaps that of already-registered code).
 * The opaqueData parameter is associated with this code region and passed back to the Cim during each of the calls
 * to the Cim for this code region.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtAddCodeRegion(
                                                PRT_IN PrtCodeInfoManager theCim,
                                                PRT_IN PrtCodeAddress start,
                                                PRT_IN PrtCodeAddress end,
                                                PRT_IN PrtCimSpecificDataType opaqueData);

PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtAddEipUnwinder(PrtCodeAddress eip, struct PrtEipUnwinder *descriptor);

PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtAddEipRse(PrtCodeAddress eip, struct PrtEipRse *descriptor);

struct PrtMutex;

PILLAR_EXPORT void __pcdecl enterUnmanagedCode(PrtTaskHandle task, PrtVseHandle value);
PILLAR_EXPORT void __pcdecl reenterManagedCode(PrtTaskHandle task);
PILLAR_EXPORT void __pcdecl enterManagedCode(PrtTaskHandle task);
PILLAR_EXPORT void __pcdecl reenterUnmanagedCode(PrtTaskHandle task);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _PRTCODEGENERATOR_H */
