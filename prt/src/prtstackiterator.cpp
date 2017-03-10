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

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/base/prtstackiterator.cpp,v 1.12 2013/02/04 19:12:23 taanders Exp $

/*
 * Stack iteration implementation. A stack iterator allows other code to iterator over a task's managed and m2n frames
 * in order from most recently pushed to first pushed. It also allows for resuming a frame in the current task.
 */

#include <string.h>

#include "prt/prtcodegenerator.h"

#include "prt/prtvse.h"
#include "prt/prtcims.h"
#include "prt/prttls.h"
#include "prt/prtcodeinfo.h"
#include "prt/prtglobals.h"
#ifdef __GNUC__
#include <ext/hash_map>
#define HASH_MAP_NAMESPACE __gnu_cxx
#else // __GNUC__
#include <hash_map>
#ifdef _STLP_USE_NAMESPACES
#define HASH_MAP_NAMESPACE _STL
#else
#define HASH_MAP_NAMESPACE stdext
#endif
#endif // __GNUC__

#define HASH_KEY uintptr_t

static HASH_MAP_NAMESPACE::hash_map<HASH_KEY, struct PrtEipUnwinder> eipUnwindMap;

PILLAR_EXPORT void prtAddEipUnwinder(PrtCodeAddress eip, struct PrtEipUnwinder *descriptor)
{
    eipUnwindMap.insert(std::pair<HASH_KEY, struct PrtEipUnwinder>((uintptr_t)eip, *descriptor));
} //prtAddEipUnwinder

static const struct PrtEipUnwinder *getEipUnwinder(PrtCodeAddress eip)
{
    HASH_MAP_NAMESPACE::hash_map<HASH_KEY, struct PrtEipUnwinder>::const_iterator iter = eipUnwindMap.find((uintptr_t)eip);
    if (iter == eipUnwindMap.end()) {
        return NULL;
    }
    return &iter->second;
} //getEipUnwinder

static inline PrtRegister *unwindRegister(PrtRegister *original,
                                          const struct PrtRegisterAdjustment *adjustment,
                                          struct PrtStackIterator *si)
{
    if (adjustment->base == PrtSiRegisterBaseNone)
        return original;
    return (PrtRegister *)(adjustment->adjustment + *(unsigned *)((uintptr_t)si + adjustment->base));
} //unwindRegister


static void printEipRegisterAdjustment(struct PrtRegisterAdjustment adj) {
    enum PrtStackIteratorRegister base = adj.base;
    printf("[");

#ifdef __x86_64__
    if (base == PrtSiRegisterBaseRsp) {
        printf("Rsp");
    } else if (base == PrtSiRegisterBaseVsh) {
        printf("Vsh");
    } else if (base == PrtSiRegisterBaseRbp) {
        printf("Rbp");
    } else if (base == PrtSiRegisterBaseRbx) {
        printf("Rbx");
    } else if (base == PrtSiRegisterBaseR12) {
        printf("R12");
    } else if (base == PrtSiRegisterBaseR13) {
        printf("R13");
    } else if (base == PrtSiRegisterBaseR14) {
        printf("R14");
    } else if (base == PrtSiRegisterBaseR15) {
        printf("R15");
    } else if (base == PrtSiRegisterBaseRip) {
        printf("Rip");
    } else if (base == PrtSiRegisterBaseNone) {
        printf("None");
    }
#else  // __x86_64__
    if (base == PrtSiRegisterBaseEsp) {
        printf("Esp");
    } else if (base == PrtSiRegisterBaseVsh) {
        printf("Vsh");
    } else if (base == PrtSiRegisterBaseEbp) {
        printf("Ebp");
    } else if (base == PrtSiRegisterBaseEbx) {
        printf("Ebx");
    } else if (base == PrtSiRegisterBaseEsi) {
        printf("Esi");
    } else if (base == PrtSiRegisterBaseEdi) {
        printf("Edi");
    } else if (base == PrtSiRegisterBaseEip) {
        printf("Eip");
    } else if (base == PrtSiRegisterBaseNone) {
        printf("None");
    }
#endif // __x86_64__

    if (base != PrtSiRegisterBaseNone) {
        printf("+%u", adj.adjustment);
    }
    printf("]");
} //printEipRegisterAdjustment


