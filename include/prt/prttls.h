/*
 * COPYRIGHT_NOTICE_1
 */

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/include/prttls.h,v 1.12 2012/06/11 20:25:19 taanders Exp $

/*
 * Pillar Runtime TLS (thread-local storage) functions.
 */

#ifndef _PILLAR_TLS_H_
#define _PILLAR_TLS_H_

#include <assert.h>
// Disable VC6 warning about template type names that are too long for the debugger to handle.
#pragma warning(disable: 4786)
#include <map>
#include "prt/prtmisc.h"
#include "prt/prtvse.h"
#include "prt/prtconcurrency.h"
#include <list>
#include <pthread.h>

class Prt_Task;

//extern tbb::enumerable_thread_specific<Prt_Task *, tbb::cache_aligned_allocator<Prt_Task*>, tbb::ets_key_per_instance> tls_task;
//extern tbb::enumerable_thread_specific<Prt_Task *, tbb::cache_aligned_allocator<Prt_Task*>, tbb::ets_no_key> tls_task;

#ifdef USE_PRT_YIELD_TIMESLICE
#define PRT_YIELD_COUNT_TIMESLICE (1 << 16) // some arbitrary power of 2, for now
#endif // USE_PRT_YIELD_TIMESLICE

typedef struct PrtTaskSetEnumerator *Prt_ResumeHandle;


extern "C" unsigned prtMinFreeStackSpace;


// Enumerate each root of the specified task, which must already be stopped.
void prt_enumerateRootsInStoppedTask(PRT_IN class Prt_Task *pillarTls, PRT_IN PrtRseInfo *rootSetInfo);

// A function in the tls file that will be run exactly once at startup.
void prt_TlsRunOnce(void);

Prt_ResumeHandle prt_SuspendAllTasks(void);

void prt_ResumeAllTasks(Prt_ResumeHandle handle);

// Can be called from managed code to get the current value cached in the TLS register. This should be same as
// the current Pillar task's TLS pointer. Returns NULL if TLS register support is not enabled.
PILLAR_EXPORT PRT_CALL_FROM_MANAGED void *__pdecl prt_getTlsRegister(void);


#define NUM_WATERMARK_STUBS 2000

class TaskSuspendInfo {
public:
    pthread_t suspender_id;
    pthread_mutex_t *the_mutex;
    pthread_cond_t *the_condition_variable;
    bool m_thread_waiting;

    TaskSuspendInfo(void) : the_mutex(NULL), the_condition_variable(NULL), m_thread_waiting(false) {}

    TaskSuspendInfo(pthread_t id,
                    pthread_mutex_t *m,
                    pthread_cond_t *c) :
        suspender_id(id),
        the_mutex(m),
        the_condition_variable(c),
        m_thread_waiting(false) {}
};

PrtBool PRT_CDECL prtPredicateNotEqualPointer(volatile void *location, void *data);

// The Prt_Task data structure that describes a Pillar thread ("task"). Prt_Tasks guarantee to clients that their first 4 bytes
// hold the offset from the TLS start to the client-specific TLS data.
class Prt_Task
{
    // Some field offsets are known and used in the assembly code and by the Pillar compiler.
    // Put those fields first.
public:
#ifdef TLS0
    PrtProvidedTlsHandle  mUserTls;                 // A pointer-sized space for the Pillar user to get task-local storage.
#endif // TLS1
    Prt_Vse              *mLastVse;                 // Last virtual stack entry associated with the task.
#ifndef TLS0
    PrtProvidedTlsHandle  mUserTls;                 // A pointer-sized space for the Pillar user to get task-local storage.
#endif // TLS1
    pthread_t            *mThread;

protected:
    unsigned              mSuspenderSize;
	PrtRseInfo           *mRootSetInfo;             // Will contain a non-NULL callback function pointer if an enumeration needs to be done.

public:
    pthread_mutex_t       mYieldStateLock;
    unsigned              mSelfSuspend;
    std::list<TaskSuspendInfo> mSuspenders;
    bool                  mUnmanaged;
    bool                  mIsEnded;
    unsigned              mRefcount;
    pthread_cond_t        mRefcountDecrease;
    unsigned              mPcallStackSize;
    Prt_Task             *mLastCreated;
    void (RUN_FUNC_CC *mRunOnYield)(void *);
    void *mRunOnYieldArg;

