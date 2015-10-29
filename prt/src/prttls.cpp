/*
 * COPYRIGHT_NOTICE_1
 */

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/base/prttls.cpp,v 1.34 2013/11/13 02:13:06 taanders Exp $

/*
 * Pillar Runtime TLS (thread-local storage) functions.
 */

#include "prtcodegenerator.h"

#include "prtvse.h"
#include "prttls.h"
#include "prtcims.h"
#include "prtcodeinfo.h"
#include "prtglobals.h"
#ifdef _WINDOWS
#include <windows.h>
#endif // _WINDOWS
#ifdef LINUX
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#endif

extern "C" {
    extern void * prtYieldUntilDestructor;
}

void startNewContinuationThread(void *threadData);


// Get the user-level tls entry associated with a Pillar task or NULL if the
// current task is not a Pillar task.
PILLAR_EXPORT PrtProvidedTlsHandle __pcdecl prtGetTls(void)
{
    Prt_Task *pt = prt_GetTask();
    if (pt == NULL) {
        return NULL;
    }
    return pt->getUserTls();
} //prtGetTls


// Sets the user-level TLS entry provided by Pillar tasks.
PILLAR_EXPORT void __pcdecl prtSetTls(PrtProvidedTlsHandle tlsHandle)
{
    Prt_Task *pt = prt_GetTask();
    pt->setUserTls(tlsHandle);
} //prtSetTls


PILLAR_EXPORT PrtProvidedTlsHandle __pcdecl prtGetTlsForTask(PrtTaskHandle pth)
{
    Prt_Task *pt = (Prt_Task*)pth;
    return pt->getUserTls();
} //prtGetTlsForTask


PILLAR_EXPORT void __pcdecl prtSetTlsForTask(PrtTaskHandle pth,PRT_IN PrtProvidedTlsHandle tlsHandle)
{
    Prt_Task *pt = (Prt_Task*)pth;
    pt->setUserTls(tlsHandle);
} //prtSetTlsForTask


extern "C" int prt_needsToYield(Prt_Task *pt)
{
    return pt->needsToYield();
} // prt_needsToYield


struct YieldUntilVse : public Prt_Vse {
   void **        root;
    PrtGcTag       tag;
    void *         gcTagParameter;
    YieldUntilVse(void **r, PrtGcTag t, void *g): root(r), tag(t), gcTagParameter(g) {}
}; // struct YieldUntilVse


struct PredicateData {
    PrtPredicate predicate;
    Prt_Task *pt;
    void *value;
    PredicateData(PrtPredicate p, void *v): predicate(p), pt(prt_GetTask()), value(v) {}
}; // struct PredicateData


static PrtBool predicateAndYieldCheck(volatile void *location, void *data)
{
    struct PredicateData *pData = (struct PredicateData *) data;
    if (pData->pt && prt_needsToYield(pData->pt))
        return PrtTrue;
    return pData->predicate(location, pData->value);
} //predicateAndYieldCheck


void YieldUntilRseFunction(PRT_IN PrtVseHandle theHandle, PRT_IN struct PrtRseInfo *rootSetInfo)
{
    struct YieldUntilVse *yuv = (struct YieldUntilVse*)theHandle;
    if(yuv->root) {
        rootSetInfo->callback((void*)rootSetInfo->env, yuv->root, yuv->tag, yuv->gcTagParameter);
    }
} //YieldUntilRseFunction

#include <limits.h>

#define SPIN_NUMBER 5