static void printEipUnwindDescriptor(const struct PrtEipUnwinder *descriptor)
{
#ifdef __x86_64__
    printf("    RipUnwind Descriptor:\n");
    printf("      rsp: ");  printEipRegisterAdjustment(descriptor->rspAdjustment);  printf("\n");
    printf("      rbp: ");  printEipRegisterAdjustment(descriptor->rbpAdjustment);  printf("\n");
    printf("      rip: ");  printEipRegisterAdjustment(descriptor->ripAdjustment);  printf("\n");
    printf("      r12: ");  printEipRegisterAdjustment(descriptor->r12Adjustment);  printf("\n");
    printf("      r13: ");  printEipRegisterAdjustment(descriptor->r13Adjustment);  printf("\n");
    printf("      r14: ");  printEipRegisterAdjustment(descriptor->r14Adjustment);  printf("\n");
    printf("      r15: ");  printEipRegisterAdjustment(descriptor->r15Adjustment);  printf("\n");
    printf("      rbx: ");  printEipRegisterAdjustment(descriptor->rbxAdjustment);  printf("\n");
#else  // __x86_64__
    printf("    EipUnwind Descriptor:\n");
    printf("      esp: ");  printEipRegisterAdjustment(descriptor->espAdjustment);  printf("\n");
    printf("      ebp: ");  printEipRegisterAdjustment(descriptor->ebpAdjustment);  printf("\n");
    printf("      eip: ");  printEipRegisterAdjustment(descriptor->eipAdjustment);  printf("\n");
    printf("      edi: ");  printEipRegisterAdjustment(descriptor->ediAdjustment);  printf("\n");
    printf("      esi: ");  printEipRegisterAdjustment(descriptor->esiAdjustment);  printf("\n");
    printf("      ebx: ");  printEipRegisterAdjustment(descriptor->ebxAdjustment);  printf("\n");
#endif // __x86_64__
    printf("      vshFramesToPop: %u\n", descriptor->vshFramesToPop);
} //printEipUnwindDescriptor


static PrtBool unwindUsingEipUnwinder(struct PrtStackIterator *si)
{
    Prt_Globals *globals = prt_GetGlobals();
    PrtCodeAddress ip = prtGetActivationIP(si);
    const struct PrtEipUnwinder *descriptor = getEipUnwinder(ip);
    if (descriptor == NULL)
        return PrtFalse;

#ifdef __x86_64__
    PrtRegister  newRsp = (PrtRegister)unwindRegister((PrtRegister *)si->rsp, &descriptor->rspAdjustment, si);
    PrtRegister *newRbp = unwindRegister(si->rbpPtr, &descriptor->rbpAdjustment, si);
    PrtRegister *newRip = unwindRegister((PrtRegister *)si->ripPtr, &descriptor->ripAdjustment, si);
    PrtRegister *newR12 = unwindRegister(si->r12Ptr, &descriptor->r12Adjustment, si);
    PrtRegister *newR13 = unwindRegister(si->r13Ptr, &descriptor->r13Adjustment, si);
    PrtRegister *newR14 = unwindRegister(si->r14Ptr, &descriptor->r14Adjustment, si);
    PrtRegister *newR15 = unwindRegister(si->r15Ptr, &descriptor->r15Adjustment, si);
    PrtRegister *newRbx = unwindRegister(si->rbxPtr, &descriptor->rbxAdjustment, si);
    PrtVseHandle newVsh = si->vsh;
    for (int i=0; i<descriptor->vshFramesToPop; i++)
        newVsh = prtGetNextVse(newVsh);

    prtSetStackIteratorFields(si, (PrtCodeAddress *)newRip, newRsp, newRbx, newRbp, newR12, newR13, newR14, newR15, newVsh, 0);
#else  // __x86_64__
    PrtRegister  newEsp = (PrtRegister)unwindRegister((PrtRegister *)si->esp, &descriptor->espAdjustment, si);
    PrtRegister *newEbp = unwindRegister(si->ebpPtr, &descriptor->ebpAdjustment, si);
    PrtRegister *newEip = unwindRegister((PrtRegister *)si->eipPtr, &descriptor->eipAdjustment, si);
    PrtRegister *newEdi = unwindRegister(si->ediPtr, &descriptor->ediAdjustment, si);
    PrtRegister *newEsi = unwindRegister(si->esiPtr, &descriptor->esiAdjustment, si);
    PrtRegister *newEbx = unwindRegister(si->ebxPtr, &descriptor->ebxAdjustment, si);
    PrtVseHandle newVsh = si->vsh;
    for (int i=0; i<descriptor->vshFramesToPop; i++)
        newVsh = prtGetNextVse(newVsh);

    prtSetStackIteratorFields(si, (PrtCodeAddress *)newEip, newEsp, newEbx, newEbp, newEsi, newEdi, newVsh, 0);
#endif // __x86_64__
    return PrtTrue;
} //unwindUsingEipUnwinder

