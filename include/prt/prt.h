/*
 * COPYRIGHT_NOTICE_1
 */

/* $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/interface/prt.h,v 1.25 2013/02/04 19:12:23 taanders Exp $ */

/*
 * This is the main Pillar Runtime interface header file. A companion header file, prtcodegenerator.h, declares
 * additional items that are normally only needed by code generators: components that create Pillar code such as compilers.
 */

#ifndef _PRT_H
#define _PRT_H

#ifdef __cplusplus
#define PRT_EXTERN_C extern "C"
#else /* !__cplusplus */
#define PRT_EXTERN_C
#endif /* !__cplusplus */

/* The following #ifdef __cplusplus/extern "C" tries to enforce having only C code in the interface files. */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stddef.h>

#ifdef __GNUC__
#define CDECL_FUNC_OUT __attribute__((cdecl))
#define CDECL_FUNC_IN
#define STDCALL_FUNC_OUT __attribute__((stdcall))
#define STDCALL_FUNC_IN
#define PDECL_FUNC_OUT __pdecl
#define PDECL_FUNC_IN
#define PRT_NORETURN __attribute__((noreturn))
#else // __GNUC__
#define CDECL_FUNC_OUT
#define CDECL_FUNC_IN __cdecl
#define STDCALL_FUNC_OUT
#define STDCALL_FUNC_IN __stdcall
#define PDECL_FUNC_OUT
#define PDECL_FUNC_IN __pdecl
#define PRT_NORETURN __declspec(noreturn)
#endif // __GNUC__
//#endif // __x86_64__

/* This is essentially a pointer-sized integer. */
typedef char *PrtRegister;
struct PrtTaskSetEnumerator;

/*
 ***********************************************************************************************
 * Procedure linkage definitions, procedure argument in/out specifications, and
 * specifications of where interface functions can be called from.
 ***********************************************************************************************
 */

#ifdef BUILDING_PILLAR

#ifdef USE_PILLAR_DLL
#define PILLAR_EXPORT PRT_EXTERN_C __declspec(dllexport)
#else  /* !USE_PILLAR_DLL */
#define PILLAR_EXPORT PRT_EXTERN_C
#endif /* !USE_PILLAR_DLL */

#else  /* !BUILDING_PILLAR_ */

#ifdef USE_PILLAR_DLL
#define PILLAR_EXPORT PRT_EXTERN_C __declspec(dllimport)
#else  /* !USE_PILLAR_DLL */
#define PILLAR_EXPORT PRT_EXTERN_C
#endif /* !USE_PILLAR_DLL */

#endif /* !BUILDING_PILLAR */


/* These are used to document function parameters to indicate whether they are IN, OUT, or INOUT. */
#define PRT_IN
#define PRT_OUT
#define PRT_INOUT

/*
 * These document where an interface function can be called from, in terms of whether from managed or unmanaged code,
 * and whether from Pillar or non-Pillar tasks.
 */
#define PRT_CALL_FROM_MANAGED       /* Denotes that this function must be called from managed code running in a Pillar task. */
#define PRT_CALL_FROM_UNMANAGED     /* Denotes that this function must be called from unmanaged code running in a Pillar task. */
#define PRT_CALL_FROM_PILLAR        /* Denotes that this function must be called from inside a Pillar task. */
#define PRT_CALL_FROM_ANYWHERE      /* Denotes that this function can be called from a Pillar or a non-Pillar task. */
#define PRT_CALL_FROM_NON_PILLAR    /* Denotes that this function must be called from a non-Pillar task. */

#ifdef _WIN32
#define PRT_STDCALL __stdcall
#define PRT_CDECL __cdecl
#elif defined __GNUC__
#define PRT_STDCALL __attribute__((stdcall))
#define PRT_CDECL __attribute__((cdecl))
#else
#error
#endif

#ifndef __pillar__
#define __pdecl PRT_STDCALL           /* Callee cleans the stack. Supports, e.g., tailcall. */
#define __pcdecl PRT_CDECL            /* Caller cleans the stack */
#endif /* !__pillar__ */

/*
 ***********************************************************************************************
 * Basic constants, definitions, and types.
 ***********************************************************************************************
 */

typedef enum { PrtFalse = 0, PrtTrue = 1 } PrtBool;