extern "C" PrtSyncResult PRT_CDECL prtYieldUntilMovable(PrtPredicate predicate,
                                                      volatile void *location,
                                                      void *value,
                                                      PrtTimeCycles64 timeout,
                                                      PrtGcTag tag,
                                                      void *gcTagParameter)
{
    PrtSyncResult res;
    YieldUntilVse yuv((void**)&(location), tag, gcTagParameter);

    // Push a VSE to protect "location" against being moved by a GC.
    prtPushVse((PrtCodeAddress)&prtYieldUntilDestructor,(struct PrtVseStruct*)&yuv);

    // FIX FIX FIX   timeouts not working!!!
#if 0
    tbb::tick_count timeNow;
    tbb::tick_count::interval_t timeout_time = /*timeNow.my_count +*/ tbb::tick_count::interval_t((double)timeout / 1000000000.0);

    timeNow = tbb::tick_count::now();
#endif
	unsigned count = 0;

    while(!predicate(location,value) /* && timeNow < timeout_time*/) {
        prtYieldUnmanaged();
		++count;
		if(count > SPIN_NUMBER) {
#ifdef _WINDOWS
			Sleep(0);
#if 0
			struct timeval tv;
			tv.tv_sec  = 0;
			tv.tv_usec = 1;
			select(0,0,0,0,&tv);
#endif
#else
                    pthread_yield();
		    //usleep(1);
#endif
		}
    }

//    if (timeNow < timeout_time) {
        res = PrtSyncResultSuccess;
//    } else {
//        res = Timeout;
//    }

    prtPopVse();

    return res;
} //prtYieldUntilMovable

extern "C" PrtSyncResult PRT_CDECL prtYieldUntil(PrtPredicate predicate,
                                               volatile void *location,
                                               void *value,
                                               PrtTimeCycles64 timeout)
{
    PrtSyncResult res;
#if 0
    // FIX FIX FIX timeouts not working!!!
    tbb::tick_count timeNow;
//    tbb::tick_count timeout_time = timeNow + timeout / 1000000000;

    timeNow = tbb::tick_count::now();
#endif
	unsigned count = 0;

    while(!predicate(location,value) /*&& timeNow < timeout_time*/) {
        prtYieldUnmanaged();
		++count;
		if(count > SPIN_NUMBER) {
#ifdef _WINDOWS
			Sleep(0);
#if 0
			struct timeval tv;
			tv.tv_sec  = 0;
			tv.tv_usec = 1;
			select(0,0,0,0,&tv);
#endif
#else
                    pthread_yield();
		    //usleep(1);
#endif
		}
    }

//    if (timeNow < timeout_time) {
        res = PrtSyncResultSuccess;
//    } else {
//        res = Timeout;
//    }

    return res;
} //prtYieldUntil


extern "C" void prt_validateTlsPointer(void *tlsRegister)
{
    Prt_Task *pt = prt_GetTask();
    if (pt != tlsRegister) {
        printf("Pillar runtime error: corrupted TLS register in %p[#%u]\n", pt,
#ifdef _WINDOWS
            pthread_self().p
#else
            pthread_self()
#endif
        );
        printf("TLS register value is 0x%p, expected value is 0x%p\n", tlsRegister, pt);
        assert(pt == tlsRegister);
    }
} //prt_validateTlsPointer


// This version of yield must be called in an unmanaged context.
PILLAR_EXPORT void PRT_CDECL prtYieldUnmanaged(void)
{
    Prt_Task *pt = prt_GetTask();
    if (pt) {
        pt->yieldUnmanaged();
    }
    sched_yield();
} // prtYieldUnmanaged


// Enumerate the roots that are in this task's VSEs.
void Prt_Task::enumerate(struct PrtRseInfo *rootSetInfo)
{
    for (Prt_Vse *pvse = mLastVse; pvse != NULL; pvse = pvse->nextFrame) {
        prt_GetGlobals()->registeredVseTypes.enumerateRoots(pvse->entryTypeCode, (PrtVseHandle)pvse, rootSetInfo);
        if (pvse->targetContinuation != NULL) {
            // There is a pending fat cut possibly containing arguments that need to be enumerated.
            PrtCimSpecificDataType opaqueData;
            Prt_Cim *cim = prt_LookupCodeInfoManager(pvse->targetContinuation->eip, PrtFalse, &opaqueData);
            if (cim) {
                cim->enumerateContinuationRoots(pvse->targetContinuation, rootSetInfo, opaqueData);
            }
        }
    }
} //Prt_Task::enumerate


