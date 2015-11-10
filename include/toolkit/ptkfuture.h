/*
 * COPYRIGHT_NOTICE_1
 */

/* $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/toolkit/futures/src/ptkfuture.h,v 1.3 2011/03/09 19:09:53 taanders Exp $ */

#ifndef _PTKFUTURE_H
#define _PTKFUTURE_H

#ifndef __pillar__
typedef char *ref;
#define noyield
#endif /* !__pillar__ */

#ifdef CONCURRENCY
#define PTK_FUTURE_SIZE (3 * sizeof(void *))
#define ptkFutureStatic(Status, Code) { (void *)Status, (void *)Code, 0 }
#else /* !CONCURRENCY */
#define PTK_FUTURE_SIZE (2 * sizeof(void *))
#define ptkFutureStatic(Status, Code) { (void *)Status, (void *)Code }
#endif /* !CONCURRENCY */

#define PTK_FUTURE_POST_EXECUTION_SIZE  (sizeof(void *))

typedef void * PtkFutureBuffer[PTK_FUTURE_SIZE / sizeof(void *)];

typedef void * PtkFutureBufferPost[PTK_FUTURE_POST_EXECUTION_SIZE / sizeof(void *)];
#define ptkFutureStaticPost(Status)   { (void *)Status }

typedef enum PtkFutureStatus {
    PtkFutureStatusUninit      = 0,
    PtkFutureStatusInit        = 1,
#ifdef CONCURRENCY
    PtkFutureStatusSpawning    = 2,
    PtkFutureStatusSpawned     = 3,
#endif /* CONCURRENCY */
    PtkFutureStatusStarted     = 4,
    PtkFutureStatusCancelled   = 5,
    PtkFutureStatusRefRet      = 6,
    PtkFutureStatusUserDefined = 7
} PtkFutureStatus;

typedef PtkFutureStatus (*PtkFutureCodePointer) (ref);

/* This struct is defined here only to facilitate the macro definitions of
 * ptkFutureInit, ptkFutureSetStatus, and ptkFutureGetStatus.
 * External code should not use it.
 */
struct PtkFuture_Internal {
    PtkFutureStatus             status;
    PtkFutureCodePointer        code;
#ifdef CONCURRENCY
    struct PtkFuture_ListNode  *qnode;
#endif /* CONCURRENCY */
};

void ptkFutureSystemStart(int numThreads);
void ptkFutureSystemShutdown(void);
void ptkFutureSystemBatchSize(unsigned batchsize);
void ptkFutureSystemSetOption(const char *optionString);

#ifdef PTKFUTURE_DISABLE_MACROS
void ptkFutureInit(ref arg, int futureOffset, PtkFutureCodePointer managedFunc);
void ptkFutureSetStatus(ref arg, int futureOffset, PtkFutureStatus newStatus);
PtkFutureStatus ptkFutureGetStatus(ref arg, int futureOffset);
#else /* !PTKFUTURE_DISABLE_MACROS */
#define PTKFUTURE_ACCESS_FIELD(arg, futureOffset, fieldName) \
    (((struct PtkFuture_Internal *)((char *)(arg) + (int)(futureOffset)))->fieldName)
#define ptkFutureSetStatus(arg, futureOffset, newStatus) (PTKFUTURE_ACCESS_FIELD((arg), (futureOffset), status) = (newStatus))
#define ptkFutureGetStatus(arg, futureOffset)            (PTKFUTURE_ACCESS_FIELD((arg), (futureOffset), status))
#ifdef CONCURRENCY
#define ptkFutureInit(arg, futureOffset, managedFunc) \
    (PTKFUTURE_ACCESS_FIELD((arg), (futureOffset), status) = PtkFutureStatusInit, \
     PTKFUTURE_ACCESS_FIELD((arg), (futureOffset), code)   = managedFunc, \
     PTKFUTURE_ACCESS_FIELD((arg), (futureOffset), qnode)  = 0)
#else /* !CONCURRENCY */
#define ptkFutureInit(arg, futureOffset, managedFunc) \
    (PTKFUTURE_ACCESS_FIELD((arg), (futureOffset), status) = PtkFutureStatusInit, \
     PTKFUTURE_ACCESS_FIELD((arg), (futureOffset), code)   = managedFunc)
#endif /* !CONCURRENCY */
#endif /* !PTKFUTURE_DISABLE_MACROS */

unsigned ptkFutureWait(ref arg, int futureOffset, unsigned shouldWait);
PtkFutureStatus ptkFutureCancel(ref arg, int futureOffset);
#ifdef CONCURRENCY
void ptkFutureSpawn(ref arg, int futureOffset);
#endif /* CONCURRENCY */

PtkFutureStatus ptkReturnRef(ref object, int futureOffset, void **result, ref value, PtkFutureStatus new_status);

#endif /* !_PTKFUTURE_H */