    bool getManagedState(void) {
        return mUnmanaged;
    }

    void suspend_for_head_suspender(void);

    void process_suspenders_list(void) {
        while(mSuspenders.size()) {
            suspend_for_head_suspender();
        }
    }

    bool task_ready(void) {
        return mThread != NULL;
    }

    void yieldUntilReady(void) {
        prtYieldUntil(prtPredicateNotEqualPointer,&mThread,NULL,PrtInfiniteWait64);
    }

    void *mWatermarkStubs, *mWatermarkStubEnd;
	void **mFreeStubIndices;
	unsigned  mCurWatermarkNum;
	unsigned  mFreeStubTop;  // always points at the next poppable index...not the pushable index
	unsigned  mRealEipOffset;
	PrtCodeAddress *get_real_eip_ptr(PrtCodeAddress watermark_stub);
	PrtCodeAddress  getWatermarkStub(PrtCodeAddress real_eip);
	void returnWatermarkStub(PrtCodeAddress free_stub);
	void SetupWatermark(unsigned number);
	void EnlargeWatermark(void);

protected:
#ifdef USE_PRT_YIELD_TIMESLICE
    unsigned                     mYieldCount;              // How many times prtYield() has been called in this task.
#endif // USE_PRT_YIELD_TIMESLICE
    char                         mPadding[128];            // Try to prevent cache line ping-pong effects.

    void selfSuspend(void);
    void selfResume(void);
public:
 	//////////////////////////////////////////////////////////////////////////////
	// Prt_Task creation and management.
	//////////////////////////////////////////////////////////////////////////////

    // Given a pthread, constructs a Prt_Task object that the pthread's TLS will point to.
    Prt_Task(
             PRT_IN pthread_t *thread,
             PRT_IN PrtProvidedTlsHandle tlsHandle
			 );

    ~Prt_Task(void);

	//////////////////////////////////////////////////////////////////////////////
	// User-controlled virtual stack entry functions.
	//////////////////////////////////////////////////////////////////////////////

    // Pushes a user-provided entry onto the virtual stack.
    void pushVse(PRT_IN PrtVseType key, PRT_IN PrtVseHandle value);

    // Pops the top entry from the virtual stack.
    PrtVseHandle popVse(void);

    // Get the underlying pthread handle.
    pthread_t *getThreadHandle(void) const {
        return mThread;
    } // getThreadHandle

    // Enumerates all the VSE roots contained within a Prt_Task
    void enumerate(PRT_IN PrtRseInfo *rootSetInfo);

    // Returns the task's user tls value.
    PrtProvidedTlsHandle getUserTls(void) const {
        return mUserTls;
    } // getUserTls

    // Sets the task's user tls value.
    void setUserTls(PRT_IN PrtProvidedTlsHandle handle) {
        mUserTls = handle;
    } // setUserTls

	void setRootSetEnumYieldFlag(PRT_IN PrtRseInfo *info) {
		mRootSetInfo = info;
	} // setRootSetInfo

    void waitForRootSetEnumToComplete(void) {
        // TO DO: a smarter implementation rather than spinning
        // Spin until mRootSetInfo is NULL, signifying that enumeration has completed.
        while (mRootSetInfo) {
            /*do nothing*/;
        }
    } // waitForRootSetEnumToComplete

