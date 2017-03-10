/*
 * Redistribution and use in source and binary forms, with or without modification, are permitted 
 * provided that the following conditions are met:
 * 1.   Redistributions of source code must retain the above copyright notice, this list of 
 * conditions and the following disclaimer.
 * 2.   Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