// Pushes a user-provided entry onto the virtual stack.  Will return NULL in case of failure.
void Prt_Task::pushVse(PrtVseType type, PrtVseHandle value)
{
    Prt_Vse *userEntry            = (Prt_Vse *)value;
    userEntry->entryTypeCode      = type;
    userEntry->nextFrame          = mLastVse;
    userEntry->targetContinuation = NULL;
    mLastVse = userEntry;
} // Prt_Task::pushUserProvidedVse


// Pops and frees a user-provided entry off the virtual stack.  If the current top of the stack is a
// user-provided entry of the specified type "type" then it is removed and we return True, else False.
PrtVseHandle Prt_Task::popVse(void)
{
    Prt_Vse *pvse = mLastVse;
    mLastVse      = pvse->nextFrame;
    return (PrtVseHandle)pvse;
} // Prt_Task::popUserProvidedVse


PrtCodeAddress * Prt_Task::get_real_eip_ptr(PrtCodeAddress watermark_stub)
{
	return (PrtCodeAddress *)((char*)watermark_stub + mRealEipOffset);
} // Prt_Task::get_real_eip_ptr


PrtCodeAddress Prt_Task::getWatermarkStub(PrtCodeAddress real_eip)
{
	PrtCodeAddress next_stub = (PrtCodeAddress)mFreeStubIndices[mFreeStubTop--];
    if (!mFreeStubTop) {
		EnlargeWatermark();
		next_stub = (PrtCodeAddress)mFreeStubIndices[mFreeStubTop--];
    }
    *(get_real_eip_ptr(next_stub)) = real_eip;  // set the correct eip to jump to
	return next_stub;
} // Prt_Task::getWatermarkStub


void Prt_Task::returnWatermarkStub(PrtCodeAddress free_stub)
{
	mFreeStubIndices[++mFreeStubTop] = free_stub;
} // Prt_Task::returnWatermarkStub

#define USE_OLD_STUBS() { mWatermarkStubs = old_stubs_start; mWatermarkStubEnd = old_stubs_end; }
#define USE_NEW_STUBS() { mWatermarkStubs = new_stubs_start; mWatermarkStubEnd = new_stubs_end; }

void Prt_Task::EnlargeWatermark(void) {
	mCurWatermarkNum *= 2;
	// Go through this thread's stack and update watermark stubs.
    // You have to do the prtYoungestActivationFromUnmanagedInTask here
    // because it uses the old watermark stub ranges to determine if
    // the first frame has been visited or not.
    PrtStackIterator _si;
    PrtStackIterator *si = &_si;
    prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle)this);

	delete [] mFreeStubIndices;
	void *old_stubs_start = mWatermarkStubs;
    void *old_stubs_end   = mWatermarkStubEnd;

	SetupWatermark(mCurWatermarkNum);

	void *new_stubs_start = mWatermarkStubs;
    void *new_stubs_end   = mWatermarkStubEnd;

    while (!prtIsActivationPastEnd(si)) {
		if (prtHasFrameBeenVisited(si)) {
			// Only get here if this frame has had its watermark set.
#ifdef __x86_64__
			PrtCodeAddress new_rip = getWatermarkStub(*(si->ripPtr));
			*(si->watermarkPtr)    = new_rip;    // set the return eip to the stub so that we'll know next time through.
			si->ripPtr             = get_real_eip_ptr(*(si->watermarkPtr));
#else  // __x86_64__
			PrtCodeAddress new_eip = getWatermarkStub(*(si->eipPtr));
			*(si->watermarkPtr)    = new_eip;    // set the return eip to the stub so that we'll know next time through.
			si->eipPtr             = get_real_eip_ptr(*(si->watermarkPtr));
#endif // __x86_64__
		}

        USE_OLD_STUBS();
        prtNextActivation(si);
        USE_NEW_STUBS();
    }
	free(old_stubs_start);
} // Prt_Task::EnlargeWatermark


#ifdef LINUX
void * page_malloc(int size) {
#if 1
//    printf("page_malloc\n");
    void *ret;
    posix_memalign(&ret,getpagesize(),size);
    return ret;
#else
    void *mem = malloc(size+getpagesize()+sizeof(void*));
    void **ptr = (void**)((long)((char*)mem+getpagesize()+sizeof(void*)) & ~(getpagesize()-1));
    ptr[-1] = mem;
    return ptr;
#endif
}