/* These are tags that the GC uses to interpret roots that are enumerated.
   Some tags require additional parameters that need to be passed in as well.
   The interface includes a single argument that can be used directly for a
   single parameter, or treated as an array or struct pointer if multiple
   parameters are required.
   Several tags are predefined for the compiler's benefit, but the high-level
   language is free to use them as well.
     "Default": The root points to the canonical object address (usually the base).
                No additional parameters are used, and the argument is ignored.
     "Offset": The root is at a known offset from the canonical object address.
               Subtract the int-type parameter from the root to get the canonical
               object address.  Example: the root points to a specific field of
               an object.
     "Base": The root is related to an object whose canonical address is found as
             the ref-type parameter.  Example: the root points to some array element
             contained within the object.
   Any tag value starting from UserDefined and beyond can be defined and used
   by the high-level language and its associated GC implementation.  The number of
   parameters, and their meanings, are up to the high-level language and GC to define.
  */
typedef enum { PrtGcTagDefault = 0, PrtGcTagOffset = 1, PrtGcTagBase = 2, PrtGcTagUserDefined = 3 } PrtGcTag;

/* Points to executable code.  It is "unsigned char *" so that arithmetic is valid. */
typedef unsigned char *PrtCodeAddress;

/* Points to a sequence of data bytes.  It is "unsigned char *" so that arithmetic is valid. */
typedef unsigned char *PrtDataPointer;

/* Forward declarations of nonexistent structures, to avoid typedef'ing to void*. */
struct PrtTaskStruct;
struct PrtCimSpecificDataStruct;
struct PrtCodeInfoManagerStruct;
struct PrtVseStruct;
struct PrtProvidedTlsStruct;

/*
 ***********************************************************************************************
 * Other Pillar types
 ***********************************************************************************************
 */

/*
 * Opaque types representing various Pillar structures.
 */
typedef struct PrtTaskStruct *             PrtTaskHandle;
typedef PrtCodeAddress                     PrtVseType;
typedef struct PrtVseStruct *              PrtVseHandle;
typedef struct PrtProvidedTlsStruct *      PrtProvidedTlsHandle;

/* Identifies a hardware thread to be "close to" in a call to, e.g.,
   prtPcall().  Thread numbering begins at 0. */
typedef unsigned PrtAffinityProcessorId;
#define PRT_NO_PROC_AFFINITY (-1)

/*
 * The context for stack frames that is used during stack walking. We use pointers in place of
 * direct values to support root set enumeration.
 * TODO: Support EM64T.
 */
struct PrtStackIterator
{
#ifdef __x86_64__
    /* Stack pointer, eip, and frame pointer */
    PrtRegister     rsp;
    PrtRegister    *rbpPtr;
    PrtCodeAddress *ripPtr;
    /* Callee-save registers */
    PrtRegister    *rbxPtr;
    PrtRegister    *r12Ptr;
    PrtRegister    *r13Ptr;
    PrtRegister    *r14Ptr;
    PrtRegister    *r15Ptr;
    /* Points to the virtual stack head (topmost VSE) current when this frame is/was the youngest on the stack. */
    PrtVseHandle vsh;
    /* The Pillar compiler may inline several "virtual" frames into a single "physical" frame.
       However, the stack iterator should maintain the illusion that no inlining was done.
       This field keeps track of which virtual frame we are at within the physical frame.
       The value 0 means the newest, innermost frame, and the value is incremented when
       doing a virtual unwind within the same physical frame. */
    long virtualFrameNumber;
#else  // __x86_64__
    /* Stack pointer, eip, and frame pointer */
    PrtRegister  esp;
    PrtRegister *ebpPtr;
    PrtCodeAddress *eipPtr;
    /* Callee-save registers */
    PrtRegister *ediPtr;
    PrtRegister *esiPtr;
    PrtRegister *ebxPtr;
    /* Points to the virtual stack head (topmost VSE) current when this frame is/was the youngest on the stack. */
    PrtVseHandle vsh;
    /* The Pillar compiler may inline several "virtual" frames into a single "physical" frame.
       However, the stack iterator should maintain the illusion that no inlining was done.
       This field keeps track of which virtual frame we are at within the physical frame.
       The value 0 means the newest, innermost frame, and the value is incremented when
       doing a virtual unwind within the same physical frame. */
    unsigned virtualFrameNumber;
#endif // __x86_64__
    /* The following are internal fields; clients should not read or write them. */
    PrtVseHandle originalVsh; /* A snapshot of the starting vsh, so the stack walk can be repeated if needed. */
	PrtTaskHandle task_for_this_stack;
	PrtCodeAddress *watermarkPtr;
}; /* struct PrtStackIterator */

