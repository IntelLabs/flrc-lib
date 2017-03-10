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

/* $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/toolkit/futures/src/ptkfuture.c,v 1.12 2012/06/22 21:37:50 taanders Exp $ */

/*
 * Special defines:
 * - CONCURRENCY: Set this for any builds with multithreaded future generation/execution.
 * - __pillar__: The Pillar compiler automatically sets this.
 * - USE_PRSCALL: Set this (along with CONCURRENCY and __pillar__) to spawn via prscall.
 */

//#define DISABLE_STEALING

typedef unsigned int uint32;

#include "toolkit/ptkfuture.h"
#include "toolkit/ptkfutureinternal.h"

#ifdef __pillar__
#pragma pillar_managed(off)
#endif /* __pillar__ */

#ifdef _WIN32

#define to ___to___
#include <windows.h>
#undef to

#endif // WIN32

// Uncomment the following to turn on event logging for futures
//#define LOG_FUTURE_EVENTS

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#ifdef __pillar__
#pragma pillar_managed(on)
#include "prt/prtcodegenerator.h"
#endif /* __pillar__ */

#undef ptkFutureInit
#undef ptkFutureSetStatus
#undef ptkFutureGetStatus

#define ptkMalloc malloc
#define ptkRealloc realloc

/* Set WEAK_MASK to 0 to disable reporting references on the future queues as weak. */
#define WEAK_MASK (1 << 31)
#define QUEUES_PER_THREAD 100

unsigned PTKFUTURE_BATCHSIZE=1;
static unsigned futures_verbose=0;
static unsigned autobatch=0;
static unsigned future_threads_used=0;
unsigned AUTO_SAMPLE_SIZE = 1000;
static unsigned g_refresultbatchsize = 0;
static unsigned future_thread_stack_size = 0;

#ifdef __pillar__

void (__pdecl *gc_heap_slot_write_barrier_indirect)(void *base, void **p_slot, void *value);
void (__pdecl *gc_heap_slot_write_interior_indirect)(void **p_slot, void *value, unsigned offset);

static void doExit(int code)
{
    prtExit(code);
}

#else /* !__pillar__ */

static void doExit(int code)
{
    exit(code);
} // doExit

#endif /* !__pillar__ */

typedef int futureBool;
#define futureTrue 1
#define futureFalse 0

typedef futureBool (__cdecl *futurePredicate)(volatile void *location, void *data);

futureBool __cdecl predicateEqualUint32(volatile void *location, void *data)
{
    return (*(volatile uint32*)location) == ((uint32)data);
}


uint32 futureLockedCmpxchgUint32(volatile uint32 * loc,uint32 cmpValue, uint32 newValue)
{
#ifdef _WIN32
#ifdef INTEL64
    register uint32 value;
    __asm {
        mov rcx, loc
        mov edx, newValue
        mov eax, cmpValue
        lock cmpxchg dword ptr[rcx], edx
        mov value, eax
    }
    return value;
#else // !INTEL64
    register uint32 value;
    __asm {
        mov ecx, loc
        mov edx, newValue
        mov eax, cmpValue
        lock cmpxchg dword ptr[ecx], edx
        mov value, eax
    }
    return value;
#endif // INTEL64
#else  /* Linux gcc/icc */
    register uint32 value;
    __asm__ __volatile__(
        "lock; cmpxchgl %3,(%1)"
        : "=a"(value)
        : "r"(loc), "a"(cmpValue), "d"(newValue)
        : "memory");
    return value;
#endif
}

uint32 futureLockedAddUint32(volatile uint32 * loc, uint32 addend)
{
#ifdef _WIN32
#ifdef INTEL64
    register uint32 val;
    __asm {
        mov rcx, loc
        mov eax, addend
        lock xadd dword ptr[rcx], eax
        mov val, eax
    }
    return val;
#else // !INTEL64
    register uint32 val;
    __asm {
        mov ecx, loc
        mov eax, addend
        lock xadd dword ptr[ecx], eax
        mov val, eax
    }
    return val;
#endif // INTEL64
#else                           /* Linux gcc */
    register uint32 val;
    __asm__ __volatile__(
        "lock; xaddl %0,(%1)"
        : "=r"(val)
        : "r"(loc), "0"(addend)
        : "memory");

    return val;
#endif                          /* _WIN32 */
}

#ifdef CONCURRENCY

#ifdef __pillar__

static void ptk_YieldUntil(futurePredicate predicate,
                           void volatile *location,
                           void *data)
{
    prtYieldUntil((PrtPredicate)predicate, location, data, InfiniteWaitCycles64);
} // ptk_YieldUntil

#else /* !__pillar__ */

#error
static void ptk_YieldUntil(futurePredicate predicate,
                           void volatile *location,
                           void *data)
{
    // FIX FIX FIX
    assert(0);
} // ptk_YieldUntil
#endif /* !__pillar__ */

#define XDECLARE(NAME, SUFF, CONV) CONV NAME ## SUFF
#define DECLARE(NAME, SUFF, CONV) XDECLARE(NAME, SUFF, CONV)
#define XCONCAT(A, B) A ## B
#define CONCAT(A, B) XCONCAT(A, B)

#define CALLING_CONVENTION __pdecl
#define SUFFIX Managed
#define ARG_TYPE ref
#include "toolkit/ptkfuture_common.c"
#undef CALLING_CONVENTION
#undef SUFFIX
#undef ARG_TYPE

#ifdef __pillar__
#pragma pillar_managed(off)
#endif /* __pillar__ */

#define CALLING_CONVENTION __cdecl
#define SUFFIX Unmanaged
#define ARG_TYPE void *
#include "toolkit/ptkfuture_common.c"
#undef CALLING_CONVENTION
#undef SUFFIX
#undef ARG_TYPE

#ifdef __pillar__
#pragma pillar_managed(on)
#endif /* __pillar__ */

/* Returns the old status value. */
#define changeStatusAtomically(location, fromValue, toValue) ((PtkFutureStatus) futureLockedCmpxchgUint32((volatile uint32 *)(location), (fromValue), (toValue)))

#ifndef USE_PRSCALL

static struct PtkFuture_Queue *queues;
static unsigned numQueues;
static unsigned shouldShutDown;
static uint32 remainingThreadsToStart;