void page_free(void *ptr) {
#if 1
    free(ptr);
#else
    free(((void**)ptr)[-1]);
#endif
}
#endif // LINUX

void Prt_Task::SetupWatermark(unsigned number) {
	mCurWatermarkNum = number;
	mFreeStubIndices = new void*[number];
	unsigned watermark_stub_size = (char*)&prt_WatermarkPrototypeEnd - (char*)&prt_WatermarkPrototypeStart;
#ifdef LINUX
	mWatermarkStubs = page_malloc(watermark_stub_size * number);
    assert(mWatermarkStubs);
#else
	mWatermarkStubs = malloc(watermark_stub_size * number);
#endif
	mWatermarkStubEnd = (char*)mWatermarkStubs + (watermark_stub_size * number);

	unsigned top_offset_1      = ((char*)&prt_WatermarkPostTopIndex1      - (char*)&prt_WatermarkPrototypeStart) - sizeof(void*);
#ifndef __x86_64__
	unsigned top_offset_2      = ((char*)&prt_WatermarkPostTopIndex2      - (char*)&prt_WatermarkPrototypeStart) - sizeof(void*);
#endif // __x86_64__
	unsigned stub_stack_offset = ((char*)&prt_WatermarkPostStubStackStart - (char*)&prt_WatermarkPrototypeStart) - sizeof(void*);
	unsigned stub_start_offset = ((char*)&prt_WatermarkPostStubStart      - (char*)&prt_WatermarkPrototypeStart) - sizeof(void*);
	mRealEipOffset             = ((char*)&prt_WatermarkPostRealEipMove    - (char*)&prt_WatermarkPrototypeStart) - sizeof(void*);


	// Initialize the free stub indices stack to contain one pointer to each
	// of the entries in the watermark stub array because they are all free
	// at this point.
	for(mFreeStubTop = 0; mFreeStubTop < number; ++mFreeStubTop) {
		mFreeStubIndices[mFreeStubTop] = (void*)((char*)mWatermarkStubs + (mFreeStubTop * watermark_stub_size));

		memcpy(mFreeStubIndices[mFreeStubTop],&prt_WatermarkPrototypeStart,watermark_stub_size);

		*(void **)((char*)mFreeStubIndices[mFreeStubTop] + top_offset_1)      = &mFreeStubTop;
#ifndef __x86_64__
		*(void **)((char*)mFreeStubIndices[mFreeStubTop] + top_offset_2)      = &mFreeStubTop;
#endif // __x86_64__
		*(void **)((char*)mFreeStubIndices[mFreeStubTop] + stub_stack_offset) = mFreeStubIndices;
		*(void **)((char*)mFreeStubIndices[mFreeStubTop] + stub_start_offset) = mFreeStubIndices[mFreeStubTop];
		*(void **)((char*)mFreeStubIndices[mFreeStubTop] + mRealEipOffset)    = NULL;
	}

	--mFreeStubTop;

#ifdef _WINDOWS
    DWORD old;
    BOOL rr = VirtualProtect(mWatermarkStubs,watermark_stub_size*number,PAGE_EXECUTE_READWRITE,&old);
    if (rr == 0) {
        LPTSTR s;
        DWORD err = GetLastError();
        if(FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                         FORMAT_MESSAGE_FROM_SYSTEM,
                         NULL,
                         err,
                         0,
                         (LPTSTR)&s,
                         0,
                         NULL) == 0) {
            printf("set_page_protection error is UNKNOWN\n");
        }
        else {
            printf("set_page_protection error is %s\n",s);
        }
        exit(-1);
    }
#elif defined LINUX
    if(mprotect(mWatermarkStubs,watermark_stub_size*number,PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        printf("mprotect failed, %d.\n",errno);
        exit(-1);
    }
#else
    printf("Could not make page writable.\n");
    exit(-1);
#endif
} // Prt_Task::SetupWatermark


Prt_Task::Prt_Task(PRT_IN pthread_t *thread,
    PRT_IN PrtProvidedTlsHandle tlsHandle) :
    mUserTls(tlsHandle),
    mSuspenderSize(0),
    mSelfSuspend(0),