static HASH_MAP_NAMESPACE::hash_map<HASH_KEY, struct PrtEipRse *> eipRseMap;

PILLAR_EXPORT void prtAddEipRse(PrtCodeAddress eip, struct PrtEipRse *descriptor)
{
    struct PrtEipRse *newDescriptor = (struct PrtEipRse *) malloc(sizeof(*newDescriptor));
    newDescriptor->numRegisterRoots = descriptor->numRegisterRoots;
    newDescriptor->numStackEspRoots = descriptor->numStackEspRoots;
    newDescriptor->registerRoots = (struct PrtRseDescriptor *) malloc(descriptor->numRegisterRoots * sizeof(*descriptor->registerRoots));
    memcpy(newDescriptor->registerRoots, descriptor->registerRoots, descriptor->numRegisterRoots * sizeof(*descriptor->registerRoots));
    newDescriptor->stackEspRoots = (struct PrtRseDescriptor *) malloc(descriptor->numStackEspRoots * sizeof(*descriptor->stackEspRoots));
    memcpy(newDescriptor->stackEspRoots, descriptor->stackEspRoots, descriptor->numStackEspRoots * sizeof(*descriptor->stackEspRoots));
    eipRseMap.insert(std::pair<HASH_KEY, struct PrtEipRse *>((uintptr_t)eip, newDescriptor));
    unsigned i;
    for (i=0; i<newDescriptor->numRegisterRoots; i++) {
        assert((unsigned)newDescriptor->registerRoots[i].offset.registerId < sizeof(PrtStackIterator));
    }
    for (i=0; i<newDescriptor->numStackEspRoots; i++) {
        //assert((unsigned)newDescriptor->stackEspRoots[i].offset.stackOffset < 4000);
    }
} //prtAddEipRse

static const struct PrtEipRse *getEipRse(PrtCodeAddress eip)
{
    HASH_MAP_NAMESPACE::hash_map<HASH_KEY, struct PrtEipRse *>::const_iterator iter = eipRseMap.find((uintptr_t)eip);
    if (iter == eipRseMap.end()) {
        return NULL;
    }
    return iter->second;
} //getEipRse

static PrtBool enumerateUsingEipRse(struct PrtStackIterator *si, struct PrtRseInfo *rootSetInfo)
{
    const struct PrtEipRse *descriptor = getEipRse(prtGetActivationIP(si));
    if (descriptor == NULL)
        return PrtFalse;
    unsigned i;
    void **addr;
    for (i=0; i<descriptor->numRegisterRoots; i++) {
        assert((unsigned)descriptor->registerRoots[i].offset.registerId < sizeof(PrtStackIterator));
        addr = *(void ***)((int)descriptor->registerRoots[i].offset.registerId + (char *)si);
        rootSetInfo->callback(rootSetInfo->env, addr, descriptor->registerRoots[i].tag, descriptor->registerRoots[i].parameter);
    }
    for (i=0; i<descriptor->numStackEspRoots; i++) {
#ifdef __x86_64__
        addr = (void **)((int)descriptor->stackEspRoots[i].offset.stackOffset + si->rsp);
#else  // __x86_64__
        addr = (void **)((int)descriptor->stackEspRoots[i].offset.stackOffset + si->esp);
#endif // __x86_64__
        rootSetInfo->callback(rootSetInfo->env, addr, descriptor->stackEspRoots[i].tag, descriptor->stackEspRoots[i].parameter);
    }
    return PrtTrue;
}

//////////////////////////////////////////////////////////////////////////
// Stack Iterator Implementation
//////////////////////////////////////////////////////////////////////////

