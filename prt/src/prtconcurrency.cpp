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

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/base/prtconcurrency.cpp,v 1.29 2013/09/19 17:24:30 taanders Exp $

/*
 * Pillar runtime task and synchronization support.
 */

#include "prt/prtcodegenerator.h"

#include "prt/prtvse.h"
#include "prt/prtcims.h"
#include "prt/prttls.h"
#include "prt/prtcodeinfo.h"
#include "prt/prtglobals.h"
#include "prt/prtmisc.h"
#include "prt/prtconcurrency.h"
#include "prt/prttypes.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif // _Win32
#ifdef __GNUC__
#include <errno.h>
#endif // GCC
#ifdef LINUX
#include <unistd.h>
#endif

unsigned cimCreated;

// #define BTL_DEBUGGING 1

// This is the unwinder for the key part of the prt_BootstrapTask routine.  It is essentially
// a code info manager for prtPcall().
static void bootstrapTaskGetPreviousFrame(PrtStackIterator *si, PrtCimSpecificDataType opaqueData)
{
#ifdef __x86_64__
    si->ripPtr = NULL;
#else  // __x86_64__
    si->eipPtr = NULL;
#endif // __x86_64__
} //bootstrapTaskGetPreviousFrame


static char *bootstrapTaskGetStringForFrame(PrtStackIterator *si, char *buffer, size_t bufferSize, PrtCimSpecificDataType opaqueData)
{
    _snprintf(buffer, bufferSize, "prt_BootstrapTask frame");
    return buffer;
} //bootstrapTaskGetStringForFrame

uint32 prtLockedCmpxchgUint32(volatile uint32 * loc,uint32 cmpValue, uint32 newValue)
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

// Note: "pastEnd" points just past the end of the last managed instruction, so it must
// be adjusted before registering the code region.
extern "C" void registerBootstrapTaskCim(PrtCodeAddress start, PrtCodeAddress pastEnd)
{
    if(prtLockedCmpxchgUint32(&cimCreated, 0, 1) == 0) {
        static struct PrtCodeInfoManagerFunctions bootstrapTaskFuncs = {
            bootstrapTaskGetStringForFrame, bootstrapTaskGetPreviousFrame
        };
        PrtCodeInfoManager bootstrapTaskCim = prtRegisterCodeInfoManager("Task bootstrap CIM", bootstrapTaskFuncs);
        prtAddCodeRegion(bootstrapTaskCim, start, pastEnd-1, NULL);
    }
} //registerBootstrapTaskCim

void prt_exitTask(Prt_Task *pt);


extern "C" int prt_needsToYield(Prt_Task *pt);

extern "C" void prt_bootstrapTaskAsm(unsigned cimCreated, void *funcToCall, void *argStart, unsigned argSize);


void prt_BootstrapTask(Prt_WrappedTaskData *wrappedData, bool deleteWrappedData)
{

    // Compute where the initial VSE will be placed on this stack.
    Prt_PcallVse stackAllocationPcallVse;
    Prt_PcallVse *pcallVse = &stackAllocationPcallVse;

#ifdef __x86_64__
    // Continuations must be 16-byte aligned.
    pcallVse = (Prt_PcallVse*)((int64_t)pcallVse & 0xFFffFFffFFffFFf0);
#endif // __x86_64__

    pthread_t *new_pthread_thread = new pthread_t;
    *new_pthread_thread = pthread_self();
    // Allocate enough storage for the Prt_Task object and the client's TLS space.
    Prt_Task *pt = new (wrappedData->getNewTaskMemory()) Prt_Task(
                                new_pthread_thread,
                                wrappedData->getTlsHandle()
								);
//    printf("prt_BootstrapTask task = %x\n",pt);
    // Add the first entry to the virtual stack.
    pt->pushVse(PRT_VSE_PCALL,(PrtVseHandle)pcallVse);

    // Store a pointer to the Prt_Task object in the new thread's TLS slot.
#ifdef _WIN32
    _asm {
        mov eax, pt
        mov fs:[0x14], eax
    }
#elif defined _WIN64
    _asm {
        mov rax, pt
        mov fs:[0x28], rax
    }
#else
    pthread_setspecific(prt_GetGlobals()->tls_key, pt);
#endif

    {
        prt_PthreadLockWrapper thisLock(&(prt_GetGlobals()->gTaskSetLock));

        // If we are doing testing where we want Pillar to keep track of all Pillar tasks
        // then add it to the task set here.
        prt_GetGlobals()->gTaskMap.insert(std::pair<pthread_t,Prt_Task*>(pthread_self(),pt));
    }

	{
        prt_PthreadLockWrapper thisLock(&(prt_GetGlobals()->gPcallLock));
    	prt_GetGlobals()->gPcallMap.erase(pthread_self());
	}

    // Get the encapsulated arguments.
    void *taskData             = wrappedData->getTaskData();
    unsigned dataSizeRegisters = wrappedData->getDataSize();
    //unsigned dataSize          = (dataSizeRegisters * sizeof(PrtRegister));
    void *funcToCall           = wrappedData->getStartAddress();

    // Copy arguments onto the new stack.

    if (deleteWrappedData) {
        delete wrappedData;
//        free(taskData);
    }

    // prt_bootstrapTaskAsm never returns
    prt_bootstrapTaskAsm(cimCreated, funcToCall, taskData, dataSizeRegisters);
} //prt_BootstrapTask