/*
 * Type of procedures called once for each enumerated root. "rootAddr" is the address of a pointer-sized field
 * containing either a reference to a heap object or a managed pointer. "isMP" is true iff the root is a managed pointer
 * instead of a heap reference. "env" is an (opaque to Pillar) value that the client passed in a PrtRseInfo structure
 * in the call to, e.g., prtEnumerateTaskRootSet().
 */
typedef void CDECL_FUNC_OUT (CDECL_FUNC_IN *PrtRseCallback)(void *env, void **rootAddr, PrtGcTag tag, void *parameter);

struct PrtRseInfo
{
    PrtRseCallback callback;  /* invoked for each enumerated root */
    void *env;                /* opaque value to be passed in each call to the callback function */
}; /* struct PrtRseInfo */

typedef void CDECL_FUNC_OUT (CDECL_FUNC_IN *PrtGlobalEnumerator)(struct PrtRseInfo *);

/*
 * On callback, the first paramater is the given thread's user-level tls pointer from the thread's PrtTask structure.
 * The second parameter is the root set enumeration information including the callback and the environment.
 */
typedef void CDECL_FUNC_OUT (CDECL_FUNC_IN *PrtTlsEnumerator)(PrtProvidedTlsHandle user_level_tls, struct PrtRseInfo *rse_info);

/*
 * Code in the prtFatCutTo stub depends on the size and ordering of this struct.
 */
struct PrtContinuation
{
    PrtCodeAddress eip;
    PrtVseHandle   vsh;
    /* Argument(s) for the continuation. Additional fat cut arguments may be stored after this field. */
#pragma warning (disable : 4200) /* Turn off warning about 0 length arrays. */
    PrtRegister    args[];
#pragma warning (default : 4200)
}; /* struct PrtContinuation */

#ifdef __GNUC__
#define PrtInfiniteWait64 0xFFffFFffFFffFFffLL
#else // __GNUC__
#define PrtInfiniteWait64 (0xFFffFFffFFffFFffUL)
#endif // __GNUC__

/*
 ***********************************************************************************************
 * Pillar startup/shutdown support.
 ***********************************************************************************************
 */

/* The type of procedure passed to prtStart.  This procedure is a managed function. */
typedef void PDECL_FUNC_OUT (PDECL_FUNC_IN *PrtInitialProc)(int argc, char *argv[]);

/* This function can be called any number of times, but must be called before any other Pillar function. */
PILLAR_EXPORT PRT_CALL_FROM_NON_PILLAR void PRT_CDECL prtInit(void);

/* Call this to start the initial Pillar task specified by "proc". This function should only be called once. */
PILLAR_EXPORT PRT_CALL_FROM_NON_PILLAR void PRT_CDECL prtStart(PRT_IN PrtInitialProc proc,
                                                             PRT_IN int argc,
                                                             PRT_IN char *argv[]);

/* Sets an option in the runtime, using a string of the form "key=value".
 * Can be called multiple times, once for each option.
 * It should generally be called before prtStart.
 * Use the string "help" to print a list of available options.
 * Use 0 or 1 for boolean values.
 */
PILLAR_EXPORT PRT_CALL_FROM_NON_PILLAR void PRT_CDECL prtSetOption(PRT_IN const char *optionString);

/* This function can be called to force an exit. */
PILLAR_EXPORT PRT_NORETURN PRT_CALL_FROM_NON_PILLAR void PRT_CDECL prtExit(PRT_IN int status);

PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE unsigned PRT_CDECL prtGetNumProcessors(void);

/*
 ***********************************************************************************************
 * Pillar runtime task and synchronization functions.
 ***********************************************************************************************
 */

/*
 * Each Pillar task gets a (pointer-sized) chunk of task-local storage (TLS). Use the following
 * functions to get and set a task's TLS. If you execute a pcall and this
 * causes a new task to be created, that task will inherit the creating task's TLS value.
 * If this is the case, that TLS value will be shared by the two tasks and should be synchronized
 * unless you choose to overwrite the TLS value in the child task.
 */