// Uncomment the following lines to enable debugging output
//#define DEBUG_STACK_WALKING 1
//#define DEBUG_ROOT_SET_ENUMERATION 1

#ifdef __x86_64__
PILLAR_EXPORT void prtSetStackIteratorFields(struct PrtStackIterator *context,
                                             PrtCodeAddress *ripPtr,
                                             PrtRegister rsp,
                                             PrtRegister *rbxPtr,
                                             PrtRegister *rbpPtr,
                                             PrtRegister *r12Ptr,
                                             PrtRegister *r13Ptr,
                                             PrtRegister *r14Ptr,
                                             PrtRegister *r15Ptr,
                                             PrtVseHandle vsh,
                                             long virtualFrameNumber)
{
    context->ripPtr = ripPtr;
    context->rsp    = rsp;
    context->rbxPtr = rbxPtr;
    context->rbpPtr = rbpPtr;
    context->r12Ptr = r12Ptr;
    context->r13Ptr = r13Ptr;
    context->r14Ptr = r14Ptr;
    context->r15Ptr = r15Ptr;
    context->vsh    = vsh;
    context->virtualFrameNumber = virtualFrameNumber;
} //prtSetStackIteratorFields
#else  // __x86_64__
PILLAR_EXPORT void prtSetStackIteratorFields(struct PrtStackIterator *context,
                                                    PrtCodeAddress *eipPtr,
                                                    PrtRegister esp,
                                                    PrtRegister *ebxPtr,
                                                    PrtRegister *ebpPtr,
                                                    PrtRegister *esiPtr,
                                                    PrtRegister *ediPtr,
                                                    PrtVseHandle vsh,
                                                    unsigned virtualFrameNumber)
{
    context->eipPtr = eipPtr;
    context->esp = esp;
    context->ebxPtr = ebxPtr;
    context->ebpPtr = ebpPtr;
    context->esiPtr = esiPtr;
    context->ediPtr = ediPtr;
    context->vsh = vsh;
    context->virtualFrameNumber = virtualFrameNumber;
} //prtSetStackIteratorFields
#endif // __x86_64__


static void initializeStackIteratorFromUnmanagedFrame(PrtStackIterator *si, Prt_Task *task)
{
    assert(task);
    assert(si);        // client must provide storage for the stack iterator
    memset(si, 0, sizeof(PrtStackIterator));
    si->originalVsh         = (PrtVseHandle)task->mLastVse;
	si->task_for_this_stack = (PrtTaskHandle)task;
    prt_unwindToLatestM2u(si, task->mLastVse);
} //initializeStackIteratorFromUnmanagedFrame


// Create a new stack iterator for the current task assuming the thread is currently in unmanaged code.
PILLAR_EXPORT void prtYoungestActivationFromUnmanaged(PrtStackIterator *si)
{
    prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle) prt_GetTask());
} //prtYoungestActivationFromUnmanaged


// Create a new stack iterator for the given task assuming it is currently in unmanaged code.
PILLAR_EXPORT void prtYoungestActivationFromUnmanagedInTask(PrtStackIterator *si, PrtTaskHandle theTaskHandle)
{
    Prt_Task *task = (Prt_Task *)theTaskHandle;
    initializeStackIteratorFromUnmanagedFrame(si, task);
} //prtYoungestActivationFromUnmanagedInTask


