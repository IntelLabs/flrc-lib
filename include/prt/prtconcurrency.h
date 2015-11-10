/*
 * COPYRIGHT_NOTICE_1
 */

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/include/prtconcurrency.h,v 1.4 2013/02/04 19:12:23 taanders Exp $

#ifndef _PRTCONCURRENCY_H
#define _PRTCONCURRENCY_H

#include "prt/prt.h"

// Prt_WrappedTaskData. Holds the PrtCodeAddress, client-supplied task data, and
// total additional client-specific data needed for a task being started.
class Prt_WrappedTaskData
{
public:
    // Initialize an object of this type.
    Prt_WrappedTaskData(PRT_IN PrtCodeAddress proc,
                        PRT_IN void *data,
                        PRT_IN unsigned dataSize /* in 4-byte quantities */,
                        PRT_IN PrtProvidedTlsHandle tlsHandle,
                        PRT_IN void *new_mem,
						PRT_IN PrtPcallArgEnumerator enumerator) :
        initialProc(proc),
        taskData(data),
        mDataSize(dataSize),
        mTlsHandle(tlsHandle),
        newTaskMemory(new_mem),
		argRefEnumerator(enumerator) {}

    // Copy constructor.
    Prt_WrappedTaskData(PRT_IN const Prt_WrappedTaskData &rhs) :
        initialProc(rhs.initialProc),
        taskData(rhs.taskData),
        mDataSize(rhs.mDataSize),
        mTlsHandle(rhs.mTlsHandle),
        newTaskMemory(rhs.newTaskMemory),
		argRefEnumerator(rhs.argRefEnumerator) {}

    // Getters

    PrtCodeAddress getStartAddress(void) const {
        return initialProc;
    }
    void  *getTaskData(void) const {
        return taskData;
    }
    PrtProvidedTlsHandle getTlsHandle(void) const {
        return mTlsHandle;
    }
    unsigned getDataSize(void) const {
        return mDataSize;
    }
    void * getNewTaskMemory(void) const {
        return newTaskMemory;
    }

	PrtPcallArgEnumerator getEnumerator(void) const {
		return argRefEnumerator;
	}

protected:
    PrtCodeAddress       initialProc;
    void                *taskData;
    unsigned             mDataSize;
    PrtProvidedTlsHandle mTlsHandle;
    void                *newTaskMemory;
 	PrtPcallArgEnumerator argRefEnumerator;
}; //class Prt_WrappedTaskData


void prt_BootstrapTask(PRT_INOUT Prt_WrappedTaskData *wrappedData, PRT_IN bool deleteWrappedData);

#endif // !_PRTCONCURRENCY_H