/*
    Locking rules for queues and nodes.
    The queue must be locked for all traversals.
    (Exception: RSE can scan a queue while all mutator threads are stopped.)
    A node can only be enqueued after its status is atomically changed to Spawning and the queue is locked.
    A node can only be dequeued after its status is atomically changed to Started and the queue is locked.
 */

static void lockQueue(struct PtkFuture_Queue *queue, PtkFuture_LockOperation operation)
{
    /* Make a few non-blocking attempts to get the lock.
       If it fails, do a full-blown unmanaged call into the blocking version. */
    unsigned i;
    int result;
    unsigned limit = (operation == pfloSpawn ? 3 : 1);
    noyield {
        for (i=0; i<limit; i++) {
            result = pthread_mutex_trylock(&queue->lock);
            if (result == 0) {
                return;
            } else {
                if(result != EBUSY) {
                    assert(0);
                }
            }
        }
    }
    result = pthread_mutex_lock(&queue->lock);
    assert(result == 0);
} // lockQueue

static void unlockQueue(struct PtkFuture_Queue *queue)
{
    int result = pthread_mutex_unlock(&queue->lock);
    assert(result == 0);
} // unlockQueue

static struct PtkFuture_ListNode *getListNode(struct PtkFuture_Queue *queue)
{
    if (queue->nodePoolNext == 0) {
        /* Replenish the nodes. */
        unsigned i;
        unsigned numNewNodes = 100;
        struct PtkFuture_ListNode *nodes = (struct PtkFuture_ListNode *) ptkMalloc(numNewNodes * sizeof(*nodes));
        resizeNodeArrayManaged(queue, numNewNodes);
        for (i=0; i<numNewNodes; i++) {
            queue->nodePool[i] = &nodes[i];
        }
        queue->nodePoolNext = numNewNodes;
    }
    return queue->nodePool[--queue->nodePoolNext];
} // getListNode

#ifdef __pillar__
static unsigned g_tls_offset;
//#define NEXT_QUEUE (((unsigned *)prtGetTls())[g_tls_offset]++)
#define NEXT_QUEUE (((struct PtkFuture_Tls *)((unsigned *)prtGetTls() + g_tls_offset))->nextQueue++)
#else /* !__pillar__ */
#define NEXT_QUEUE (mcrtThreadGetId())
#endif /* !__pillar__ */

static void addFuture(ref arg, int futureOffset, unsigned record_time)
{
#ifdef _WINDOWS
	ULARGE_INTEGER start_spawn, done_spawn;
#elif defined LINUX
    struct timeval start_spawn, done_spawn;
#else
#error
#endif

    unsigned nextQueue = NEXT_QUEUE % numQueues;
    struct PtkFuture_Queue *myQueue = &queues[nextQueue];
    struct PtkFuture_ListNode *node;
    assert(GetFuture(arg, futureOffset)->status == PtkFutureStatusSpawning);

    myQueue->spawn_count++;

#if 1
	if(nextQueue == 0 && autobatch) {
		if(myQueue->auto_mode == pfamIncreasing) {
			if(myQueue->auto_sample == 0) {
				printf("Starting sample for batch size %d\n",PTKFUTURE_BATCHSIZE);
#ifdef _WINDOWS
				QueryPerformanceCounter((LARGE_INTEGER*)&myQueue->auto_sample_start);
#elif defined LINUX
				gettimeofday(&myQueue->auto_sample_start,NULL);
#endif
			}
			myQueue->auto_sample++;
		} else if(myQueue->auto_mode == pfamStable) {
			if(myQueue->auto_sample == 0) {
//				printf("Starting sample for batch size %d\n",PTKFUTURE_BATCHSIZE);
#ifdef _WINDOWS
				QueryPerformanceCounter((LARGE_INTEGER*)&myQueue->auto_sample_start);
#elif defined LINUX
				gettimeofday(&myQueue->auto_sample_start,NULL);
#endif
			}
			myQueue->auto_sample++;
		}
	}
#else
	if(nextQueue == 0 && record_time && autobatch) {
#ifdef _WINDOWS
		QueryPerformanceCounter(&start_spawn);
#elif defined LINUX
		gettimeofday(&start_spawn,NULL);
#endif
	}
#endif

	lockQueue(myQueue, pfloSpawn);
    //printf("Spawning 0x%p to queue %u\n", arg, nextQueue);
    node = getListNode(myQueue);
    node->next = &myQueue->dummy;
    node->prev = myQueue->dummy.prev;
#ifdef __pillar__
    // We need to set this to NULL before making the call so that in
    // concurrent mode the GC won't think the random value in that
    // slot is a meaningful object to be marked.
    node->future = NULL;
    gc_heap_slot_write_interior_indirect((void**)&(node->future),GetFuture(arg, futureOffset),futureOffset);
#else // !__pillar__
    node->future = GetFuture(arg, futureOffset);
#endif // !__pillar__
    node->futureGcOffset = futureOffset;
    node->list = myQueue;
    myQueue->dummy.prev->next = node;
    myQueue->dummy.prev = node;
    GetFuture(arg, futureOffset)->qnode = node;

#if 1
	if(nextQueue == 0 && autobatch) {
		if(myQueue->auto_mode == pfamIncreasing) {
			if(myQueue->auto_sample == AUTO_SAMPLE_SIZE) {
#ifdef _WINDOWS
				QueryPerformanceCounter((LARGE_INTEGER*)&done_spawn);
				done_spawn.QuadPart = done_spawn.QuadPart - myQueue->auto_sample_start.QuadPart;
				if(done_spawn.QuadPart > myQueue->auto_last_sample.QuadPart || ((PTKFUTURE_BATCHSIZE+10)>MAX_FUTURE_BATCHSIZE)) {
					printf("Sample ended for batchsize %d.  Old sample is better, %I64u vs %I64u.\n",PTKFUTURE_BATCHSIZE,myQueue->auto_last_sample.QuadPart,done_spawn.QuadPart);
					myQueue->auto_mode = pfamStable;
					ptkFutureSystemBatchSize(myQueue->auto_prev_sample_size);
				} else {
					printf("Sample ended for batchsize %d.  New sample is better, %I64u vs %I64u.\n",PTKFUTURE_BATCHSIZE,myQueue->auto_last_sample.QuadPart,done_spawn.QuadPart);
					myQueue->auto_prev_prev_sample_size = myQueue->auto_prev_sample_size;
					myQueue->auto_prev_sample_size = PTKFUTURE_BATCHSIZE;
					ptkFutureSystemBatchSize(PTKFUTURE_BATCHSIZE+10);
					myQueue->auto_last_sample.QuadPart = done_spawn.QuadPart;
				}

#elif defined LINUX
				gettimeofday(&done_spawn,NULL);
                // FIX FIX FIX
#endif

				myQueue->auto_sample = 0;
			}
		} else if(myQueue->auto_mode == pfamStable) {
			if(myQueue->auto_sample == AUTO_SAMPLE_SIZE) {
#ifdef _WINDOWS
				QueryPerformanceCounter((LARGE_INTEGER*)&done_spawn);
				done_spawn.QuadPart = done_spawn.QuadPart - myQueue->auto_sample_start.QuadPart;
				myQueue->auto_sample = 0;
				printf("Sample ended for batchsize %d. %I64u.\n",PTKFUTURE_BATCHSIZE,done_spawn.QuadPart);
#elif defined LINUX
				gettimeofday(&done_spawn,NULL);
                // FIX FIX FIX
#endif
			}
		}
	}
#else
	if(nextQueue == 0 && record_time && autobatch) {
#ifdef _WINDOWS
		QueryPerformanceCounter(&done_spawn);
		start_spawn.QuadPart = done_spawn.QuadPart - start_spawn.QuadPart;
		if(myQueue->spawn_wma == 0.0) {
			myQueue->spawn_wma = start_spawn.QuadPart;
		} else {
			myQueue->spawn_wma = (myQueue->spawn_wma * myQueue->spawn_weight) + ((1-myQueue->spawn_weight) * start_spawn.QuadPart);
            myQueue->spawn_weight += (0.98 - myQueue->spawn_weight) / 10.0;
		}
#elif defined LINUX
		gettimeofday(&done_spawn,NULL);
        // FIX FIX FIX
#endif
	}
#endif

    unlockQueue(myQueue);
} // addFuture