/* Go to the managed frame previous to the current one. */
PILLAR_EXPORT void prtNextActivation(PrtStackIterator *si)
{
    assert(si);
    if (prtIsActivationPastEnd(si)) {
        return;
    }
    PrtCodeAddress ip = prtGetActivationIP(si);
    if (!unwindUsingEipUnwinder(si)) {
        PrtCimSpecificDataType opaqueData;
        Prt_Cim *cim = prt_LookupCodeInfoManager(ip, PrtTrue, &opaqueData);
        if (cim) {
            cim->getPreviousFrame(si, opaqueData);
#ifdef DEBUG_STACK_WALKING
            const char *cimName = cim->getCimName();
            if (prtIsActivationPastEnd(si)) {
                printf("  After prtNextActivation from %p with cim=%p(%s): activation is past end!\n", ip, cim, cimName);
            } else {
                char buffer[200];
                char *afterStr = prtGetActivationString(si, buffer, sizeof(buffer));
                printf("  After prtNextActivation from %p with cim=%p(%s): %s\n", ip, cim, cimName, afterStr);
            }
#endif //DEBUG_STACK_WALKING
        } else {
            prt_ErrorNoCim(si, "prtNextActivation");
            return;
        }
    }

	// Update watermark if necessary.
    PrtCodeAddress new_ip = prtGetActivationIP(si);
    Prt_Task *pt = (Prt_Task*)si->task_for_this_stack;
    if (new_ip &&
        new_ip >= pt->mWatermarkStubs &&
        new_ip <= pt->mWatermarkStubEnd) {
#ifdef __x86_64__
        si->watermarkPtr = si->ripPtr;
        si->ripPtr = pt->get_real_eip_ptr(*(si->watermarkPtr));
#else  // __x86_64__
        si->watermarkPtr = si->eipPtr;
        si->eipPtr = pt->get_real_eip_ptr(*(si->watermarkPtr));
#endif // __x86_64__
    } else {
        si->watermarkPtr = NULL; // not a special watermark frame
    }
} //prtNextActivation


// Return a string describing the current frame.
PILLAR_EXPORT char *prtGetActivationString(PrtStackIterator *si, char *buffer, unsigned bufferSize)
{
    assert(buffer);
    if (prtIsActivationPastEnd(si)) {
        buffer[0] = '\0';
        return buffer;
    }
	PrtCimSpecificDataType opaqueData;
    Prt_Cim *cim = prt_LookupCodeInfoManager(prtGetActivationIP(si), PrtTrue, &opaqueData);
    if (cim != NULL) {
        return cim->getStringForFrame(si, buffer, bufferSize, opaqueData);
    } else {
        PrtCodeAddress ip = prtGetActivationIP(si);
#ifdef __x86_64__
        _snprintf(buffer, bufferSize, "Unmanaged frame: ip=%p, rsp=%p, vsh=%p, vfn=%u",
            ip, si->rsp, si->vsh, si->virtualFrameNumber);
#else  // __x86_64__
        _snprintf(buffer, bufferSize, "Unmanaged frame: ip=%p, esp=%p, vsh=%p, vfn=%u",
            ip, si->esp, si->vsh, si->virtualFrameNumber);
#endif // __x86_64__
        return buffer;
    }
} //prtGetActivationString


// Enumerate the current frame's roots.
PILLAR_EXPORT void prtEnumerateRootsOfActivation(PrtStackIterator *si, struct PrtRseInfo *rootSetInfo)
{
    assert(si);
    if (prtIsActivationPastEnd(si)) {
        return;
    }
    if (enumerateUsingEipRse(si, rootSetInfo)) {
        return;
    }
	PrtCimSpecificDataType opaqueData;
    Prt_Cim *cim = prt_LookupCodeInfoManager(prtGetActivationIP(si), PrtTrue, &opaqueData);
    if (cim) {
        cim->enumerateRoots(si, rootSetInfo, opaqueData);
    } else {
        prt_ErrorNoCim(si, "prtEnumerateRootsOfActivation");
    }
} //prtEnumerateRootsOfActivation


// Enumerate the roots of each of the VSE's in the given task's virtual stack.
PILLAR_EXPORT void prtEnumerateVseRootsOfTask(PrtTaskHandle theTaskHandle, struct PrtRseInfo *rootSetInfo)
{
    Prt_Task *task = (Prt_Task *)theTaskHandle;
    task->enumerate(rootSetInfo);
} // prtEnumerateVseRootsOfTask