PILLAR_EXPORT PRT_CALL_FROM_PILLAR PrtProvidedTlsHandle __pcdecl prtGetTls(void);
PILLAR_EXPORT PRT_CALL_FROM_PILLAR void __pcdecl prtSetTls(PRT_IN PrtProvidedTlsHandle handle);
/*
 * The following two functions are probably only temporary and they are dangerous!
 * They do no locking so only use when you know a race cannot exist!!!!!
 */
PILLAR_EXPORT PRT_CALL_FROM_PILLAR PrtProvidedTlsHandle __pcdecl prtGetTlsForTask(PrtTaskHandle pth);
PILLAR_EXPORT PRT_CALL_FROM_PILLAR void __pcdecl prtSetTlsForTask(PrtTaskHandle pth,PRT_IN PrtProvidedTlsHandle handle);

/* Gets the task handle for the current task. */
//#if defined _WIN32 && defined INLINE_GET_TASK_HANDLE
#if defined _WIN32
__inline PRT_CALL_FROM_PILLAR PrtTaskHandle __pcdecl prtGetTaskHandle(void) {
    __asm {
        mov eax, fs:[0x14]
    }
}
#else
PILLAR_EXPORT PRT_CALL_FROM_PILLAR PrtTaskHandle __pcdecl prtGetTaskHandle(void);
#endif


/* Callback type for tasks coming into and going out of existence.  Second param is true if
   thread is coming into existence, false if it is going out of existence. */
typedef void CDECL_FUNC_OUT (CDECL_FUNC_IN *PrtTaskExistenceCallback)(PrtTaskHandle new_task, PrtBool true_if_creation);
/* Register to receive a callback whenever a new task is created. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void __pcdecl prtRegisterTaskExistenceCallback(PrtTaskExistenceCallback cb);

PILLAR_EXPORT PRT_CALL_FROM_UNMANAGED void PRT_CDECL prtYieldUnmanaged(void);

/* Sets the size of the stack used for threads created from this parent thread via pcall.
   A value of 0 means use the default thread size.
   Non-zero values specify the number of bytes of stack to use but be aware if this is less than the
   minimum stack size then it will be adjusted up to that minimum. */
PILLAR_EXPORT PRT_CALL_FROM_PILLAR void __pcdecl prtSetPcallStackSize(unsigned size);
PILLAR_EXPORT PRT_CALL_FROM_PILLAR unsigned __pcdecl prtGetPcallStackSize(void);

typedef struct PrtCimSpecificDataStruct *PrtCimSpecificDataType;

typedef void (*PrtPcallArgEnumerator)
		(PrtCodeAddress func,
			void *closureArgs,
            struct PrtRseInfo *rootSetInfo,
            PrtCimSpecificDataType opaqueData);

/*
 * Pillar pcall (parallel call) task creation. Creates a new task and executes the given function
 * in parallel with the continuation (i.e., the code and execution environment that follows the pcall
 * in the Pillar source). Note that pcall does not have sequential semantics.
 * A PrtAffinityProcessorId of NO_PROC_AFFINITY indicates there is no affinity request.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE  void __pdecl prtPcall(PRT_IN PrtCodeAddress managedFunc,
                                                            PRT_IN void *argStart,
                                                            PRT_IN unsigned argSize,
                                                            PRT_IN PrtAffinityProcessorId pid,
															PRT_IN PrtPcallArgEnumerator argRefEnumerator);

PILLAR_EXPORT PRT_CALL_FROM_PILLAR PrtTaskHandle PRT_CDECL prtGetLastPcallTaskHandle(void);

typedef enum { PrtRunOnYieldSuccess  = 0,
               PrtRunOnYieldEnding   = 1,    // the thread is ending so can't run anything in it
               PrtRunOnYieldPrevious = 2     // a previous runOnYield request has not completed yet, try again later
             } PrtRunOnYieldResult;

#define RUN_FUNC_CC PRT_CDECL
//#define RUN_FUNC_CC __pdecl

PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtRunOnYieldResult __pcdecl prtRunOnYield(PrtTaskHandle task,void (RUN_FUNC_CC * function)(void *),void *arg);

/*
 ***********************************************************************************************
 * Support for invoking both managed and unmanaged functions.
 * argSize must be in multiples of the minimum stack alignment for the architecture.
 ***********************************************************************************************
 */