#ifdef USE_PRT_YIELD_TIMESLICE
    mYieldCount(0),
#endif // USE_PRT_YIELD_TIMESLICE
    mLastVse(NULL),
    mUnmanaged(false),
    mIsEnded(false),
    mRefcount(0),
    mPcallStackSize(0),
    mLastCreated(NULL),
    mRunOnYield(NULL)
{
//        printf("New task %p\n",this);

	SetupWatermark(NUM_WATERMARK_STUBS);

#ifdef _WIN32
    __asm {
        mov eax, this
        mov fs:[0x14], eax
    }
#elif defined _WIN64
    __asm {
        mov rax, this
        mov fs:[0x14], rax
    }
#else
    pthread_setspecific(prt_GetGlobals()->tls_key, this);
#endif

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mYieldStateLock,&attr);
    pthread_cond_init(&mRefcountDecrease,NULL);

    pthread_attr_t pattr;
    pthread_attr_init(&pattr);

#ifdef _WINDOWS
    pthread_attr_setstacksize(&pattr, 20000000);
    size_t pss;
    pthread_attr_getstacksize(&pattr, &pss);
    if(pss != 20000000) {
        printf("setstacksize problem\n");
    }
#endif // _WINDOWS

    mThread = thread; // don't move this..we use it to know the task is ready to go

    PrtTaskExistenceCallback cb = prt_GetGlobals()->task_callback;
    if (cb != NULL) {
        cb(/*new_task*/ (PrtTaskHandle)this, /*true_if_creation*/ PrtTrue);
    }
} //Prt_Task::Prt_Task


Prt_Task::~Prt_Task(void)
{
//    printf("Ending task %p\n",this);

    PrtTaskExistenceCallback cb = prt_GetGlobals()->task_callback;
    if(cb) {
        cb((PrtTaskHandle)this, PrtFalse);
    }

#ifdef LINUX
	page_free(mWatermarkStubs);
#else
	free(mWatermarkStubs);
#endif
	delete [] mFreeStubIndices;
} // Prt_Task::~PrtTask


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Task suspend/resume implementation //////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Prt_Task::selfSuspend(void) {
    prt_PthreadLockWrapper thisLock(&mYieldStateLock);

    if(!mUnmanaged) {
        printf("A call to selfSuspend from managed code.\n");
        assert(0);
        exit(-2);
    }

    mSelfSuspend++;
    if(prt_GetGlobals()->suspendunmanaged == 0) {
        if(mSuspenders.size()) {
            // If there is a suspender on the suspender's list then they may already be manipulating
            // our stack since we're in unmanaged code.  So, wait for them to finish before we lock
            // ourself.
            suspend_for_head_suspender();
        }
    } else {
        process_suspenders_list();
    }
} // Prt_Task::selfSuspend

void Prt_Task::selfResume(void) {
    prt_PthreadLockWrapper thisLock(&mYieldStateLock);

    if(!mUnmanaged) {
        printf("A call to resume from managed code.\n");
        assert(0);
        exit(-2);
    }

    mSelfSuspend--;
    if(prt_GetGlobals()->suspendunmanaged == 0) {
        if(!mSelfSuspend) {
            // If we've finished with any recursive locks on ourself we might have held then
            // process the suspenders list to allow others to lock us.
            process_suspenders_list();
        }
    }
} // Prt_Task::selfResume