// Enumerate each root of the specified task, which must already be stopped.
void prt_enumerateRootsInStoppedTask(Prt_Task *task, struct PrtRseInfo *rootSetInfo)
{
#ifdef DEBUG_ROOT_SET_ENUMERATION
    printf("\nprtEnumerateTaskRootSet[%p]\n", task);
#endif //DEBUG_ROOT_SET_ENUMERATION

    // First enumerate each frame on the task's stack.
    PrtStackIterator _si;
    PrtStackIterator *si = &_si;
    prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle)task);
    while (!prtIsActivationPastEnd(si)) {
#ifdef DEBUG_ROOT_SET_ENUMERATION
        char buffer[200];
        char *str = prtGetActivationString(si, buffer, sizeof(buffer));
        PrtCodeAddress ip = prtGetActivationIP(si);
        printf("  [%p]: ip=%p, %s\n", task, ip, str);
#endif // DEBUG_ROOT_SET_ENUMERATION
        prtEnumerateRootsOfActivation(si, rootSetInfo);
        prtNextActivation(si);
    }

    // Now enumerate the task's VSE roots.
    prtEnumerateVseRootsOfTask((PrtTaskHandle)task, rootSetInfo);

    // Enumerate roots in the TLS area.
    prtEnumerateTlsRootSet((PrtTaskHandle)task, rootSetInfo);
} // prt_enumerateRootsInStoppedTask

// Enumerate all roots in the specified task after stopping it.
PILLAR_EXPORT void prtEnumerateTaskRootSet(PrtTaskHandle theTaskHandle, struct PrtRseInfo *rootSetInfo)
{
    Prt_Task *task = (Prt_Task *)theTaskHandle;
    prt_enumerateRootsInStoppedTask(task, rootSetInfo);
} //prtEnumerateTaskRootSet

PILLAR_EXPORT void PRT_CDECL prtRegisterGlobalEnumerator(PrtGlobalEnumerator enumerator)
{
	Prt_Globals *globals = prt_GetGlobals();
    assert(enumerator);
    globals->numGlobalEnumerators ++;
    globals->globalEnumeratorList = (PrtGlobalEnumerator *) realloc(globals->globalEnumeratorList, globals->numGlobalEnumerators * sizeof(PrtGlobalEnumerator));
    globals->globalEnumeratorList[globals->numGlobalEnumerators - 1] = enumerator;
} //prtRegisterGlobalEnumerator

PILLAR_EXPORT void PRT_CDECL prtRegisterTlsEnumerator(PrtTlsEnumerator enumerator)
{
	Prt_Globals *globals = prt_GetGlobals();
    assert(enumerator);
    globals->numTlsEnumerators ++;
    globals->tlsEnumeratorList = (PrtTlsEnumerator *) realloc(globals->tlsEnumeratorList, globals->numTlsEnumerators * sizeof(PrtTlsEnumerator));
    globals->tlsEnumeratorList[globals->numTlsEnumerators - 1] = enumerator;
} //prtRegisterTlsEnumerator

PILLAR_EXPORT void PRT_CDECL prtEnumerateTlsRootSet(PrtTaskHandle theTaskHandle, struct PrtRseInfo *rootSetInfo)
{
    Prt_Task *task = (Prt_Task *) theTaskHandle;
	Prt_Globals *globals = prt_GetGlobals();
    PrtProvidedTlsHandle tls = task->getUserTls();
    for (unsigned i=0; i<globals->numTlsEnumerators; i++) {
        globals->tlsEnumeratorList[i](tls, rootSetInfo);
    }
} //prtEnumerateTlsRootSet

/*
 * Enumerate all non-thread local roots.
 */
PILLAR_EXPORT void prtEnumerateGlobalRootSet(PRT_IN struct PrtRseInfo *rootSetInfo)
{
	Prt_Globals *globals = prt_GetGlobals();
    for (unsigned i=0; i<globals->numGlobalEnumerators; i++) {
        globals->globalEnumeratorList[i](rootSetInfo);
    }
	{
		Prt_Globals *globals = prt_GetGlobals();

		prt_PthreadLockWrapper thisLock(&(prt_GetGlobals()->gPcallLock));
		std::map<pthread_t, Prt_WrappedTaskData *>::iterator pcall_iter;
		for(pcall_iter  = globals->gPcallMap.begin();
			pcall_iter != globals->gPcallMap.end();
		  ++pcall_iter) {
		    Prt_WrappedTaskData *wrapped_data = pcall_iter->second;
			PrtPcallArgEnumerator enumerator = wrapped_data->getEnumerator();
			if(enumerator) {
				enumerator(wrapped_data->getStartAddress(),
				           wrapped_data->getTaskData(),
				           rootSetInfo,
				           NULL);
			}
		}
	}
} // prtEnuemrateGlobalRootSet