	// Returns whether this task needs to yield. Can be called from managed code.
    // Guaranteed not to block or yield so it doesn't need to be explicitly walkable.
	bool needsToYield(void) //const
    {
#ifdef USE_PRT_YIELD_TIMESLICE
        mYieldCount ++;
#endif // USE_PRT_YIELD_TIMESLICE

#ifdef USE_PRT_YIELD_TIMESLICE
        if (mYieldCount % PRT_YIELD_COUNT_TIMESLICE == 0)
            return true;
#endif // USE_PRT_YIELD_TIMESLICE

        return mSuspenders.size() ? true : false;
    } // Prt_Task::needsToYield

	// void yield(void);

    PrtRunOnYieldResult runOnYield(void (RUN_FUNC_CC * function)(void *),void *arg);
    void yieldUnmanaged(void);

#ifdef USE_PRT_YIELD_TIMESLICE
    unsigned getYieldCount(void) { return mYieldCount; }
#endif // USE_PRT_YIELD_TIMESLICE

    // suspend this task
    PrtBool suspend(void);
    // resume this task after a suspend
    void resume(void);

    // If si is NULL, creates a new default StackIterator to walk the current task's stack.
    void printStack(const char *legend, PrtStackIterator *si=NULL);
private:
    Prt_Task(void)               { assert(0); }
    // Prt_Tasks should not be copied!
    Prt_Task(const Prt_Task &)   { assert(0); }
    Prt_Task & operator=(const Prt_Task &) { assert(0); return *this;}
}; //class Prt_Task


extern const int prt_OriginalEspTaskOffset;

class prt_SuspendWrapper {
public:
    prt_SuspendWrapper(void) {
        handle = prt_SuspendAllTasks();
    }
    ~prt_SuspendWrapper(void) {
        prt_ResumeAllTasks(handle);
    }
private:
    Prt_ResumeHandle handle;
}; //prt_SuspendWrapper

typedef _STL::map<PrtVseType, PrtVseRseFunction> Prt_VseRseMap;


// A Prt_VseRseContainer holds the mappings between VSE types and their root set enumerators (if any).
class Prt_VseRseContainer
{
public:
    Prt_VseRseContainer(void) {}

    ~Prt_VseRseContainer(void) {}

    // Registers a root set enumeration function associated with a VSE type.
    void registerVseRse(PRT_IN PrtVseType destructor, PRT_IN PrtVseRseFunction enumerator)
    {
        if (enumerator) {
            prt_SuspendWrapper lock;
            mVseRseFunctions.insert(_STL::pair<PrtVseType, PrtVseRseFunction>(destructor, enumerator));
        }
    } // registerVseRse

    // Call the enumerate roots callback for the given VSE type with the given VSE and RSE callback information.
    void enumerateRoots(PRT_IN PrtVseType type,
                        PRT_IN PrtVseHandle theHandle,
                        PRT_IN PrtRseInfo *rootSetInfo)
    {
        PrtVseRseFunction enumerateRoots = NULL;
        {
            // Protect multiple access to mVseRseFunctions
            // Look up the enumerateRoots function for this VSE type.
            Prt_VseRseMap::const_iterator iter = mVseRseFunctions.find(type);
            if (iter != mVseRseFunctions.end()) {
                enumerateRoots = iter->second;
                assert(enumerateRoots);
            }
        }
        // If an enumeration function was found then invoke it.
        if (enumerateRoots) {
            enumerateRoots(theHandle, rootSetInfo);
        }
    }

protected:
    // Maps PrtVseTypes to their PrtVseRseFunctions.
    Prt_VseRseMap mVseRseFunctions;

private:
    // Shouldn't ever copy this thing...can't hurt but there is no forseen reason to have two copies
    Prt_VseRseContainer(const Prt_VseRseContainer &) { assert(0); }
    Prt_VseRseContainer &operator=(const Prt_VseRseContainer &) { assert(0); return *this; }
}; //class Prt_VseRseContainer


void prt_printVseList(Prt_Vse *initialVsh);

#endif // _PILLAR_TLS_H_