static void  * startNewThreadForPcall(void *threadData)
{
#ifdef _WIN32
    __try {
#endif // _WIN32
        prt_BootstrapTask((Prt_WrappedTaskData *) threadData, /*deleteWrappedData*/ true);
#ifdef _WIN32
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("Uncaught exception in startNewThreadForPcall.\n");
        exit(-1);
    }
#endif // _WIN32
    return NULL;
} //startNewThreadForPcall


/*
 * prtPcall
 *    Immediately launches a new Pillar task running initialProc.
 */
extern "C" void pcallOnSystemStack(PrtCodeAddress initialProc,
								   void *argStart,
								   unsigned argSize,
								   PrtAffinityProcessorId pid,
 								   PrtPcallArgEnumerator argRefEnumerator) {
    PrtProvidedTlsHandle userTls = NULL;
    Prt_Task *this_task = prt_GetTask();
    if(this_task) {
        userTls = this_task->getUserTls();
    }
    unsigned argSizeBytes = argSize * sizeof(PrtRegister);
    // Allocate space to copy the arguments.
    void *taskData = malloc(argSizeBytes);
    void *newTask  = malloc(sizeof(Prt_Task));
    memset(newTask,0,sizeof(Prt_Task));
    if(this_task) {
        this_task->mLastCreated = (Prt_Task*)newTask;
    }
    // Copy the arguments.
    memcpy(taskData, argStart, argSizeBytes);
    // Get the current (creating) task's client TLS and use this for the new task, at least initially.
    Prt_WrappedTaskData *wrappedData = new Prt_WrappedTaskData(initialProc, taskData, argSize, userTls, newTask, argRefEnumerator);

    pthread_t new_thread;
    int pthread_create_err;
    pthread_attr_t create_attr;
    pthread_attr_init(&create_attr);
    if(this_task && this_task->mPcallStackSize) {
        pthread_attr_setstacksize(&create_attr,this_task->mPcallStackSize);
    }

    pthread_create_err = pthread_create(&new_thread, &create_attr, startNewThreadForPcall, wrappedData);
    if(!pthread_create_err) {
        pthread_attr_destroy(&create_attr);
        prt_PthreadLockWrapper thisLock(&(prt_GetGlobals()->gPcallLock));
        prt_GetGlobals()->gPcallMap.insert(std::pair<pthread_t,Prt_WrappedTaskData*>(new_thread,wrappedData));
        return;
    }

    printf("pthread_create failed in pcallOnSystemStack, exiting.\n");
    errno = pthread_create_err;
    perror("pthread_create error was: ");
    exit(-119);
} //pcallOnSystemStack

extern "C" void prt_pcallAsm(PrtRegister *newEsp);


PILLAR_EXPORT void __pdecl prtPcall(PrtCodeAddress managedFunc,
								    void *argStart,
									unsigned argSize,
									PrtAffinityProcessorId pid,
									PrtPcallArgEnumerator argRefEnumerator) {
    pcallOnSystemStack(managedFunc,argStart,argSize,pid,argRefEnumerator);
} //prtPcall


// Returns the task handle for the current task.
//#if defined _WIN32 && defined INLINE_GET_TASK_HANDLE
#if defined _WIN32
extern PRT_CALL_FROM_PILLAR PrtTaskHandle __pcdecl prtGetTaskHandle(void);
#else
PILLAR_EXPORT PrtTaskHandle __pcdecl prtGetTaskHandle(void)
{
    return (PrtTaskHandle)prt_GetTask();
} // prtGetTaskHandle
#endif