PILLAR_EXPORT PRT_CALL_FROM_UNMANAGED void PRT_STDCALL prtInvokeManagedFunc(
                                                        PRT_IN PrtCodeAddress managedFunc,
                                                        PRT_IN void *argStart,
                                                        PRT_IN unsigned argSize);

/* Do a fat cut to the specified continuation. */
PILLAR_EXPORT PRT_NORETURN PRT_CALL_FROM_PILLAR void __pdecl prtFatCutTo(PRT_IN struct PrtContinuation *cont);

/* Do a thin cut to the specified continuation.  This should not be used except in very special situations. */
PILLAR_EXPORT PRT_NORETURN PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtThinCutTo(PRT_IN struct PrtContinuation *k);


/*
 **********************************************************************************************************
 * Stack iteration functions
 **********************************************************************************************************
 */

/*
 * Stack iteration general comments:
 *   1) A stack iterator allows client code to walk a task's stack in order from the youngest to oldest.
 *      All frames returned by the prtYoungestActivationXXX() and prtNextActivation() functions are managed.
 *      Note that stack walking is not MT-safe: concurrent stack changes during the walk may result in a crash.
 */

/*
 * Create a stack iterator for the current task assuming it is in unmanaged code. "si" points to client-allocated storage.
 */
PILLAR_EXPORT PRT_CALL_FROM_UNMANAGED void PRT_CDECL prtYoungestActivationFromUnmanaged(PRT_OUT struct PrtStackIterator *si);

/*
 * Same as prtYoungestActivationFromUnmanaged, but for a particular task instead of the current task.
 */
PILLAR_EXPORT PRT_CALL_FROM_UNMANAGED void PRT_CDECL prtYoungestActivationFromUnmanagedInTask(PRT_OUT struct PrtStackIterator *si,
                                                                                            PRT_IN  PrtTaskHandle theTaskHandle);

/* Go to the activation previous to (older than) the current one. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtNextActivation(PRT_INOUT struct PrtStackIterator *si);

/* Return True iff no activations remain on the stack. */
#if 0
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtBool PRT_CDECL prtIsActivationPastEnd(PRT_IN struct PrtStackIterator *si);
#else /* !0 */
#ifdef __x86_64__
#define prtIsActivationPastEnd(si) (((si)->ripPtr == NULL)? PrtTrue : PrtFalse)
#else  // __x86_64__
#define prtIsActivationPastEnd(si) (((si)->eipPtr == NULL)? PrtTrue : PrtFalse)
#endif // __x86_64__
#endif /* !0 */

/* Return the ip for the current activation. */
#if 0
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtCodeAddress PRT_CDECL prtGetActivationIP(PRT_IN struct PrtStackIterator *si);
#else /* !0 */
#ifdef __x86_64__
#define prtGetActivationIP(si) ((PrtCodeAddress)((si)->ripPtr ? *(si)->ripPtr : 0))
#else  // __x86_64__
#define prtGetActivationIP(si) ((PrtCodeAddress)((si)->eipPtr ? *(si)->eipPtr : 0))
#endif // __x86_64__
#endif /* !0 */

/* Convenience function for updating all writable fields of a stack iterator at once.
   The advantage of using this consistently is that if the PrtStackIterator
   ever changes, this function can be changed accordingly, and all calls to
   it can be easily found and modified.
 */
#ifdef __x86_64__
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtSetStackIteratorFields(PRT_OUT struct PrtStackIterator *context,
                                                                            PRT_IN PrtCodeAddress *ripPtr,
                                                                            PRT_IN PrtRegister rsp,
                                                                            PRT_IN PrtRegister *rbxPtr,
                                                                            PRT_IN PrtRegister *rbpPtr,
                                                                            PRT_IN PrtRegister *r12Ptr,
                                                                            PRT_IN PrtRegister *r13Ptr,
                                                                            PRT_IN PrtRegister *r14Ptr,
                                                                            PRT_IN PrtRegister *r15Ptr,
                                                                            PRT_IN PrtVseHandle vsh,
                                                                            PRT_IN long virtualFrameNumber);