static void removeFuture(ref arg, int futureOffset)
{
    struct PtkFuture_ListNode *qnode = GetFuture(arg, futureOffset)->qnode;
    struct PtkFuture_Queue *queue = qnode->list;
    lockQueue(queue, pfloWait);
    removeFutureWithNodeQueueManaged(arg, futureOffset, qnode, queue);
    unlockQueue(queue);
} // removeFuture

void emptyBatchedRefReturns(struct PtkFuture_Tls *myTls);

/* Searches the given queue for an available future to execute.
   Returns true if it found and executed one. */
static unsigned findExecuteFuture(struct PtkFuture_Queue *queue, unsigned queueIndex)
{
//	LARGE_INTEGER exe_start, exe_done;
    unsigned result = 0;
    struct PtkFuture_ListNode *node, *next;
    ref arg = 0;
    int futureOffset = -1;
    if (queue->dummy.next == &queue->dummy) {
        return 0;
    }
    lockQueue(queue, plfoSteal);
    for (node=queue->dummy.next; !result && node != &queue->dummy; node=next) {
        next = node->next;
        futureOffset = node->futureGcOffset;
        arg = (ref) ((char *)node->future - futureOffset);
        /* A null argument would indicate a weak root being set to null during a previous GC. */
        if (arg) {
            if (node->future->status == PtkFutureStatusSpawned &&
                changeStatusAtomically(&node->future->status, PtkFutureStatusSpawned, PtkFutureStatusStarted) == PtkFutureStatusSpawned) {
                removeFutureWithNodeQueueManaged(arg, futureOffset, node, queue);
                result = 1;
            }
#ifdef DONT_REMOVE_AFTER_WAIT
			if (node->future->status != PtkFutureStatusSpawning &&
				node->future->status != PtkFutureStatusSpawned) {
	            removeFutureWithNodeQueueManaged(arg, futureOffset, node, queue);
			}
#endif // DONT_REMOVE_AFTER_WAIT
        } else {
            removeFutureWithNodeQueueManaged(arg, futureOffset, node, queue);
        }
    }
    unlockQueue(queue);
    if (result) {
		PtkFutureStatus newStatus;
#if 0
		if(queueIndex == 0 && autobatch) {
			QueryPerformanceCounter(&exe_start);
		}
#endif // 0

#ifdef LOG_FUTURE_EVENTS
        prtLogEvent(arg, "ENTER", "EVAL_SPAWNED");
#endif // LOG_FUTURE_EVENTS
        newStatus = GetFuture(arg, futureOffset)->code(arg);
#if 0
		if(queueIndex == 0 && autobatch) {
			QueryPerformanceCounter(&exe_done);
			exe_start.QuadPart = exe_done.QuadPart - exe_start.QuadPart;
        	queue->exe_count++;
			if(queue->exe_wma == 0.0) {
				queue->exe_wma = exe_start.QuadPart;
			} else {
//          	  if(queueIndex==0) printf("exe_wma %p, %f, %I64u, ",queue,queues[0].exe_wma,exe_start.QuadPart);
				queue->exe_wma = (queue->exe_wma * queue->exe_weight) + ((1-queue->exe_weight) * exe_start.QuadPart);
    	        queue->exe_weight += (0.98 - queue->exe_weight) / 100.0;
//      	      if(queueIndex==0) printf("%f, %f\n",queues[0].exe_wma, queue->exe_weight);
			}

        	if(queues[0].spawn_count>10 && queues[0].exe_count>10) {
            	ptkFutureSystemBatchSize(1 + (queues[0].spawn_wma * future_threads_used / queues[0].exe_wma));
				if(futures_verbose) {
 					printf("Auto-setting batch size to %d, spawn=%f,exe=%f.\n",PTKFUTURE_BATCHSIZE,queues[0].spawn_wma,queues[0].exe_wma);
				}
            }
        }
#endif // 0

#ifdef LOG_FUTURE_EVENTS
        prtLogEvent(arg, "EXIT", "EVAL_SPAWNED");
#endif // LOG_FUTURE_EVENTS
        GetFuture(arg, futureOffset)->status = newStatus;
        assert(newStatus >= PtkFutureStatusUserDefined);
        //printf("Future queue %u ran future.\n", queueIndex);
    }
    return result;
}