// Return a continuation corresponding to a continuation listed in an "also unwinds to" clause.
PILLAR_EXPORT struct PrtContinuation *prtGetUnwindContinuation(struct PrtStackIterator *si, unsigned continuationIndex)
{
    assert(si);
    if (prtIsActivationPastEnd(si)) {
        return NULL;
    }
	PrtCimSpecificDataType opaqueData;
    Prt_Cim *cim = prt_LookupCodeInfoManager(prtGetActivationIP(si), PrtTrue, &opaqueData);
    if (cim != NULL) {
        return cim->getUnwindContinuation(si, continuationIndex, opaqueData);
    } else {
        prt_ErrorNoCim(si, "prtGetUnwindContinuation");
        return NULL;
    }
} //prtGetUnwindContinuation


// Returns the value associated with the smallest span tagged with "key" and containing the program point
// where "si" is suspended.  Returns (PrtDataPointer)NULL if there is no such span.
PILLAR_EXPORT PrtDataPointer prtGetSpanDescriptor(struct PrtStackIterator *si, unsigned key)
{
    assert(si);
    if (prtIsActivationPastEnd(si)) {
        return NULL;
    }
    PrtCimSpecificDataType opaqueData;
    Prt_Cim *cim = prt_LookupCodeInfoManager(prtGetActivationIP(si), PrtTrue, &opaqueData);
    if (cim) {
        return cim->getSpanDescriptor(si, key, opaqueData);
    } else {
        prt_ErrorNoCim(si, "prtGetSpanDescriptor");
        return NULL;
    }
} //prtGetSpanDescriptor


// Return the ip for the current frame.
#ifdef prtGetActivationIP
#undef prtGetActivationIP
#endif // prtGetActivationIP
PILLAR_EXPORT PrtCodeAddress prtGetActivationIP(PrtStackIterator *si)
{
    assert(si);
    PrtCodeAddress ip = 0;
#ifdef __x86_64__
    assert(si->ripPtr);
    if (si->ripPtr != NULL) {
        ip = *si->ripPtr;
    }
#else  // __x86_64__
    assert(si->eipPtr);
    if (si->eipPtr != NULL) {
        ip = *si->eipPtr;
    }
#endif // __x86_64__
    return ip;
} //prtGetActivationIP


// Return PrtTrue if no frames remain on the stack.
#ifdef prtIsActivationPastEnd
#undef prtIsActivationPastEnd
#endif // prtIsActivationPastEnd
PILLAR_EXPORT PrtBool prtIsActivationPastEnd(PrtStackIterator *si)
{
    assert(si);
#ifdef __x86_64__
    return ((si->ripPtr == NULL)? PrtTrue : PrtFalse);
#else  // __x86_64__
    return ((si->eipPtr == NULL)? PrtTrue : PrtFalse);
#endif // __x86_64__
} //prtIsActivationPastEnd


/* Marks the frame as having been seen during a stack walk. */
PILLAR_EXPORT void PRT_CDECL prtMarkFrameAsVisited(PRT_IN struct PrtStackIterator *context)
{
	Prt_Task *pt = (Prt_Task*)context->task_for_this_stack;

#ifdef __x86_64__
	PrtCodeAddress new_rip = pt->getWatermarkStub(*(context->ripPtr));
	*(context->ripPtr) = new_rip;    // set the return eip to the stub so that we'll know next time through.

	// fix the two eip pointers
	context->watermarkPtr = context->ripPtr;
	context->ripPtr       = pt->get_real_eip_ptr(*(context->watermarkPtr));
#else  // __x86_64__
	PrtCodeAddress new_eip = pt->getWatermarkStub(*(context->eipPtr));
	*(context->eipPtr) = new_eip;    // set the return eip to the stub so that we'll know next time through.

	// fix the two eip pointers
	context->watermarkPtr = context->eipPtr;
	context->eipPtr       = pt->get_real_eip_ptr(*(context->watermarkPtr));
#endif // __x86_64__
} // prtMarkFrameAsVisited

/* Returns true if the frame was previously marked with prtMarkFrameAsVisited. */
PILLAR_EXPORT PrtBool PRT_CDECL prtHasFrameBeenVisited(PRT_IN const struct PrtStackIterator *context)
{
	if (context->watermarkPtr != NULL) return PrtTrue;
	else                               return PrtFalse;
} // prtHasFrameBeenVisited