#else  // __x86_64__
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtSetStackIteratorFields(PRT_OUT struct PrtStackIterator *context,
                                                                            PRT_IN PrtCodeAddress *eipPtr,
                                                                            PRT_IN PrtRegister esp,
                                                                            PRT_IN PrtRegister *ebxPtr,
                                                                            PRT_IN PrtRegister *ebpPtr,
                                                                            PRT_IN PrtRegister *esiPtr,
                                                                            PRT_IN PrtRegister *ediPtr,
                                                                            PRT_IN PrtVseHandle vsh,
                                                                            PRT_IN unsigned virtualFrameNumber);
#endif // __x86_64__

/* Marks the frame as having been seen during a stack walk. */
PILLAR_EXPORT PRT_CALL_FROM_UNMANAGED void PRT_CDECL prtMarkFrameAsVisited(PRT_IN struct PrtStackIterator *context);

/* Returns true if the frame was previously marked with prtMarkFrameAsVisited. */
PILLAR_EXPORT PRT_CALL_FROM_UNMANAGED PrtBool PRT_CDECL prtHasFrameBeenVisited(PRT_IN const struct PrtStackIterator *context);

/* Return a continuation corresponding to a continuation listed in an "also unwinds to" clause. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE struct PrtContinuation * PRT_CDECL prtGetUnwindContinuation(PRT_IN struct PrtStackIterator *si,
                                                                                              PRT_IN unsigned continuationIndex);

/* Built-in span keys that facilitate transferring control back into managed or unmanaged code.
 * The managed-to-unmanaged transition frame provides a span with key PRT_SPAN_KEY_M2U.
 * prtGetSpanDescriptor returns a continuation that can be cut to in order to return to
 * the managed caller.
 * Similarly, the unmanaged-to-managed transition frame provides a span with key PRT_SPAN_KEY_U2M,
 * and prtGetSpanDescriptor returns a continuation that can be cut to in order to return to
 * the unmanaged caller.
 * In both cases, the continuation contains 64 bits of arguments used as a return value,
 * where the first argument holds the low-order 32 bits and the second argument holds
 * the high-order 32 bits.
 */
#define PRT_SPAN_KEY_M2U ((unsigned) -1)
#define PRT_SPAN_KEY_U2M ((unsigned) -2)

/*
 * Returns the value associated with the smallest span tagged with "key" and containing the program point
 * where "si" is suspended.  Returns (PrtDataPointer)NULL if there is no such span.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtDataPointer PRT_CDECL prtGetSpanDescriptor(PRT_IN struct PrtStackIterator *si,
																	        	 PRT_IN unsigned key);

/* Returns a string describing the current frame. The string space is provided by the caller.
   Always returns the "buffer" argument for convenience. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE char * PRT_CDECL prtGetActivationString(PRT_IN struct PrtStackIterator *si,
                                                                          PRT_OUT char *buffer,
                                                                          PRT_IN unsigned bufferSize);

/*******************************************************************************
 * Root set enumeration functions
 *******************************************************************************/

/*
 * Root Set Enumeration general comments -
 *
 * The roots of a Pillar task consist of the roots on that task's stack frames and
 * the roots in that task's virtual stack elements.  While the nodes of the virtual
 * stack may be created inside the physical stack frames, the roots in those virtual
 * stack elements should be enumerated by the virtual stack enumeration function and
 * not the physical stack frame enumeration function.  Conversely, the roots in the
 * physical stack frame should be enumerated only by the physical stack frame
 * enumeration callback.  In this way, the roots identified by both the physical stack
 * and virtual stack enumeration routines should not overlap but the union of them
 * should represent all the roots for a given task.
 */