PILLAR_EXPORT PRT_CALL_FROM_PILLAR void __pcdecl prtSetPcallStackSize(unsigned size) {
    Prt_Task *pt = prt_GetTask();
    pt->mPcallStackSize = size;
}

PILLAR_EXPORT PRT_CALL_FROM_PILLAR unsigned __pcdecl prtGetPcallStackSize(void) {
    Prt_Task *pt = prt_GetTask();
    return pt->mPcallStackSize;
}

PILLAR_EXPORT PRT_CALL_FROM_PILLAR PrtTaskHandle PRT_CDECL prtGetLastPcallTaskHandle(void) {
    Prt_Task *pt = prt_GetTask();
    if(!pt->mLastCreated->task_ready()) {
        pt->mLastCreated->yieldUntilReady();
    }
    return (PrtTaskHandle)pt->mLastCreated;
}

PILLAR_EXPORT void __pcdecl prtRegisterTaskExistenceCallback(PrtTaskExistenceCallback cb)
{
    prt_GetGlobals()->task_callback = cb;
} // prtRegisterTaskCreationCallback


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Task termination ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void prt_exitTask(Prt_Task *pt)
{
    assert(pt);

    {
        prt_PthreadLockWrapper thisLock(&(prt_GetGlobals()->gTaskSetLock));
        // If we are keeping track of the set of Pillar tasks at the Pillar level
        // then remove this task from the set of tasks since it is ending.
        prt_GetGlobals()->gTaskMap.erase(pthread_self());
    }

    {
        prt_PthreadLockWrapper targetLock(&(pt->mYieldStateLock));
        pt->mIsEnded = true;
        while(pt->mRefcount) {
            pthread_cond_wait(&(pt->mRefcountDecrease), &(pt->mYieldStateLock));
        }

        // If anybody tries to stop us after we've started the process of stopping
        // then signal them to wake up now that mIsEnded is true.
        while(pt->mSuspenders.size()) {
            TaskSuspendInfo tsi = pt->mSuspenders.front();
            pt->mSuspenders.pop_front();

            if(tsi.m_thread_waiting) {
                pthread_cond_signal(tsi.the_condition_variable);
            } else {
                delete tsi.the_condition_variable;
            }
        }
    }

    // Delete the memory for the Pillar task.
#if 1
    pt->~Prt_Task();
    free(pt);
#else
    delete pt;
#endif
} // prt_exitTask


// prt_ExitThread: Performs all the work to shut down the current Pillar task.
extern "C" void prt_ExitThread(void)
{
    // Get the task structure for this task.
    Prt_Task *pt = prt_GetTask();
    prt_exitTask(pt);
    pthread_exit(0);
} // prt_ExitThread

typedef struct PrtConstantListNodeS {
//    tbb::tbb_thread *thread;
    Prt_Task *prt_task;
    struct PrtConstantListNodeS *next;
} PrtConstantListNode;


typedef struct PrtTaskSetS {
    int ts_refcount;
    PrtConstantListNode  head; // head.thread not used, only head.next is used...this simplifies some sequences
    PrtConstantListNode *tail;
    struct PrtTaskSetS  *next; // next task set in the list of all task sets
    int last_enumerated_index;
} PrtTaskSet;


void PrtTaskSetCtor(PrtTaskSet *task_set)
{
    task_set->ts_refcount = 1;
    task_set->head.next   = NULL;
    task_set->next        = NULL;
    task_set->tail        = &(task_set->head);
    task_set->last_enumerated_index = -1;
}


void PrtTaskSetIncRefCount(PrtTaskSet *task_set)
{
#ifdef BTL_DEBUGGING
    if (task_set == NULL) {
        printf("PrtTaskSetIncRefCount: NULL task set, exiting.\n");
        exit(-9);
    }
#endif /* BTL_DEBUGGING */
    ++(task_set->ts_refcount);
} //PrtTaskSetIncRefCount

PrtTaskSet *gTaskSetList = NULL;

