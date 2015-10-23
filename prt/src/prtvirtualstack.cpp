/*
 * COPYRIGHT_NOTICE_1
 */

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/base/prtvirtualstack.cpp,v 1.2 2011/03/09 19:09:53 taanders Exp $

#include "prtcodegenerator.h"

#include "prtvse.h"
#include "prttls.h"
#include "prtcodeinfo.h"
#include "prtglobals.h"

PILLAR_EXPORT PrtVseHandle prtGetVsh(void)
{
    return (PrtVseHandle) prt_GetTask()->mLastVse;
} // prtGetVsh


// Registers a set of callback functions as belonging to a logical module that is responsible for understanding
// one or more regions of code for stackwalking, exceptions, root set enumeration, etc.
PILLAR_EXPORT void prtRegisterVseRseFunction(PrtVseType vseType, PrtVseRseFunction enumerator)
{
    prt_GetGlobals()->registeredVseTypes.registerVseRse(vseType, enumerator);
} // prtRegisterVseRseFunction


// Pushes "value" to the top of the Pillar virtual stack. Returns True if successful, False otherwise.
PILLAR_EXPORT void prtPushVse(PrtVseType type, PrtVseHandle value)
{
    Prt_Task *pt = prt_GetTask();
    pt->pushVse(type, value);
} // prtPushVse


// Pops the top entry off the Pillar virtual stack iff the key of that entry matches the specified key.
// Returns True if successful, False otherwise.
PILLAR_EXPORT PrtVseHandle prtPopVse(void)
{
    Prt_Task *pt = prt_GetTask();
    return pt->popVse();
} // prtPopVse


// Pushes "value" to the top of the Pillar virtual stack. Returns True if successful, False otherwise.
PILLAR_EXPORT void prtPushVseForTask(PrtTaskHandle task, PrtVseType type, PrtVseHandle value)
{
    Prt_Task *pt = (Prt_Task*)task;
    pt->pushVse(type, value);
} // prtPushVseForTask


// Pops the top entry off the Pillar virtual stack iff the key of that entry matches the specified key.
// Returns True if successful, False otherwise.
PILLAR_EXPORT PrtVseHandle prtPopVseForTask(PrtTaskHandle task)
{
    Prt_Task *pt = (Prt_Task*)task;
    return pt->popVse();
} // prtPopVseForTask


// Gets the stack entry type for the given virutal stack entry.
PILLAR_EXPORT PrtVseType prtGetVseType(PrtVseHandle vse)
{
    return (PrtVseType)(((Prt_Vse *)vse)->entryTypeCode);
} // prtGetVseType


// Returns the next VSE farther down the stack.
PILLAR_EXPORT PrtVseHandle prtGetNextVse(PrtVseHandle vse)
{
    return vse ? (PrtVseHandle)((Prt_Vse *)vse)->nextFrame : NULL;
} // prtGetNextVse


// To do a fat cut, we compare the VSH with the target continuation's vsh field.
// If they are the same, we do a thin cut to the target continuation.
// If they are different, we do a thin cut to the VSE at the top of the virtual stack.
// (The VSE fields are deliberately laid out to match a 1-argument fat continuation.)
// That VSE/continuation takes the original target continuation k as its argument,
// and it is expected to complete its destructor operation and then recut to the
// original target continuation.
// The VSE is kept on the virtual stack (without popping it) for as long as possible
// (i.e., until the recut) so that the original target continuation's arguments can
// be enumerated if a GC happens while the fat cut executes.
// If the VSE's target continuation argument is non-null, it indicates that the
// destructor is executing; the fat cut ignores any such VSEs to avoid infinite
// destructor loops.
PILLAR_EXPORT void __pdecl prtFatCutTo(struct PrtContinuation *k)
{
    // vsh and vse always point to the same place
    PrtVseHandle vsh = prtGetVsh();
    Prt_Vse *vse = (Prt_Vse *) vsh;
    // Pop all elements whose mTargetContinuation is non-NULL.
    while (vse->targetContinuation != NULL) {
        assert(k->vsh != vsh);
        prtPopVse();
        vse = (Prt_Vse *) (vsh = prtGetVsh()); // keep vse and vsh in synch
    }
    if (k->vsh == vsh) {
        prtThinCutTo(k);
    } else {
        vse->targetContinuation = k;
        prtThinCutTo((PrtContinuation *)vsh);
    }
} //prtFatCutTo


static void printValidationFailure(PrtVseHandle vsh, PrtVseHandle expectedVse, PrtVseType expectedType)
{
    printf("VSE validation failure: vsh=0x%p, expectedVse=0x%p, expectedType=0x%p\n", vsh, expectedVse, expectedType);
} //printValidationFailure


// This is a convenience function meant to be called from managed code in prtstubs.asm.
extern "C" void prt_ValidateVsh(PrtVseHandle expectedVse, PrtVseType expectedType)
{
    PrtVseHandle vsh = prtGetVsh();
    Prt_Vse *vse = (Prt_Vse *)vsh;
    if ((vsh == NULL) || (vsh != expectedVse) || (vse->entryTypeCode != expectedType)) {
        PrtRegister args[3];
        args[0] = (PrtRegister)vsh;
        args[1] = (PrtRegister)expectedVse;
        args[2] = (PrtRegister)expectedType;
        prtInvokeUnmanagedFunc((PrtCodeAddress)&printValidationFailure, args, 3, PrtCcCdecl);
    }
} //prt_ValidateVsh
