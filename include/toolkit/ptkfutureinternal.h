/*
 * COPYRIGHT_NOTICE_1
 */

/* $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/toolkit/futures/src/ptkfutureinternal.h,v 1.5 2011/03/09 19:09:53 taanders Exp $ */

#ifndef _PTKFUTUREINTERNAL_H
#define _PTKFUTUREINTERNAL_H

#ifndef __pillar__
#define __pcdecl __cdecl
#define __pdecl __cdecl
#endif /* !__pillar__ */

#ifdef __pillar__
#pragma pillar_managed(off)
#endif /* __pillar__ */
#include <pthread.h>
#ifdef LINUX
#include <errno.h>
#endif
#ifdef __pillar__
#pragma pillar_managed(on)
#endif /* __pillar__ */

#ifdef CONCURRENCY

#ifdef __GNUC__
typedef unsigned long long __uint64;
#else /* !__GNUC__ */
typedef unsigned __int64 __uint64;
#endif /* !__GNUC__ */

#define InfiniteWaitCycles64  0xFFFFFFFFFFFFFFFF

#if 0
/* Copied/adapted from McRT. */
#define InfiniteWaitCycles64  0xFFFFFFFFFFFFFFFF

typedef enum McrtSyncResultE {
    Success = 0,
    IllegalState,
    Timeout,
    Interrupt,          // thread may be cancelled, or signal received
    Deadlock,
    Unavailable,        // for Try routines, lock is unavailable
    RecursiveRelease,
    Yielded,            // for yield routines, there was no other work to do
    LastThread          // for barriers, this thread was the last thread to arrive
} McrtSyncResult;

typedef int McrtBool;
struct McrtUncheckedLockS {
    volatile McrtBool loc;      /* McrtTrue if locked, McrtFalse if not */
};
typedef struct McrtUncheckedLockS McrtUncheckedLock;

extern McrtUncheckedLock * __cdecl mcrtUncheckedLockNew(void);
extern void __cdecl mcrtUncheckedLockDelete(McrtUncheckedLock *);
extern void __cdecl mcrtUncheckedLockInit(McrtUncheckedLock *);
extern void __cdecl mcrtUncheckedLockDeinit(McrtUncheckedLock *);

extern McrtSyncResult __pcdecl mcrtUncheckedLockTryAcquire(McrtUncheckedLock *);
extern McrtSyncResult __cdecl mcrtUncheckedLockAcquire(McrtUncheckedLock *);
extern McrtSyncResult __pcdecl mcrtUncheckedLockRelease(McrtUncheckedLock *);

int __pcdecl mcrtThreadGetId();
uint32 __pcdecl mcrtLockedCmpxchgUint32(volatile uint32 * loc, uint32 cmpValue, uint32 newValue);
extern uint32 __pcdecl mcrtLockedXAddUint32(volatile uint32 * loc, uint32 addend);
uint32 __pcdecl mcrtGetNumProcessors(void);

typedef __uint64 McrtTimeCycles64;
#define McrtFalse 0
#define McrtTrue 1
typedef McrtBool (__cdecl *McrtPredicate)(volatile void *location, void *data);
McrtBool __cdecl mcrtPredicateNotEqualUint32(volatile void *location, void *data);
McrtBool __cdecl mcrtPredicateEqualUint32(volatile void *location, void *data);
McrtSyncResult __cdecl mcrtThreadYieldUntil(McrtPredicate predicate,
                                            void volatile *location,
                                            void *data,
                                            McrtTimeCycles64 endTime);
typedef void (__cdecl * McrtThreadFunctionPtr1)(void *);
/*McrtThread * */ void __cdecl mcrtThreadCreate(McrtThreadFunctionPtr1 fn, void *fnargs);
McrtTimeCycles64 __pcdecl mcrtGetTimeStampCounter(void);
#endif

struct PtkFuture_ListNode {
    struct PtkFuture_ListNode *next, *prev;
    struct PtkFuture_Queue    *list;
    struct PtkFuture_Internal *future;
    int                        futureGcOffset;
};

typedef enum PtkFuture_LockOperation {
    pfloSpawn = 0,
    pfloWait = 1,
    plfoSteal = 2,
    plfoNumOperations = 3
} PtkFuture_LockOperation;

typedef enum PtkFuture_AutoMode {
	pfamIncreasing = 0,
	pfamStable = 1
} PtkFuture_AutoMode;

#ifdef _WINDOWS
#ifdef __pillar__
#pragma pillar_managed(off)
#endif /* __pillar__ */
#define to ___to___
#include <windows.h>
#undef to
#ifdef __pillar__
#pragma pillar_managed(on)
#endif /* __pillar__ */
#endif // _WINDOWS
#ifdef LINUX
#ifdef __pillar__
#pragma pillar_managed(off)
#endif /* __pillar__ */
#include <sys/time.h>
#ifdef __pillar__
#pragma pillar_managed(on)
#endif /* __pillar__ */
#endif // LINUX

struct PtkFuture_Queue {
    pthread_mutex_t lock;
    struct PtkFuture_ListNode dummy;
    /* The nodePool is an array of pointers to available list nodes that can be
       returned when getListNode() is called.
       nodePoolSize is the total length of the array.
       nodePool[0..nodePoolNext-1] are valid pointers to available nodes.
     */
    struct PtkFuture_ListNode **nodePool;
    unsigned nodePoolSize;
    unsigned nodePoolNext;
    long spawn_count;
#if 0
    double spawn_wma;
    double spawn_weight;
    double exe_wma;
    long exe_count;
    double exe_weight;
#endif // 0
    PtkFuture_AutoMode auto_mode;
	unsigned auto_sample;
	unsigned auto_prev_sample_size;
	unsigned auto_prev_prev_sample_size;
#ifdef _WINDOWS
	ULARGE_INTEGER auto_sample_start;
	ULARGE_INTEGER auto_last_sample;
#elif defined LINUX
	struct timeval auto_sample_start;
	struct timeval auto_last_sample;
#else
#error
#endif

    char padding[256];
};

#define MAX_FUTURE_BATCHSIZE 256

struct PtkFuture_BatchEntry {
    ref future;
    int futureGcOffset;
};

struct SaveReturnRef {
    ref object;
    int futureOffset;
    void **result;
    ref value;
    PtkFutureStatus new_status;
};

struct PtkFuture_Tls {
    unsigned nextQueue; /* where to deal out the next spawned future */
    unsigned numBatchedFutures;
	unsigned size_batch_array;
//    struct PtkFuture_BatchEntry batchedFutures[MAX_FUTURE_BATCHSIZE];
    struct   PtkFuture_BatchEntry *batchedFutures;

    unsigned numBatchRefReturns;
    unsigned size_batch_ref_array;
    struct   SaveReturnRef *batchedRefReturns;
    unsigned trigger_publish;
};

#endif /* CONCURRENCY */

/* This is a macro rather than a function because the Pillar compiler
   doesn't support interior ref pointers as function arguments. */
#define GetFuture(arg, offset) ((struct PtkFuture_Internal *) ((char *)arg + offset))

#endif /* !_PTKFUTUREINTERNAL_H */