static futureBool __cdecl isWorkAvailable(volatile void *location, void *data);

static void futureLoop(unsigned queueIndex)
{
    struct PtkFuture_Tls *myTls = (struct PtkFuture_Tls *)((unsigned *)prtGetTls() + g_tls_offset);

    futureLockedAddUint32(&remainingThreadsToStart, -1);
    while (!shouldShutDown) {
        unsigned found = 0;
        unsigned i;
        for (i=0; !shouldShutDown && !found && i<numQueues; i++) {
            do {
				unsigned qIndex = (i + QUEUES_PER_THREAD*queueIndex) % numQueues;
                found = findExecuteFuture(&queues[qIndex], qIndex);
				// I think this old line with queueIndex is wrong...findExecuteFuture previously didn't use this parameter
				// at all.  The new version for autobatch does and it needs to computed index.
//                found = findExecuteFuture(&queues[(i + QUEUES_PER_THREAD*queueIndex) % numQueues], queueIndex);
                if(myTls->trigger_publish) {
                    myTls->trigger_publish = 0;
                    emptyBatchedRefReturns(myTls);
                }
            } while (found && !shouldShutDown);
        }
        if (!shouldShutDown && !found) {
            emptyBatchedRefReturns(myTls);
            ptk_YieldUntil(isWorkAvailable, 0, (void*)queueIndex);
        }
    }
#if 0
	if(futures_verbose && autobatch) {
		if(queueIndex == 0) {
//			unsigned i;
//			for(i=0;i<numQueues; i++) {
				printf("Spawn_wma = %f, Exe_wma = %f\n",queues[0].spawn_wma,queues[0].exe_wma);
                fflush(stdout);
//			}
		}
	}
#endif // 0
}

static futureBool __cdecl isDoneOrWorkAvailable(volatile void *location, void *data)
{
	ref arg = (ref)data;
	int futureOffset = (int)location;

	if(GetFuture(arg,futureOffset)->status != PtkFutureStatusStarted) return futureTrue;
	if(isWorkAvailable(0,0)) return futureTrue;
	return futureFalse;
} // isDoneOrWorkAvailable

#ifdef WORK_DONT_WAIT
static void doWorkAllQueues(ref arg, int futureOffset)
{
	while(GetFuture(arg,futureOffset)->status == PtkFutureStatusStarted) {
		unsigned found=0;
		unsigned i;

	    for (i=0; GetFuture(arg,futureOffset)->status == PtkFutureStatusStarted && !found && i<numQueues; i++) {
			do {
				found = findExecuteFuture(&queues[i % numQueues], 0);
			} while (found && GetFuture(arg,futureOffset)->status == PtkFutureStatusStarted);
		}
		if(GetFuture(arg,futureOffset)->status == PtkFutureStatusStarted && !found) {
			ptk_YieldUntil(isDoneOrWorkAvailable, (void*)futureOffset, arg);
		}
	}
}
#endif // WORK_DONT_WAIT

#ifdef __pillar__

static void __cdecl futureRseFunction(struct PrtRseInfo *rootSetInfo)
{
    unsigned i;
    for (i=0; i<numQueues; i++) {
//        pthread_mutex_lock(&queues[i].lock);

        unsigned nodeCount = 0;
        unsigned reclaimedCount = 0;
        struct PtkFuture_Queue *queue = &queues[i];
        struct PtkFuture_ListNode *node, *next;
        /* Do a test lock/unlock to see if the lock is held by someone.
           If so, don't try to clean out nulled futures. */
        int lockStatus = getLockStatusUnmanaged(queue);
        for (node=queue->dummy.next; node != &queue->dummy; node=next) {
            next = node->next;
            if (node->future == (struct PtkFuture_Internal *)node->futureGcOffset) {
                /* The future has been collected and zeroed out. */
                if (lockStatus == 0) {
                    reclaimedCount ++;
                    removeFutureWithNodeQueueUnmanaged(0, node->futureGcOffset, node, queue);
                }
            } else {
                nodeCount ++;
                rootSetInfo->callback(rootSetInfo->env, (void **)&node->future, (PrtGcTag)(PrtGcTagOffset | WEAK_MASK), (void *)node->futureGcOffset);
            }
        }
        /*printf("Future queue %u: nodeCount=%u, reclaimedCount=%u\n", i, nodeCount, reclaimedCount);*/

//        pthread_mutex_unlock(&queues[i].lock);
    }
} // futureRseFunction

static void __cdecl tlsRseFunction(void *tls, struct PrtRseInfo *rootSetInfo)
{
    struct PtkFuture_Tls *myTls = (struct PtkFuture_Tls *)((unsigned *)tls + g_tls_offset);
    unsigned i;
    //printf("tlsRseFunction\n");
    for (i=0; i<myTls->numBatchedFutures; i++) {
        //printf("Enumerating 0x%p\n", myTls->batchedFutures[i].future);
        rootSetInfo->callback(rootSetInfo->env,
            (void **)&myTls->batchedFutures[i].future,
            (PrtGcTag)(PrtGcTagDefault /*| WEAK_MASK*/),
            0);
    }
    for (i=0; i<myTls->numBatchRefReturns; i++) {
        rootSetInfo->callback(rootSetInfo->env,
            (void **)&myTls->batchedRefReturns[i].object,
            (PrtGcTag)(PrtGcTagDefault),
            0);
        rootSetInfo->callback(rootSetInfo->env,
            (void **)&myTls->batchedRefReturns[i].value,
            (PrtGcTag)(PrtGcTagDefault),
            0);
        rootSetInfo->callback(rootSetInfo->env,
            (void **)&myTls->batchedRefReturns[i].result,
            (PrtGcTag)(PrtGcTagBase),
            myTls->batchedRefReturns[i].object);
    }
} // tlsRseFunction

#endif /* __pillar__ */
#endif /* !USE_PRSCALL */

#ifdef __pillar__

static PrtBool __cdecl notEqual(volatile void *location, void *data)
{
    return (*(void **)location != data) ? PrtTrue : PrtFalse;
} // notEqual

static void ptk_YieldUntilStatusChange(ref arg, int futureOffset, PtkFutureStatus status)
{
    prtYieldUntilMovable(notEqual, &GetFuture(arg, futureOffset)->status, (void *)status,
        InfiniteWaitCycles64, PrtGcTagOffset, (void *)futureOffset);
} // ptk_YieldUntilStatusChange