void PrtTaskSetDecRefCount(PrtTaskSet *task_set)
{
    PrtTaskSet **prev_ptr;
    PrtConstantListNode *node;
    int cur_count = 0;

#ifdef BTL_DEBUGGING
    if (task_set == NULL) {
        printf("PrtTaskSetDecRefCount: NULL task set, exiting.\n");
        exit(-9);
    }
#endif /* BTL_DEBUGGING */

    if (--(task_set->ts_refcount) == 0) {
        prt_PthreadLockWrapper theLock(&(prt_GetGlobals()->gTaskSetLock));

        prev_ptr = &gTaskSetList;
        while ((*prev_ptr != NULL) && (*prev_ptr != task_set)) {
            prev_ptr = &((*prev_ptr)->next);
        }
        if (prev_ptr != NULL) {
            *prev_ptr = ((*prev_ptr)->next);
        } else {
#ifdef BTL_DEBUGGING
            printf("PrtTaskSetDecRefCount: NULL prev_ptr, exiting.\n");
#endif /* BTL_DEBUGGING */
            assert(0);
            exit(-8);
        }
        // delete nodes in the list...no need to protect since this is the last ref to the list
        while (task_set->head.next != NULL) {
#ifdef BTL_DEBUGGING
            if (task_set->head.next->thread == NULL) {
                printf("PrtTaskSetDecRefCount: NULL task_set->head.next->thread, exiting.\n");
                exit(-9);
            }
#endif /* BTL_DEBUGGING */

            {
                prt_PthreadLockWrapper targetLock(&(task_set->head.next->prt_task->mYieldStateLock));
                --task_set->head.next->prt_task->mRefcount;
                if(task_set->head.next->prt_task->mRefcount == 0) {
                    pthread_cond_broadcast(&(task_set->head.next->prt_task->mRefcountDecrease));
                }
            }

            node = task_set->head.next;
            task_set->head.next = node->next;
            free(node);
            ++cur_count;
        }
        free(task_set);
    }
} //PrtTaskSetDecRefCount


// This procedure assumes "ThreadListLock" is already held.
void addThreadToTaskSet(PrtTaskSet *task_set, Prt_Task *task)
{
    PrtConstantListNode *new_node = (PrtConstantListNode*)malloc(sizeof(PrtConstantListNode));

    if (new_node == NULL) {
        printf("PRT: addThreadToTaskSet: couldn't allocate new ConstantListNode, exiting.\n");
        exit(-9);
    }

    // BTL 20081231 Don't add dead threads to the task set.
    // Atomically increment the node's thread iteration ref count but ensure that the thread's isEnded flag is false.
    {
        prt_PthreadLockWrapper targetLock(&(task->mYieldStateLock));
        if(task->mIsEnded) {
            return;
        }
        ++task->mRefcount;
    }

    new_node->next   = NULL;
    new_node->prt_task = task;

#ifdef BTL_DEBUGGING
    if (task_set == NULL) {
        printf("Prt: addThreadToTaskSet: NULL task set, exiting.\n");
        exit(-9);
    }
    if (task == NULL) {
        printf("Prt: addThreadToTaskSet: NULL task, exiting.\n");
        exit(-9);
    }
#endif /* BTL_DEBUGGING */
    assert(task_set->tail);
#ifdef BTL_DEBUGGING
    if (task_set->tail == NULL) {
        printf("Prt: addThreadToTaskSet: NULL task_set->tail, exiting.\n");
        exit(-9);
    }
#endif /* BTL_DEBUGGING */
    task_set->tail->next = new_node;
    task_set->tail       = new_node;
} //addThreadToTaskSet


// This procedure also assumes "ThreadListLock" is already held.
void addThreadToAllTaskSets(Prt_Task *task)
{
    PrtTaskSet *task_set = gTaskSetList;
    while (task_set != NULL) {
        addThreadToTaskSet(task_set, task);
        task_set = task_set->next;
    }
} //addThreadToAllTaskSets


void getThreadList(PrtTaskSet *task_set)
{
//    printf("getThreadList start %x\n", task_set);
#ifdef BTL_DEBUGGING
    if (task_set == NULL) {
        printf("Prt: getThreadList: NULL task set, exiting.\n");
        exit(-9);
    }
#endif /* BTL_DEBUGGING */

    prt_PthreadLockWrapper thisLock(&(prt_GetGlobals()->gTaskSetLock));
    // Make "task_set" the head of the list of task sets
    task_set->next = gTaskSetList;
    gTaskSetList = task_set;
    // Now add all the currently "living" threads to the task set.
    // This also increments the ref counts for each thread added.

    std::map<pthread_t,Prt_Task *>::iterator task_iter;
    for(task_iter  = prt_GetGlobals()->gTaskMap.begin();
        task_iter != prt_GetGlobals()->gTaskMap.end();
      ++task_iter) {
        addThreadToTaskSet(task_set, (*task_iter).second);
    }
//    printf("getThreadList end %x\n", task_set);
} //getThreadList