/* Enumerate the roots of a frame given by the specified stack iterator. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtEnumerateRootsOfActivation(PRT_IN struct PrtStackIterator *si,
                                                                                PRT_IN struct PrtRseInfo *rootSetInfo);

/* Enumerate the roots of each of the VSE's in the specified task's virtual stack.
 * If there are any pending fat cuts, then the target continuations will be found
 * on the virtual stack and their arguments will be enumerated.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtEnumerateVseRootsOfTask(PRT_IN PrtTaskHandle theTaskHandle,
                                                                             PRT_IN struct PrtRseInfo *rootSetInfo);

/*
 * Convenience function that combines other functions to enumerate all roots in a specified task including
 * stack roots and TLS roots.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtEnumerateTaskRootSet(PRT_IN PrtTaskHandle theTaskHandle,
                                                                          PRT_IN struct PrtRseInfo *rootSetInfo);

PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtEnumerateTlsRootSet(PRT_IN PrtTaskHandle theTaskHandle,
                                                                         PRT_IN struct PrtRseInfo *rootSetInfo);

/*
 * Enumerate all non-thread local roots.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtEnumerateGlobalRootSet(PRT_IN struct PrtRseInfo *rootSetInfo);

/*
 * Register a function to enumerate global roots.  This can be called for
 * multiple enumerator functions.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtRegisterGlobalEnumerator(PRT_IN PrtGlobalEnumerator enumerator);

/*
 * Register a function to enumerate roots in the prtGetTls() area.  This can be called for
 * multiple enumerator functions.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtRegisterTlsEnumerator(PRT_IN PrtTlsEnumerator enumerator);

/*
 **********************************************************************************************************
 * VSE (Virtual Stack Entry) functions.
 **********************************************************************************************************
 */

/* Constant specifying the size of a VSE. */
enum {PILLAR_VSE_SIZE = (sizeof(struct PrtContinuation) + sizeof(PrtRegister))};


typedef void CDECL_FUNC_OUT (CDECL_FUNC_IN *PrtVseRseFunction)(PRT_IN PrtVseHandle theHandle, PRT_IN struct PrtRseInfo *rootSetInfo);


/*
 * Associates a root set enumeration function with a VSE type.  This is only needed
 * for VSEs that are created within unmanaged code, which doesn't know how to enumerate
 * the VSE roots.  Note that there's no need to register VSEs that can't hold roots.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtRegisterVseRseFunction(PRT_IN PrtVseType vseType,
                                                                            PRT_IN PrtVseRseFunction enumerator);

/* Returns the head of the virtual stack for the current task. */
PILLAR_EXPORT PRT_CALL_FROM_PILLAR PrtVseHandle PRT_CDECL prtGetVsh(void);

/* Pushes "vse" onto the top of the Pillar virtual stack. Returns True if successful, False otherwise. */
PILLAR_EXPORT PRT_CALL_FROM_PILLAR void PRT_CDECL prtPushVse(PRT_IN PrtVseType type,
                                                           PRT_INOUT PrtVseHandle vse);

/* Pops the topmost VSE off the Pillar virtual stack.  Returns a handle to the popped VSE. */
PILLAR_EXPORT PRT_CALL_FROM_PILLAR PrtVseHandle PRT_CDECL prtPopVse(void);

/* Pushes "vse" onto the top of the Pillar virtual stack. Returns True if successful, False otherwise. */
PILLAR_EXPORT PRT_CALL_FROM_PILLAR void PRT_CDECL prtPushVseForTask(PRT_IN PrtTaskHandle task,
                                                                  PRT_IN PrtVseType type,
                                                                  PRT_INOUT PrtVseHandle vse);

/* Pops the topmost VSE off the Pillar virtual stack.  Returns a handle to the popped VSE. */
PILLAR_EXPORT PRT_CALL_FROM_PILLAR PrtVseHandle PRT_CDECL prtPopVseForTask(PRT_IN PrtTaskHandle task);