#else /* !__pillar__ */

static void ptk_YieldUntilStatusChange(ref arg, int futureOffset, PtkFutureStatus status)
{
    mcrtThreadYieldUntil(mcrtPredicateNotEqualUint32, &GetFuture(arg, futureOffset)->status,
        (void *)status, InfiniteWaitCycles64);
} // ptk_YieldUntilStatusChange

#endif /* __pillar__ */

#ifdef USE_PRSCALL

static void spawnViaPrscall(ref arg, int futureOffset)
{
    PtkFutureStatus newStatus;

#ifdef ONE_PROC_STEALING
	prt_IdleThreadWaitFunctionForMcrt(NULL);
    mcrtThreadYield();
#endif // ONE_PROC_STEALING

    GetFuture(arg, futureOffset)->status = PtkFutureStatusStarted;
    newStatus = GetFuture(arg, futureOffset)->code(arg);
    GetFuture(arg, futureOffset)->status = newStatus;
    assert(newStatus >= PtkFutureStatusUserDefined);
} // spawnViaPrscall

#endif /* USE_PRSCALL */

//static McrtTimeCycles64 spawnTimestamps[1600*1200*2*10];
static unsigned curSpawn = 0;
void ptkFutureSpawn(ref arg, int futureOffset)
{
	unsigned first = 1;

#if defined __pillar__ && !defined USE_PRSCALL
    struct PtkFuture_Tls *myTls = (struct PtkFuture_Tls *)((unsigned *)prtGetTls() + g_tls_offset);
//    spawnTimestamps[curSpawn++] = mcrtGetTimeStampCounter();
    //printf("Spawn 0x%p (batching), myTls=0x%p\n", arg, myTls);
	if(!myTls->batchedFutures) {
		myTls->size_batch_array = PTKFUTURE_BATCHSIZE > MAX_FUTURE_BATCHSIZE ? PTKFUTURE_BATCHSIZE : MAX_FUTURE_BATCHSIZE;
		myTls->batchedFutures = (struct PtkFuture_BatchEntry *)ptkMalloc(sizeof(struct PtkFuture_BatchEntry) * myTls->size_batch_array);
	}
    myTls->batchedFutures[myTls->numBatchedFutures].future = arg;
    myTls->batchedFutures[myTls->numBatchedFutures].futureGcOffset = futureOffset;
    myTls->numBatchedFutures ++;
    if (myTls->numBatchedFutures >= PTKFUTURE_BATCHSIZE || myTls->numBatchedFutures >= myTls->size_batch_array) {
        while (myTls->numBatchedFutures) {
            arg = myTls->batchedFutures[myTls->numBatchedFutures - 1].future;
            futureOffset = myTls->batchedFutures[myTls->numBatchedFutures - 1].futureGcOffset;
            myTls->numBatchedFutures --;
            if (arg && PtkFutureStatusInit == changeStatusAtomically(&GetFuture(arg, futureOffset)->status, PtkFutureStatusInit, PtkFutureStatusSpawning)) {
                addFuture(arg, futureOffset, first);
				first = 0;
                GetFuture(arg, futureOffset)->status = PtkFutureStatusSpawned;
#ifdef LOG_FUTURE_EVENTS
                prtLogEvent(arg, "ENTER", "SPAWNED");
#endif // LOG_FUTURE_EVENTS
            } else {
                if (arg) {
                    //printf("Not spawning, status was %u\n", GetFuture(arg, futureOffset)->status);
                } else {
                    //printf("Not spawning, arg==NULL\n");
                }
            }
        }
    }
//    spawnTimestamps[curSpawn++] = mcrtGetTimeStampCounter();
    return;
#else /* __pillar && !defined USE_PRSCALL */
    if (PtkFutureStatusInit != changeStatusAtomically(&GetFuture(arg, futureOffset)->status, PtkFutureStatusInit, PtkFutureStatusSpawning)) {
        return;
    }
#ifdef USE_PRSCALL
    /* Currently the Pillar compiler unwinder doesn't work properly around a prscall,
       so we invoke prtPrscall directly. */
    /*prscall spawnViaPrscall(arg, futureOffset);*/
    {
        void *args[2];
        args[0] = (void *) arg;
        args[1] = (void *) futureOffset;
        prtPrscall((PrtCodeAddress)spawnViaPrscall, &args[0], 2, 0);
    }
#else /* !USE_PRSCALL */
    addFuture(arg, futureOffset, first);
    GetFuture(arg, futureOffset)->status = PtkFutureStatusSpawned;
#ifdef LOG_FUTURE_EVENTS
    prtLogEvent(arg, "ENTER", "SPAWNED");
#endif // LOG_FUTURE_EVENTS
#endif /* !USE_PRSCALL */
#endif /* __pillar && !defined USE_PRSCALL */
} // ptkFutureSpawn

void publishLastRef(void) {
#if defined __pillar__
    struct PtkFuture_Tls *myTls = (struct PtkFuture_Tls *)((unsigned *)prtGetTls() + g_tls_offset);
	assert(myTls->batchedRefReturns);
    assert(myTls->numBatchRefReturns);

    myTls->numBatchRefReturns--;

    gc_heap_slot_write_barrier_indirect((void*)myTls->batchedRefReturns[myTls->numBatchRefReturns].object,
                                        (void **)myTls->batchedRefReturns[myTls->numBatchRefReturns].result,
                                        (void*)myTls->batchedRefReturns[myTls->numBatchRefReturns].value);

    GetFuture(myTls->batchedRefReturns[myTls->numBatchRefReturns].object, myTls->batchedRefReturns[myTls->numBatchRefReturns].futureOffset)->status = myTls->batchedRefReturns[myTls->numBatchRefReturns].new_status;
#else // __pillar__
    assert(0);
#endif // __pillar__
}

#define DONT_PUBLISH_IF_OPTIONAL

unsigned ptkTriggerPublish(ref arg, int futureOffset) {
    struct PtkFuture_Internal *the_future = GetFuture(arg,futureOffset);
    struct PtkFuture_Tls *myTls = (struct PtkFuture_Tls *)((unsigned *)prtGetTls() + g_tls_offset);
    if(the_future->code == myTls) {
        emptyBatchedRefReturns(myTls);
        return 1;
    } else {
        myTls->trigger_publish = 1;
        return 0;
    }
}