PrtBool Prt_Task::suspend(void) {
    Prt_Task *target_task      = this;
    pthread_t target_thread_id = *mThread;
    pthread_t this_thread_id   = pthread_self();
    Prt_Task *running_task     = NULL;

    std::map<pthread_t,Prt_Task *> *task_map = &(prt_GetGlobals()->gTaskMap);
    std::map<pthread_t,Prt_Task *>::iterator task_iter;
    task_iter = task_map->find(this_thread_id);
    if(task_iter != task_map->end()) {
        running_task = (*task_iter).second;
    }

    if(mIsEnded) {
        return PrtFalse;
    }

#ifdef _WINDOWS
    if(target_thread_id.p == this_thread_id.p) {
#else
    if(target_thread_id == this_thread_id) {
#endif
        if(running_task) {
            running_task->selfSuspend();
            return PrtTrue;
        } else {
            return PrtFalse;
        }
    }

    // Avoid a deadlock if two tasks try to suspend each other at the same time.
#ifdef _WINDOWS
    bool targetHigherInCanonicalOrdering = (target_thread_id.p > this_thread_id.p);
#else
    bool targetHigherInCanonicalOrdering = (target_thread_id > this_thread_id);
#endif

    pthread_cond_t  *target_thread_condition_variable = new pthread_cond_t;
    pthread_cond_init(target_thread_condition_variable,NULL);

    if(targetHigherInCanonicalOrdering) {
        // Deal with the current thread first and then the target thread.

        // If the running task is a Prt_Task then we essentially need to lock ourself.
        if(running_task) {
            running_task->selfSuspend();  // get the lock on ourself
        }

        bool is_unmanaged;

        // Now lock the other thread.
        {
            prt_PthreadLockWrapper targetLock(&mYieldStateLock);
            // The thread has ended so we can't suspend it.
            if(mIsEnded) {
                targetLock.release();

                if(running_task) {
                    running_task->selfResume();
                }

                return PrtFalse;
            }
            // Put an entry on the target thread's suspend list saying we'd like to suspend it.
            mSuspenders.push_back(TaskSuspendInfo(this_thread_id, &mYieldStateLock, target_thread_condition_variable));
            ++mSuspenderSize;
            is_unmanaged = mUnmanaged;
            if(!is_unmanaged || mSuspenderSize > 1 || mSelfSuspend) {
                // Waits for an acknowledgement from the target thread that it is suspended and we can manipulate its stack.
                pthread_cond_wait(target_thread_condition_variable, &mYieldStateLock);

                if(mIsEnded) {
                    targetLock.release();

                    if(running_task) {
                        running_task->selfResume();
                    }

                    return PrtFalse;
                }
            }
            // What kind of sanity check can we do here?  Should be unmanaged?  We should be
            // head of the suspenders list I think.  mSelfSuspend could maybe be 1 I think.
        }
    } else {
        bool is_unmanaged;

        // Deal with the target thread first and then the current thread.
        {
            prt_PthreadLockWrapper targetLock(&mYieldStateLock);
            // The thread has ended so we can't suspend it.
            if(mIsEnded) {
                return PrtFalse;
            }
            // Put an entry on the target thread's suspend list saying we'd like to suspend it.
            mSuspenders.push_back(TaskSuspendInfo(this_thread_id, &mYieldStateLock, target_thread_condition_variable));
            ++mSuspenderSize;
            is_unmanaged = mUnmanaged;
            if(!is_unmanaged || mSuspenderSize > 1 || mSelfSuspend) {
                // Waits for an acknowledgement from the target thread that it is suspended and we can manipulate its stack.
                pthread_cond_wait(target_thread_condition_variable, &mYieldStateLock);

                if(mIsEnded) {
                    return PrtFalse;
                }
            }
        }

        // If the running task is a Prt_Task then we essentially need to lock ourself.
        if(running_task) {
            running_task->selfSuspend(); // Get the lock on ourself
        }
    }

    return PrtTrue;
} // Prt_Task::suspend

void Prt_Task::resume(void) {
    Prt_Task   *target_task  = this;
    pthread_t target_thread_id = *mThread;
    pthread_t this_thread_id = pthread_self();
    Prt_Task   *running_task = NULL;

    std::map<pthread_t,Prt_Task *> *task_map = &(prt_GetGlobals()->gTaskMap);
    std::map<pthread_t,Prt_Task *>::iterator task_iter;
    task_iter = task_map->find(this_thread_id);
    if(task_iter != task_map->end()) {
        running_task = (*task_iter).second;
    }

#ifdef _WINDOWS
    if(target_thread_id.p == this_thread_id.p) {
#else // _WINDOWS
    if(target_thread_id == this_thread_id) {
#endif // _WINDOWS
        if(running_task) {
            running_task->selfResume();
            return;
        } else {
            return;
        }
    }

    TaskSuspendInfo tsi;

	if(!mIsEnded)
    {
        prt_PthreadLockWrapper targetLock(&(mYieldStateLock));
        // Get the entry we placed on the suspender list.
        tsi = mSuspenders.front();
        // Remove that entry from the suspender list.
        mSuspenders.pop_front();
        --mSuspenderSize;

        if(tsi.m_thread_waiting) {
            // Tell the suspended thread to wake up.
            pthread_cond_signal(tsi.the_condition_variable);
        } else {
            delete tsi.the_condition_variable;
        }

        if(prt_GetGlobals()->suspendunmanaged == 0) {
            // If there is another thread waiting to suspend the target thread then signal it to tell it that
            // it now has the lock.
            if(mSuspenders.size()) {
                tsi = mSuspenders.front();
                pthread_cond_signal(tsi.the_condition_variable);
            }
        }
    }

    // If the running task is a Prt_Task then we essentially need to unlock ourself.
    if(running_task) {
        running_task->selfResume();
    }
} // Prt_Task::resume

extern "C" void prtToUnmanaged(Prt_Task *pt) {
    prt_PthreadLockWrapper theLock(&(pt->mYieldStateLock));
    pt->mUnmanaged = true;

    if(prt_GetGlobals()->suspendunmanaged == 0) {
        if(pt->mSuspenders.size()) {
            TaskSuspendInfo tsi = pt->mSuspenders.front();

            // Wake the other thread up and tell it it has suspended us.
            pthread_cond_signal(tsi.the_condition_variable);
        }
    } else {
        while(pt->mSuspenders.size()) {
            TaskSuspendInfo tsi = pt->mSuspenders.front();

            pthread_cond_signal(tsi.the_condition_variable);
            // Wait on the current suspender to release us.
            pthread_cond_wait(tsi.the_condition_variable,&(pt->mYieldStateLock));
        }
    }
} // prtToUnmanaged

extern "C" void prtToManaged(Prt_Task *pt) {
    prt_PthreadLockWrapper theLock(&(pt->mYieldStateLock));

    if(prt_GetGlobals()->suspendunmanaged == 0) {
        while(pt->mSuspenders.size()) {
            TaskSuspendInfo tsi = pt->mSuspenders.front();

            pt->mSuspenders.front().m_thread_waiting = true;

            pthread_cond_signal(tsi.the_condition_variable);
            // Wait on the current suspender to release us.
            pthread_cond_wait(tsi.the_condition_variable,&(pt->mYieldStateLock));

            delete tsi.the_condition_variable;
        }
    }

    pt->mUnmanaged = false;
} // prtToManaged

// If si is NULL, creates a new default StackIterator to walk the current task's stack.
void Prt_Task::printStack(const char *legend, PrtStackIterator *si)
{
    PrtStackIterator local_si;
    PrtStackIterator *psi = si;
    if (psi == NULL) {
        psi = &local_si;
        prtYoungestActivationFromUnmanagedInTask(psi, (PrtTaskHandle)this);
    }
    printf("===========================================================================\n");
    printf("   %s:\n", legend);
    while (!prtIsActivationPastEnd(psi)) {
        char buffer[200];
        char *str = prtGetActivationString(psi, buffer, sizeof(buffer));
#ifdef __x86_64__
        printf("%s, rbx %p @ %p\n", str, (psi->rbxPtr? *(psi->rbxPtr) : (char*)0xDEADDEAD), psi->rbxPtr);
#else  // __x86_64__
        printf("%s, ebx %p @ %p\n", str, (psi->ebxPtr? *(psi->ebxPtr) : (char*)0xDEADDEAD), psi->ebxPtr);
#endif // __x86_64__
        prtNextActivation(psi);
    }
    printf("===========================================================================\n");
    fflush(stdout);
} // Prt_Task::printStack

void prt_TlsRunOnce(void)
{
    prtRegisterVseRseFunction((PrtCodeAddress)&prtYieldUntilDestructor, YieldUntilRseFunction);
} // prt_TlsRunOnce

// Print a task's VSE list starting from "initialVsh".
void prt_printVseList(Prt_Vse *initialVsh) {
    if (initialVsh == NULL) {
        printf("      Empty VSE list\n");
        return;
    }
    for (Prt_Vse *lvse = initialVsh;  lvse != NULL;  lvse = lvse->nextFrame) {
        PrtVseType t = lvse->entryTypeCode;
        const char *nm = "other";
        if (t == PRT_VSE_PCALL) {
            nm = "PCALL";
        } else if (t == PRT_VSE_M2U) {
            nm = "M2U";
        }
        if (lvse->targetContinuation) {
            printf("      VSE %p, type %p (%s), targetCont %p\n", lvse, t, nm, lvse->targetContinuation);
        } else {
            printf("      VSE %p, type %p (%s)\n", lvse, t, nm);
        }
    }
} //prt_printVseList

void Prt_Task::suspend_for_head_suspender(void) {
    // Get the first thread trying to suspend us.
    TaskSuspendInfo tsi = mSuspenders.front();
    // Make sure it isn't ourself.
#ifdef _WINDOWS
    assert(tsi.suspender_id.p != mThread->p);
#else
    assert(tsi.suspender_id != *mThread);
#endif

    {
//        prt_PthreadLockWrapper the_lock(tsi.the_mutex);
        mSuspenders.front().m_thread_waiting = true;

        // Wake the other thread up and tell it it has suspended us.
        pthread_cond_signal(tsi.the_condition_variable);
        // Wait for it to finish.  It will signal us when it is done.
        pthread_cond_wait(tsi.the_condition_variable,tsi.the_mutex);

        delete tsi.the_condition_variable;
    }
}

extern "C" class Prt_Task *prt_GetTaskNonInline(void) {
    return prt_GetTask();
} //prt_GetTask

extern "C" void enterUnmanagedCode(PrtTaskHandle task, PrtVseHandle value) {
    Prt_Task *pt = (Prt_Task*)task;
    pt->pushVse(PRT_VSE_M2U, value);
    prtToUnmanaged(pt);
} // enterUnmanagedCode

extern "C" void reenterManagedCode(PrtTaskHandle task) {
    Prt_Task *pt = (Prt_Task*)task;
    prtToManaged(pt);
    pt->popVse();
} // reenterManagedCode

extern "C" void enterManagedCode(PrtTaskHandle task) {
    Prt_Task *pt = (Prt_Task*)task;
    prtToManaged(pt);
} // enterManagedCode

extern "C" void reenterUnmanagedCode(PrtTaskHandle task) {
    Prt_Task *pt = (Prt_Task*)task;
    prtToUnmanaged(pt);
} // reenterUnmanagedCode

PrtBool PRT_CDECL prtPredicateNotEqualPointer(volatile void *location, void *data) {
    return (PrtBool)((*(volatile void**)location) != ((void *)data));
}

PrtRunOnYieldResult Prt_Task::runOnYield(void (RUN_FUNC_CC *function)(void *),void *arg) {
    prt_PthreadLockWrapper targetLock(&mYieldStateLock);

    // The thread has ended so we can't suspend it.
    if(mIsEnded) {
        return PrtRunOnYieldEnding;
    }

    // The thread is already waiting on another runOnYield request.
    if(mRunOnYield) {
        return PrtRunOnYieldPrevious;
    }

    mRunOnYield = function;
    mRunOnYieldArg = arg;
    mSuspenderSize++;

    return PrtRunOnYieldSuccess;
}

void Prt_Task::yieldUnmanaged(void) {
    if(mSuspenderSize) {
        if(mRunOnYield) {
            void (RUN_FUNC_CC *func_to_run)(void *);

            {
                prt_PthreadLockWrapper targetLock(&mYieldStateLock);
                func_to_run = mRunOnYield;
                mRunOnYield = NULL;
                --mSuspenderSize;
            }

            func_to_run(mRunOnYieldArg);

            //prtInvokeManagedFunc((PrtCodeAddress)func_to_run,&mRunOnYieldArg,1);
        }
    }
}