/* Returns the given VSE's type. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtVseType PRT_CDECL prtGetVseType(PRT_IN PrtVseHandle vse);

/* Returns the next VSE given the specified one. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtVseHandle PRT_CDECL prtGetNextVse(PRT_IN PrtVseHandle vse);

/* Returns a task set. If you want the diffs from a previous task set then pass that task set as a parameter. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE struct PrtTaskSetEnumerator* PRT_CDECL prtGetTaskSet(struct PrtTaskSetEnumerator *tse);

/* You must call this when you're done with a task set so the tasks therein are free to terminate. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtReleaseTaskSet(struct PrtTaskSetEnumerator *tse);

/*
 * After a call to getTaskSet you call this to return the first thread from the enumerator.
 * Can be called multiple times if you want to go over the list more than once.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtTaskHandle PRT_CDECL prtStartIterator(struct PrtTaskSetEnumerator *tse);

/* Returns the next thread in the iterator.  Returns NULL if you're at the end. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtTaskHandle PRT_CDECL prtNextIterator(struct PrtTaskSetEnumerator *tse);

/* Asks the TLS offset manager to reserve from space. */
/* This may not permanently belong here but it is a fast place to put it initially. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE unsigned PRT_CDECL ptkGetNextTlsOffset(unsigned space_needed);
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE unsigned PRT_CDECL ptkGetMinTlsSpace(void);

/* Stops a pillar task as returned by one of the enumeration functions above */
/* Returns true if the task was stopped, false if the task could not be stopped because it was exiting. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtBool PRT_CDECL prtSuspendTask(PrtTaskHandle task);

/* Resumes a previously suspended task. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtResumeTask(PrtTaskHandle task);

#if 0
/*
 * If you have suspended a task, you can use this procedure to hand off the task's (locked) stack walking lock to a target task.
 * Then, when that target task tries to acquire the lock, it will get it. Also, only the target task can acquire the stack lock.
 * The call to this procedure must be followed later by a call to prtHandoffReacquireStackLock().
 * When the target task later releases the lock, this task's prtHandoffReacquireStackLock() call will return with the
 * current task again having the stack lock.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtHandoffStackLock(PrtTaskHandle taskLocked, PrtTaskHandle targetTask);

/*
 * This procedure reacquires a task's stack walking lock that you previously handed off using prtHandoffStackLock().
 * This function blocks until the reacquire has succeeded.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtHandoffReacquireStackLock(PrtTaskHandle taskLocked);

/*
 * If you have suspended a task, you can use this procedure to hand off the task's (locked) stack walking lock to a target task.
 * Then, when that target task tries to acquire the lock, it will get it. Also, only the target task can acquire the stack lock.
 * The call to this procedure must be followed later by a call to prtHandoffReacquireStackLock().
 * When the target task later releases the lock, this task's prtHandoffReacquireStackLock() call will return with the
 * current task again having the stack lock.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtHandoffStackLock(PrtTaskHandle taskLocked, PrtTaskHandle targetTask);

/*
 * This procedure reacquires a task's stack walking lock that you previously handed off using prtHandoffStackLock().
 * This function blocks until the reacquire has succeeded.
 */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtHandoffReacquireStackLock(PrtTaskHandle taskLocked);
#endif

/*
 **********************************************************************************************************
 * Functions to pause and resume VTune data collection.
 **********************************************************************************************************
 */

/*
 * Note that when these functions are used, run VTune with the "Start with data collection paused" option enabled
 * on the activity's configuration dialog box. This dialog box appears when you execute the "Modify Activity" menu
 * item for an activity. This ensures that data collection is only done when the application wants it.
 * See http://cache-www.intel.com/cd/00/00/21/93/219345_sampling_vtune.pdf.
 */

/* Pause VTune data collection for all VTune data collectors (sampling, counter monitor, and call graph). */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtVtunePause(void);

/* Resume VTune data collection for all VTune data collectors. */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtVtuneResume(void);

/*
 **********************************************************************************************************
 * Function to append events to a trace file.
 **********************************************************************************************************
 */

/* Log the fact that we have just made the "transition" (usually "ENTER" or "EXIT") from "state". */
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtLogEvent(PRT_IN void *clientData, PRT_IN char *transition, PRT_IN char *state);

/*
 **********************************************************************************************************
 * Synchronization Functions.
 **********************************************************************************************************
 */

struct prtMutex;
struct prtMutexAttr;

// These all return 0 on success.
struct prtMutex * __pcdecl prtMutexCreate(const struct prtMutexAttr *attr);
int PRT_CDECL prtMutexLock(struct prtMutex *Mutex);
int __pcdecl prtMutexTrylock(struct prtMutex *Mutex);
int __pcdecl prtMutexUnlock(struct prtMutex *Mutex);
int __pcdecl prtMutexDestroy(struct prtMutex *Mutex);

struct prtCondition;
struct prtConditionAttr;

struct prtCondition * __pcdecl prtConditionInit(struct prtConditionAttr *cond_attr);
int __pcdecl prtConditionSignal(struct prtCondition *cond);
int __pcdecl prtConditionBroadcast(struct prtCondition *cond);
int PRT_CDECL prtConditionWait(struct prtCondition *cond, struct prtMutex *mutex);
int __pcdecl prtConditionDestroy(struct prtCondition *cond);

void PRT_CDECL prtSleep(unsigned milliseconds);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _PRT_H */