unsigned ptkFutureWait(ref arg, int futureOffset, unsigned shouldWait)
{
    PtkFutureCodePointer code = GetFuture(arg, futureOffset)->code;
    while (1) {
        PtkFutureStatus status = GetFuture(arg, futureOffset)->status;
        PtkFutureStatus newStatus;
        switch (status) {
        case PtkFutureStatusInit:
        case PtkFutureStatusSpawned:
            if (status != changeStatusAtomically(&GetFuture(arg, futureOffset)->status, status, PtkFutureStatusStarted))
                continue; /* with the while(1) loop */
#ifndef USE_PRSCALL
            if (status == PtkFutureStatusSpawned) {
#ifndef DONT_REMOVE_AFTER_WAIT
                removeFuture(arg, futureOffset);
#endif // DONT_REMOVE_AFTER_WAIT
            }
#endif /* !USE_PRSCALL */
#ifdef LOG_FUTURE_EVENTS
            prtLogEvent(arg, "ENTER", "EVAL_SPAWNED");
#endif // LOG_FUTURE_EVENTS
            newStatus = code(arg);
#ifdef LOG_FUTURE_EVENTS
            prtLogEvent(arg, "EXIT", "EVAL_SPAWNED");
#endif // LOG_FUTURE_EVENTS
            if(shouldWait && newStatus == PtkFutureStatusRefRet) {
#if 0
                if(status == PtkFutureStatusSpawned) {
                    printf("Ref return was spawned.\n");
                }
#endif
                publishLastRef();
            } else {
                GetFuture(arg, futureOffset)->status = newStatus;
                assert(newStatus >= PtkFutureStatusUserDefined);
            }
            if(!shouldWait && newStatus == PtkFutureStatusRefRet) {
                return 0;
            } else {
                return 1;
            }
            break;
        case PtkFutureStatusStarted:
            if (!shouldWait) {
                return 0;
            }
#ifdef LOG_FUTURE_EVENTS
            prtLogEvent(arg, "ENTER", "WAIT_EVAL_DONE");
#endif // LOG_FUTURE_EVENTS
#ifdef WORK_DONT_WAIT
			doWorkAllQueues(arg, futureOffset);
#endif // WORK_DONT_WAIT
            ptk_YieldUntilStatusChange(arg, futureOffset, status);
#ifdef LOG_FUTURE_EVENTS
			prtLogEvent(arg, "EXIT", "WAIT_EVAL_DONE");
#endif // LOG_FUTURE_EVENTS
            break; /* continue with the while(1) loop */
        case PtkFutureStatusSpawning:
            ptk_YieldUntilStatusChange(arg, futureOffset, status);
            break; /* continue with the while(1) loop */
        case PtkFutureStatusRefRet:
#ifdef DONT_PUBLISH_IF_OPTIONAL
            if (!shouldWait) {
                return 0;
            } else {
#endif // DONT_PUBLISH_IF_OPTIONAL
                if(!ptkTriggerPublish(arg, futureOffset)) {
                    ptk_YieldUntilStatusChange(arg, futureOffset, status);
                }
#ifdef DONT_PUBLISH_IF_OPTIONAL
            }
#endif // DONT_PUBLISH_IF_OPTIONAL
            break;
        case PtkFutureStatusUninit:
            printf("ptkFutureWait: illegal status: PtkFutureStatusUninit\n");
            doExit(1);
            break;
        default:
            /* Nothing to do, future is already executed or cancelled. */
            return 0;
        }
    }
    assert(0); /* Shouldn't get here. */
    return 0;
} // ptkFutureWait

void emptyBatchedRefReturns(struct PtkFuture_Tls *myTls) {
    while (myTls->numBatchRefReturns) {
        myTls->numBatchRefReturns--;

        gc_heap_slot_write_barrier_indirect((void*)myTls->batchedRefReturns[myTls->numBatchRefReturns].object,
                                            (void **)myTls->batchedRefReturns[myTls->numBatchRefReturns].result,
                                            (void*)myTls->batchedRefReturns[myTls->numBatchRefReturns].value);

        GetFuture(myTls->batchedRefReturns[myTls->numBatchRefReturns].object, myTls->batchedRefReturns[myTls->numBatchRefReturns].futureOffset)->status = myTls->batchedRefReturns[myTls->numBatchRefReturns].new_status;
    }
}

PtkFutureStatus ptkReturnRef(ref arg, int futureOffset, void **result, ref value, PtkFutureStatus new_status) {
    if(!g_refresultbatchsize) {
        gc_heap_slot_write_barrier_indirect((void*)arg,(void **)result,(void*)value);
        return new_status;
    } else {
        PtkFutureStatus current_status = GetFuture(arg, futureOffset)->status;

#if defined __pillar__
        struct PtkFuture_Internal *the_future = GetFuture(arg,futureOffset);

        struct PtkFuture_Tls *myTls = (struct PtkFuture_Tls *)((unsigned *)prtGetTls() + g_tls_offset);
	    if(!myTls->batchedRefReturns) {
            myTls->size_batch_ref_array = 10;
		    myTls->batchedRefReturns = (struct SaveReturnRef *)ptkMalloc(sizeof(struct SaveReturnRef) * myTls->size_batch_ref_array);
	    }
        myTls->batchedRefReturns[myTls->numBatchRefReturns].object = arg;
        myTls->batchedRefReturns[myTls->numBatchRefReturns].futureOffset = futureOffset;
        myTls->batchedRefReturns[myTls->numBatchRefReturns].result = result;
        myTls->batchedRefReturns[myTls->numBatchRefReturns].value = value;
        myTls->batchedRefReturns[myTls->numBatchRefReturns].new_status = new_status;
        myTls->numBatchRefReturns++;
        if (myTls->numBatchRefReturns >= myTls->size_batch_ref_array) {
            emptyBatchedRefReturns(myTls);
            return new_status;
        }

        the_future->code = (PtkFutureCodePointer)myTls;  // make it easy to get to the future thread that batched this result
        return PtkFutureStatusRefRet;
#else // __pillar__
        gc_heap_slot_write_barrier_indirect((void*)arg,(void **)result,(void*)value);
        return new_status;
#endif // __pillar__
    }
}