unsigned getTaskSetSize(PrtTaskSet *task_set)
{
    unsigned rr = 0;
    PrtConstantListNode *node;

#ifdef BTL_DEBUGGING
    if (task_set == NULL) {
        printf("Prt: getTaskSetSize: NULL task set, exiting.\n");
        exit(-9);
    }
#endif /* BTL_DEBUGGING */

//    printf("getTaskSetSize %x: ", task_set);
    node = task_set->head.next;
    while (node != NULL) {
//        printf("%x ", node->thread);
        ++rr;
        node = node->next;
    }
//    printf("\n");
    return rr;
} //getTaskSetSize

typedef struct {
    PrtTaskSet *task_set;
    unsigned begin;
    unsigned end;

    PrtConstantListNode *cur_iter;
    unsigned cur_iter_index;
} PrtTaskSetEnumeratorInternal;


void PrtTaskSetEnumeratorCtor(PrtTaskSetEnumeratorInternal *tse, PrtTaskSet *task_set, unsigned begin, unsigned end)
{
#ifdef BTL_DEBUGGING
    if (tse == NULL) {
        printf("PrtTaskSetEnumeratorCtor: NULL task set enumerator, exiting.\n");
        exit(-9);
    }
    if (tse->task_set == NULL) {
        printf("PrtTaskSetEnumeratorCtor: NULL task set, exiting.\n");
        exit(-9);
    }
#endif /* BTL_DEBUGGING */

    tse->task_set = task_set;
    tse->begin    = begin;
    tse->end      = end;
    tse->cur_iter_index = 0;
    tse->cur_iter       = tse->task_set->head.next;
    tse->task_set->last_enumerated_index = end;
} //PrtTaskSetEnumeratorCtor




PrtTaskSetEnumeratorInternal *getTaskSet(PrtTaskSetEnumeratorInternal *tse)
{
    PrtTaskSetEnumeratorInternal *new_tse = (PrtTaskSetEnumeratorInternal*)malloc(sizeof(PrtTaskSetEnumeratorInternal));
//    printf("getTaskSet %x\n", tse);
    if (tse == NULL) {
        PrtTaskSet *ts = (PrtTaskSet*)malloc(sizeof(PrtTaskSet));
        if (ts == NULL) {
            printf("PRT: getTaskSet: can't allocate new task set, exiting.\n");
            exit(-9);
        }
        PrtTaskSetCtor(ts);
        // Fills in ts with the current list of pthreads and increments the ref count for each of those threads.
        getThreadList(ts);
        PrtTaskSetEnumeratorCtor(new_tse, ts, /*begin*/ 0, /*end*/ (getTaskSetSize(ts) - 1));
    } else {
#ifdef BTL_DEBUGGING
        if (tse->task_set == NULL) {
            printf("Prt: getTaskSet: tse=%p, tse->task_set=NULL, exiting.\n", tse);
            exit(-9);
        }
#endif /* BTL_DEBUGGING */
        PrtTaskSetIncRefCount(tse->task_set);
        // The new task set enumerator points to the same TaskSetList as the argument tse but begin is now one past the previous end:
        // i.e., threads started after the call that generated the incoming tse.
        PrtTaskSetEnumeratorCtor(new_tse, tse->task_set, /*begin*/ (tse->end + 1), /*end*/ (getTaskSetSize(tse->task_set) - 1));
    }
    return new_tse;
} //getTaskSet

// get a task set. if you want the diffs from a previous task set then pass that task set as a param
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE struct PrtTaskSetEnumerator* prtGetTaskSet(struct PrtTaskSetEnumerator *tse)
{
    return (PrtTaskSetEnumerator*)getTaskSet((PrtTaskSetEnumeratorInternal*)tse);
} // prtGetTaskSet


