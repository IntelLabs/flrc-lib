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

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/include/prtglobals.h,v 1.8 2013/02/04 19:12:23 taanders Exp $

/*
 * Declares the global variables for a Pillar instance, which are collected into a single Prt_Globals structure.
 */

#ifndef _PILLAR_GLOBALS_H_
#define _PILLAR_GLOBALS_H_

#ifdef TEST_TASK_SET
#pragma warning(disable: 4786)
#include <set>
#endif // TEST_TASK_LIST

#include <list>
#include <set>
#include <map>

// JMS 2006-08-23.  Prt_Globals.codeInfoManagers in now a pointer to a Prt_CodeIntervalTree,
// whose definition is opaque.  This requires special new/delete/postThreadSystemInit wrapper functions.
// The purpose is to limit VC++ 6.0's "warning 4786" messages to the compilation of
// prtcodeinfo.cpp, which arise from the use of templates.
class Prt_CodeIntervalTree;

#include "prt/prtcodeinfo.h"
#include <pthread.h>

#ifdef _WINDOWS
bool operator<(const pthread_t &a, const pthread_t &b);
#endif // _WINDOWS

class Prt_WrappedTaskData;

// This struct holds all globals used by the Pillar runtime. This will simplify duplicating them in the future if necessary.
// This also makes it easy to track how much global state the Pillar runtime needs.
struct Prt_Globals {
    Prt_CodeIntervalTree *codeInfoManagers;   // The code info managers that are registered with the runtime.
    Prt_VseRseContainer   registeredVseTypes; // If you created your own VSE types then the manager for that type is contained herein.
    std::map<pthread_t, Prt_Task *> gTaskMap;
    pthread_mutex_t                 gTaskSetLock;
	std::map<pthread_t, Prt_WrappedTaskData *> gPcallMap;
    pthread_mutex_t                 gPcallLock;
    PrtTaskExistenceCallback task_callback;

    // Command line flags.
    int                   verboseStackExtension; // Print messages when the stack is extended (default=0)
    int                   logEvents;             // Enable logging of events to a file
    int                   suspendunmanaged;

    PrtGlobalEnumerator  *globalEnumeratorList;
    unsigned              numGlobalEnumerators;
    PrtTlsEnumerator     *tlsEnumeratorList;
    unsigned              numTlsEnumerators;

#if !defined _WIN32 && !defined _WIN64
    pthread_key_t tls_key;
#endif

    Prt_Globals() {
        codeInfoManagers = prt_CreateCodeIntervalTree();
        task_callback = NULL;
        verboseStackExtension = 0;
        logEvents = 0;
        suspendunmanaged = 0;
        globalEnumeratorList = NULL;
        numGlobalEnumerators = 0;
        tlsEnumeratorList = NULL;
        numTlsEnumerators = 0;
        pthread_mutex_init(&gTaskSetLock,NULL);
        pthread_mutex_init(&gPcallLock,NULL);
#if !defined _WIN32 && !defined _WIN64
        tls_key = 0;
#endif
    }

    ~Prt_Globals() {
        prt_DeleteCodeIntervalTree(codeInfoManagers);
        codeInfoManagers = NULL;
    }
}; //struct Prt_Globals


Prt_Globals *prt_GetGlobals(void);

extern "C" inline class Prt_Task *prt_GetTask(void)
{
#ifdef _WIN32
    __asm {
        mov eax, fs:[0x14]
    }
#elif defined _WIN64
    __asm {
        mov rax, fs:[0x28]
    }
#else
    return (Prt_Task*)pthread_getspecific(prt_GetGlobals()->tls_key);
#endif
} //prt_GetTask


class prt_PthreadLockWrapper {
protected:
    bool m_held;
public:
    prt_PthreadLockWrapper(void) : mMux(NULL), m_held(false) {}

    prt_PthreadLockWrapper(PRT_INOUT pthread_mutex_t *mux, PRT_IN bool callYield=false) : mMux(mux), m_held(false)
    {
        if(mMux) {
            pthread_mutex_lock(mMux);
            m_held = true;
        }
    }

    ~prt_PthreadLockWrapper(void)
    {
        release();
    }

    void release(void) {
        if (mMux && m_held) {
            if(pthread_mutex_unlock(mMux) != 0) {
                assert(0);
            }
        }
    }
protected:
    pthread_mutex_t *mMux;
}; // prt_PthreadLockWrapper

#ifdef LINUX
#include <stdint.h>
#ifdef __x86_64__
#define PRT_POINTER_SIZE_INT int64_t
#else  // ia32
#define PRT_POINTER_SIZE_INT int32_t
#endif // ia32
#else // LINUX
#ifdef __x86_64__
#define PRT_POINTER_SIZE_INT __int64
#else  // ia32
#define PRT_POINTER_SIZE_INT __int32
#endif // ia32
#endif // LINUX

#ifndef _WINDOWS
#define _snprintf(x,y,...) sprintf(x, __VA_ARGS__ )
#endif // !_WINDOWS

#endif // _PILLAR_GLOBALS_H_