#else /* !CONCURRENCY */

unsigned ptkFutureWait(ref arg, int futureOffset, unsigned shouldWait /* ignored for sequential */)
{
    PtkFutureStatus status = GetFuture(arg, futureOffset)->status;
    PtkFutureStatus newStatus;
    switch (status) {
    case PtkFutureStatusUninit:
        printf("ptkFutureWait: illegal status: PtkFutureStatusUninit\n");
        doExit(1);
        break;
    case PtkFutureStatusInit:
        GetFuture(arg, futureOffset)->status = PtkFutureStatusStarted;
        newStatus = GetFuture(arg, futureOffset)->code(arg);
        GetFuture(arg, futureOffset)->status = newStatus;
        assert(newStatus >= PtkFutureStatusUserDefined);
        return 1;
        break;
    case PtkFutureStatusStarted:
        printf("ptkFutureWait: illegal status: PtkFutureStatusStarted\n");
        doExit(1);
        break;
    default:
        /* Nothing to do, future is already executed or cancelled. */
        break;
    }
    return 0;
} // ptkFutureWait

PtkFutureStatus ptkReturnRef(ref arg, int futureOffset, void **result, ref value, PtkFutureStatus new_status) {
    gc_heap_slot_write_barrier_indirect((void*)arg,(void **)result,(void*)value);
    return new_status;
}

#endif /* !CONCURRENCY */

void ptkFutureSystemBatchSize(unsigned batchsize)
{
#ifdef CONCURRENCY
#if 0
	PTKFUTURE_BATCHSIZE = batchsize;
#else
    if(batchsize < MAX_FUTURE_BATCHSIZE) {
		PTKFUTURE_BATCHSIZE = batchsize;
	} else {
		PTKFUTURE_BATCHSIZE = MAX_FUTURE_BATCHSIZE;
	}
#endif
#endif // CONCURRENCY
} // ptkFutureSystemBatchSize

int optionNumThreads;
int optionNumThreadsSpecified = 0;

#define BATCH_OPTION "batch="
#define REFRESULTBATCH_OPTION "refresultbatch="
#define STACKSIZE_OPTION "stacksize="
#define SAMPLESIZE_OPTION "samplesize="
#define NUMTHREADS_OPTION "numThreads="

void ptkFutureSystemSetOption(const char *optionString)
{
    if(strncmp(optionString,BATCH_OPTION,strlen(BATCH_OPTION)) == 0) {
		const char *value_ptr = optionString + strlen(BATCH_OPTION);
		unsigned batchsize = atoi(value_ptr);
		ptkFutureSystemBatchSize(batchsize);
    } else if(strncmp(optionString,REFRESULTBATCH_OPTION,strlen(REFRESULTBATCH_OPTION)) == 0) {
		const char *value_ptr = optionString + strlen(REFRESULTBATCH_OPTION);
		g_refresultbatchsize = atoi(value_ptr);
	} else if(strcmp(optionString,"verbose") == 0) {
		futures_verbose = 1;
	} else if(strcmp(optionString,"autobatch") == 0) {
        autobatch = 1;
    } else if(strcmp(optionString,"noautobatch") == 0) {
        autobatch = 0;
    } else if(strncmp(optionString,SAMPLESIZE_OPTION,strlen(SAMPLESIZE_OPTION)) == 0) {
		const char *value_ptr = optionString +   strlen(SAMPLESIZE_OPTION);
		AUTO_SAMPLE_SIZE = atoi(value_ptr);
    } else if(strncmp(optionString,STACKSIZE_OPTION,strlen(STACKSIZE_OPTION)) == 0) {
		const char *value_ptr = optionString +   strlen(STACKSIZE_OPTION);
		future_thread_stack_size = atoi(value_ptr);
	} else if(strncmp(optionString,NUMTHREADS_OPTION,strlen(NUMTHREADS_OPTION)) == 0) {
		const char *value_ptr = optionString +   strlen(NUMTHREADS_OPTION);
        optionNumThreadsSpecified = 1;
		optionNumThreads = atoi(value_ptr);
	}
} // ptkFutureSystemSetOption

void ptkFutureSystemStart(int numThreads)
{
#ifdef CONCURRENCY
#ifndef USE_PRSCALL
    unsigned i;
    int j;
    numQueues = 0;
    if(optionNumThreadsSpecified) {
        if(optionNumThreads >= 0) {
            numThreads = optionNumThreads;
        } else {
            numThreads = prtGetNumProcessors() + numThreads;
        }
    } else {
        if (numThreads == 0) {
            numThreads = prtGetNumProcessors();
        } else if (numThreads < 0) {
            numThreads = prtGetNumProcessors() + numThreads;
        }
    }

#ifdef __pillar__
    g_tls_offset = ptkGetNextTlsOffset(sizeof(struct PtkFuture_Tls)) / sizeof(unsigned);
    prtRegisterGlobalEnumerator(futureRseFunction);
    prtRegisterTlsEnumerator(tlsRseFunction);
#endif /* __pillar__ */
#if 1
    if(numThreads < 1) {
        numQueues  = 1;
        numThreads = 0;
    } else {
        numQueues = QUEUES_PER_THREAD*numThreads;
    }
#else /* !0 */
    if (numQueues == 0) {
        numQueues = QUEUES_PER_THREAD * numThreads;
    }
#endif /* !0 */
    remainingThreadsToStart = numThreads;
    queues = ptkMalloc(numQueues * sizeof(*queues));
    for (i=0; i<numQueues; i++) {
        pthread_mutex_init(&queues[i].lock,NULL);
        queues[i].nodePool = NULL;
        queues[i].nodePoolSize = 0;
        queues[i].nodePoolNext = 0;
//		queues[i].spawn_wma = 0.0;
        queues[i].spawn_count = 0;
//        queues[i].spawn_weight = 0.5;
//		queues[i].exe_wma = 0.0;
//        queues[i].exe_count = 0;
//        queues[i].exe_weight = 0.5;
		queues[i].auto_mode = pfamIncreasing;
		queues[i].auto_sample = 0;
		queues[i].auto_prev_sample_size = -9;
		queues[i].auto_prev_prev_sample_size = -19;
#ifdef _WINDOWS
		queues[i].auto_last_sample.QuadPart = 0xFFffFFffFFffFFff;
#elif defined LINUX
        // FIX FIX FIX
#endif
        queues[i].dummy.next = queues[i].dummy.prev = &queues[i].dummy;
    }
#if 0
	if(PTKFUTURE_BATCHSIZE > 1) {
		queues[0].auto_mode = pfamStable;
		autobatch = 1;
	}
#endif
    future_threads_used = numThreads;
    for (j=0; j<numThreads; j++) {
#ifdef __pillar__
        unsigned cur_ss;
        if(future_thread_stack_size) {
            cur_ss = prtGetPcallStackSize();
            prtSetPcallStackSize(future_thread_stack_size);
        }
        pcall futureLoop(j);
        if(future_thread_stack_size) {
            prtSetPcallStackSize(cur_ss);
        }
#else /* !__pillar__ */
        int pthread_create_err;
        pthread_attr_t create_attr;
        pthread_attr_init(&create_attr);
        if(future_thread_stack_size) {
            pthread_attr_setstacksize(&create_attr,future_thread_stack_size);
        }

        pthread_t new_thread;
        pthread_create_err = pthread_create(&new_thread, &create_attr ,futureLoop, (void *)j);
        pthread_attr_destroy(&create_attr);
        if(pthread_create_err) {
            printf("pthread_create failed to create a futureLoop thread...exiting\n");
            doExit(-1);
        }
#endif /* !__pillar__ */
    }
    ptk_YieldUntil((futurePredicate)predicateEqualUint32, &remainingThreadsToStart, 0);
#endif /* !USE_PRSCALL */
#endif /* CONCURRENCY */
} // ptkFutureSystemStart