void releaseTaskSet(PrtTaskSetEnumeratorInternal *tse)
{
#ifdef BTL_DEBUGGING
    if (tse == NULL) {
        printf("Prt: releaseTaskSet: NULL task set enumerator, exiting.\n");
        exit(-9);
    }
#endif /* BTL_DEBUGGING */

    assert(tse);
    PrtTaskSetDecRefCount(tse->task_set);
    free(tse);
} //releaseTaskSet


// must call this when you're done with a task set so the tasks therein are free to terminate
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void prtReleaseTaskSet(struct PrtTaskSetEnumerator *tse)
{
    releaseTaskSet((PrtTaskSetEnumeratorInternal*)tse);
} // prtReleaseTaskSet

Prt_Task *nextIterator(PrtTaskSetEnumeratorInternal *tse)
{
    Prt_Task *th;

#ifdef BTL_DEBUGGING
    if (tse == NULL) {
        debugPrintf("Prt: nextIterator: NULL task set enumerator, exiting.\n");
        exit(-9);
    }
#endif /* BTL_DEBUGGING */

    if (tse->cur_iter_index < tse->begin) {
        return NULL;
    }
    if (tse->cur_iter_index > tse->end) {
        return NULL;
    }
    tse->cur_iter_index++;
    th = tse->cur_iter->prt_task;
    tse->cur_iter = tse->cur_iter->next;
#ifdef BTL_DEBUGGING
    if (th == NULL) {
        debugPrintf("Prt: nextIterator: NULL thread in task set, exiting\n");
        exit(-9);
    }
    lockedXAddUint32(&(th->state_endedAndIterRefCt.whole), 0);
    if (th->state_endedAndIterRefCt.parts.isEnded) {
        debugPrintf("Prt: nextIterator: thead %p has already ended!\n", th);
        exit(-9);
    }
#endif /* BTL_DEBUGGING */
    return th;
} //nextIterator

Prt_Task *startIterator(PrtTaskSetEnumeratorInternal *tse)
{
    Prt_Task *th;

#ifdef BTL_DEBUGGING
    if (tse == NULL) {
        debugPrintf("Prt: startIterator: NULL task set enumerator, exiting.\n");
        exit(-9);
    }
    if (tse->task_set == NULL) {
        debugPrintf("Prt: startIterator: NULL tse->task_set, exiting.\n");
        exit(-9);
    }
#endif /* BTL_DEBUGGING */

    tse->cur_iter_index = 0;
    tse->cur_iter       = tse->task_set->head.next;
    while (tse->cur_iter_index < tse->begin) {
#ifdef BTL_DEBUGGING
        if (tse->cur_iter == NULL) {
            debugPrintf("Prt: startIterator: NULL tse->cur_iter, exiting.\n");
            exit(-9);
        }
#endif /* BTL_DEBUGGING */
        tse->cur_iter_index++;
        tse->cur_iter = tse->cur_iter->next;
    }
    if (tse->end == (unsigned)-1 || tse->cur_iter_index > tse->end) {
        return NULL;
    }

    tse->cur_iter_index++;
#ifdef BTL_DEBUGGING
    if (tse->cur_iter == NULL) {
        debugPrintf("Prt: startIterator: NULL tse->cur_iter, exiting\n");
        exit(-9);
    }
#endif /* BTL_DEBUGGING */
    th = tse->cur_iter->prt_task;
    tse->cur_iter = tse->cur_iter->next;
#ifdef BTL_DEBUGGING
    if (th == NULL) {
        debugPrintf("Prt: startIterator: NULL thread in task set, exiting\n");
        exit(-9);
    }
    lockedXAddUint32(&(th->state_endedAndIterRefCt.whole), 0);
    if (th->state_endedAndIterRefCt.parts.isEnded) {
        debugPrintf("Prt: startIterator: thead %p has already ended!\n", th);
        exit(-9);
    }
#endif /* BTL_DEBUGGING */
    return th;
} //startIterator


// after a call to getTaskSet you call this to return the first thread from the enumerator
// can be called multiple times if you want to go over the list more than once.
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtTaskHandle prtStartIterator(struct PrtTaskSetEnumerator *tse)
{
    return (PrtTaskHandle)startIterator((PrtTaskSetEnumeratorInternal*)tse);
} // prtStartIterator


// get the next thread in the iterator.  returns NULL if you're at the end.
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtTaskHandle prtNextIterator(struct PrtTaskSetEnumerator *tse)
{
    return (PrtTaskHandle)nextIterator((PrtTaskSetEnumeratorInternal*)tse);
} // prtNextIterator


PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE PrtBool prtSuspendTask(PrtTaskHandle task)
{
    Prt_Task *pt = (Prt_Task*)task;
    PrtBool res = pt->suspend();
    return res;
} // prtSuspendTask


PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void prtResumeTask(PrtTaskHandle task)
{
    Prt_Task *pt = (Prt_Task*)task;
    pt->resume();
} // prtResumeTask

static unsigned next_tls_offset = 0;

PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE unsigned ptkGetNextTlsOffset(unsigned space_needed) {
    unsigned ret = next_tls_offset;
    next_tls_offset += space_needed;
    return ret;
} // ptkGetNextTlsOffset

PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE unsigned ptkGetMinTlsSpace(void) {
    return next_tls_offset;
}

Prt_ResumeHandle prt_SuspendAllTasks(void)
{
    struct PrtTaskSetEnumerator *tse = prtGetTaskSet(NULL);
    PrtTaskHandle task;
    for (task=prtStartIterator(tse); task; task=prtNextIterator(tse)) {
        prtSuspendTask(task);
    }
    return (Prt_ResumeHandle) tse;
} //prt_SuspendAllTasks


void prt_ResumeAllTasks(Prt_ResumeHandle handle)
{
    struct PrtTaskSetEnumerator *tse = (struct PrtTaskSetEnumerator *)handle;
    PrtTaskHandle task;
    for (task=prtStartIterator(tse); task; task=prtNextIterator(tse)) {
        prtResumeTask(task);
    }
    prtReleaseTaskSet(tse);
} //prt_ResumeAllTasks

#ifdef _WINDOWS
bool operator<(const pthread_t &a, const pthread_t &b) {
    return a.p < b.p;
}
#endif // _WINDOWS

struct prtMutex * prtMutexCreate(const struct prtMutexAttr *attr) {
    pthread_mutex_t *ml = new pthread_mutex_t();
    pthread_mutexattr_t attrs;
    pthread_mutexattr_init(&attrs);
    pthread_mutexattr_settype(&attrs,PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(ml,&attrs);
    pthread_mutexattr_destroy(&attrs);
    return (struct prtMutex*)ml;
}

int prtMutexDestroy(struct prtMutex *Mutex) {
    pthread_mutex_destroy((pthread_mutex_t*)Mutex);
    delete (pthread_mutex_t*)Mutex;
    return 0;
}

int prtMutexLockUnmanaged(struct prtMutex *Mutex) {
    int res = pthread_mutex_lock((pthread_mutex_t*)Mutex);
    return res;
}

int prtMutexLock(struct prtMutex *Mutex) {
    if(prtMutexTrylock(Mutex) == 0) {
        return 0;
    } else {
        return prtMutexLockUnmanaged(Mutex);
    }
}

int prtMutexTrylock(struct prtMutex *Mutex) {
    int res = pthread_mutex_trylock((pthread_mutex_t*)Mutex);
    return res;
}

int prtMutexUnlock(struct prtMutex *Mutex) {
    int res = pthread_mutex_unlock((pthread_mutex_t*)Mutex);
    return res;
}

struct prtCondition * prtConditionInit(struct prtConditionAttr *cond_attr) {
    pthread_cond_t *ml = new pthread_cond_t;
    pthread_cond_init(ml,NULL);
    return (struct prtCondition*)ml;
}

int prtConditionDestroy(struct prtCondition *cond) {
    pthread_cond_destroy((pthread_cond_t*)cond);
    delete (pthread_cond_t*)cond;
    return 0;
}

int prtConditionSignal(struct prtCondition *cond) {
    return pthread_cond_signal((pthread_cond_t*)cond);
}

int prtConditionBroadcast(struct prtCondition *cond) {
    return pthread_cond_broadcast((pthread_cond_t*)cond);
}

int prtConditionWait(struct prtCondition *cond, struct prtMutex *mutex) {
    return pthread_cond_wait((pthread_cond_t*)cond,(pthread_mutex_t*)mutex);
}

PrtRunOnYieldResult prtRunOnYield(PrtTaskHandle task,void (RUN_FUNC_CC * function)(void *),void *arg) {
    Prt_Task *pt = (Prt_Task*)task;
    return pt->runOnYield(function,arg);
}

void prtSleep(unsigned milliseconds) {
#ifdef _WINDOWS
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}