void ptkFutureSystemShutdown(void)
{
#ifdef CONCURRENCY
#ifndef USE_PRSCALL
    shouldShutDown = 1;
#endif /* !USE_PRSCALL */
#endif /* CONCURRENCY */
} // ptkFutureSystemShutdown

void ptkFutureInit(ref arg, int futureOffset, PtkFutureCodePointer managedFunc)
{
#ifdef CONCURRENCY
    GetFuture(arg, futureOffset)->qnode  = NULL;
#endif /* CONCURRENCY */
    GetFuture(arg, futureOffset)->code   = managedFunc;
    GetFuture(arg, futureOffset)->status = PtkFutureStatusInit;
} // ptkFutureInit

void ptkFutureSetStatus(ref arg, int futureOffset, PtkFutureStatus newStatus)
{
    assert(newStatus >= PtkFutureStatusUserDefined);
    GetFuture(arg, futureOffset)->status = newStatus;
} // ptkFutureSetStatus

PtkFutureStatus ptkFutureGetStatus(ref arg, int futureOffset)
{
    return GetFuture(arg, futureOffset)->status;
} // ptkFutureGetStatus

PtkFutureStatus ptkFutureCancel(ref arg, int futureOffset)
{
    PtkFutureStatus result = (PtkFutureStatus)-1;
    unsigned done = 0;
    while (!done) {
        PtkFutureStatus status = GetFuture(arg, futureOffset)->status;
        result = status;
        switch (status) {
        case PtkFutureStatusUninit:
        case PtkFutureStatusInit:
#ifdef CONCURRENCY
            if (status == changeStatusAtomically(&GetFuture(arg, futureOffset)->status, status, PtkFutureStatusCancelled)) {
                done = 1;
            }
#else /* !CONCURRENCY */
            GetFuture(arg, futureOffset)->status = PtkFutureStatusCancelled;
            done = 1;
#endif /* !CONCURRENCY */
            break;
#ifdef CONCURRENCY
        case PtkFutureStatusSpawning:
            /* wait until spawned before deleting */
            ptk_YieldUntilStatusChange(arg, futureOffset, status);
                break;
#ifndef USE_PRSCALL
        case PtkFutureStatusSpawned:
            if (status == changeStatusAtomically(&GetFuture(arg, futureOffset)->status, status, PtkFutureStatusCancelled)) {
                assert(GetFuture(arg, futureOffset)->qnode);
#ifndef DONT_REMOVE_AFTER_WAIT
                removeFuture(arg, futureOffset);
#endif // DONT_REMOVE_AFTER_WAIT
                done = 1;
            }
            break;
#endif /* !USE_PRSCALL */
#endif /* CONCURRENCY */
        case PtkFutureStatusStarted:
            /* nothing can be done to delete once it has started */
        case PtkFutureStatusCancelled:
        default:
            /* some user-specified state means the future is completed and
               nothing can be done to cancel it */
            done = 1;
            break;
        }
    }

    return result;
} // ptkFutureCancel

#ifdef CONCURRENCY

#ifdef __pillar__
#pragma pillar_managed(off)
#endif /* __pillar__ */

static futureBool __cdecl isWorkAvailable(volatile void *location, void *data)
{
    unsigned i;
    unsigned queueIndex = (unsigned) data;
    unsigned queueStart, queueEnd;
    int sync_res;
    struct PtkFuture_ListNode *node;
    int (*try_lock)(pthread_mutex_t *);
    int (*release)(pthread_mutex_t *);

#ifndef USE_PRSCALL
    if (shouldShutDown) {
        return futureTrue;
    }

    try_lock = (int (*)(pthread_mutex_t *))&pthread_mutex_trylock;
    release  = (int (*)(pthread_mutex_t *))&pthread_mutex_unlock;

#ifdef DISABLE_STEALING
    queueStart = queueIndex;
    queueEnd = queueIndex + 1;
#else // !DISABLE_STEALING
    queueStart = 0;
    queueEnd = numQueues;
#endif // !DISABLE_STEALING

    for (i=queueStart; i<queueEnd; i++) {
        if (queues[i].dummy.next != &queues[i].dummy) {
            sync_res = try_lock(&queues[i].lock);
            if(sync_res == 0) {
                for (node  = queues[i].dummy.next;
		             node != &queues[i].dummy;
	                 node = node->next) {
		           if(node->future->status == PtkFutureStatusSpawned) {
			           release(&queues[i].lock);
		               return futureTrue;
				   }
				}

		        release(&queues[i].lock);
			}
        }
    }
#endif // !USE_PRSCALL
    return futureFalse;
} // isWorkAvailable

#endif // CONCURRENCY
