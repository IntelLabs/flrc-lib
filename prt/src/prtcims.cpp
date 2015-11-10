/*
 * COPYRIGHT_NOTICE_1
 */

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/base/prtcims.cpp,v 1.7 2012/01/17 21:09:12 taanders Exp $
//
// This file implements the built-in code info managers.

#include "prt/prtcodegenerator.h"

#include "prt/prtvse.h"
#include "prt/prtcims.h"
#include "prt/prtcodeinfo.h"
#include "prt/prttls.h"
#include "prt/prtglobals.h"

PrtVseType PRT_VSE_M2U               = (PrtVseType) &prt_InvokeUnmanagedFuncDestructor;
PrtVseType PRT_VSE_PCALL             = (PrtVseType) &prt_PcallDestructor;

extern "C" void prt_PrintIllegalCutMessage(void)
{
    // Use prtGetVsh() to get the VSE responsible for calling this routine.
    printf("prt_IllegalCut: attempt to cut past initial frame of task.\n");
} //prt_PrintIllegalCutMessage


// Unwind from the current stub frame by updating "si" to describe the previous frame.
static void standardUnwind(PrtStackIterator *si, Prt_Vse *vse, Prt_SmallVseProlog *vseProlog, PrtCodeAddress *eip, unsigned frameSize)
{
    // Set esp using the stub's frame size and and the address of the stub's VSE, which is stored on the stack just after the
    // frame (at lower addresses).
    // Note: for setting esp, this code assumes that the VSE is the last (lowest address) field in the stub's
    // stack frame, and therefore the esp value before unwinding is equal to the VSE pointer.
#ifdef __x86_64__
    prtSetStackIteratorFields(si,
                              /*eipPtr*/ eip,
                              /*esp*/    ((PrtRegister)vse + frameSize),
                              /*ebxPtr*/ &vseProlog->rbx,
                              /*ebpPtr*/ &vseProlog->rbp,
                              /*r12Ptr*/ &vseProlog->r12,
                              /*r13Ptr*/ &vseProlog->r13,
                              /*r14Ptr*/ &vseProlog->r14,
                              /*r15Ptr*/ &vseProlog->r15,
                              /*vsh*/    (PrtVseHandle)vse->nextFrame,
                              /*virtualFrameNumber*/ 0);
#else  // __x86_64__
    prtSetStackIteratorFields(si,
                              /*eipPtr*/ eip,
                              /*esp*/    ((PrtRegister)vse + frameSize),
                              /*ebxPtr*/ &vseProlog->ebx,
                              /*ebpPtr*/ &vseProlog->ebp,
                              /*esiPtr*/ &vseProlog->esi,
                              /*ediPtr*/ &vseProlog->edi,
                              /*vsh*/    (PrtVseHandle)vse->nextFrame,
                              /*virtualFrameNumber*/ 0);
#endif // __x86_64__
} //standardUnwind


extern "C" void prtPillarCompilerUnwinder(PrtStackIterator *si, Prt_Vse *lvse) {
    Prt_M2uFrame *psf = (Prt_M2uFrame *)lvse;

    // Note: the callee-save register values are incorrect, but they will be fixed after the M2U unwind.
#ifdef __x86_64__
    prtSetStackIteratorFields(si,
                              NULL,
                              /*esp*/    (PrtRegister)lvse,
                              /*rbxPtr*/ si->rbxPtr,
                              /*rbpPtr*/ si->rbpPtr,
                              /*r12Ptr*/ si->r12Ptr,
                              /*r13Ptr*/ si->r13Ptr,
                              /*r14Ptr*/ si->r14Ptr,
                              /*r15Ptr*/ si->r15Ptr,
                              /*vsh*/    (PrtVseHandle)lvse,
                              /*virtualFrameNumber*/ 0);
#else  // __x86_64__
    prtSetStackIteratorFields(si,
                              NULL,
                              /*esp*/    (PrtRegister)lvse,
                              /*ebxPtr*/ si->ebxPtr,
                              /*ebpPtr*/ si->ebpPtr,
                              /*esiPtr*/ si->esiPtr,
                              /*ediPtr*/ si->ediPtr,
                              /*vsh*/    (PrtVseHandle)lvse,
                              /*virtualFrameNumber*/ 0);
#endif // __x86_64__

    standardUnwind(/*context*/   si,
                   /*vse*/       psf,
                   /*vseProlog*/ &psf->prolog.calleeSaves,
                   /*eip*/       &psf->prolog.eip,
                   /*framesize*/ sizeof(Prt_M2uFrame));
}

// =================================================================================
// U2M CodeInfoManager
// =================================================================================

// This function is called to initiate a stack walk from unmanaged code,
// and also to skip over unmanaged frames in the U2M unwinder.
void prt_unwindToLatestM2u(PrtStackIterator *si, Prt_Vse *initialVsh)
{
    assert(si);
    if (initialVsh == NULL) {
#ifdef __x86_64__
        si->ripPtr = NULL;
#else  // __x86_64__
        si->eipPtr = NULL;
#endif // __x86_64__
        return;
    }

    // Search for the newest M2U VSE on the virtual stack.  Skip over any non-system VSEs that might have
    // been pushed by the unmanaged code.  No system VSEs should be encountered during the search.
    Prt_Vse *lvse = initialVsh;
    while ((lvse != NULL) && (lvse->entryTypeCode != PRT_VSE_M2U)) {
        PrtVseType t = lvse->entryTypeCode;
        assert(
            t != PRT_VSE_PCALL &&
            true);
        lvse = lvse->nextFrame;
    }
    assert(lvse);
    if (lvse == NULL) {
        // This should not happen, but let's not crash if it does.
#ifdef __x86_64__
        si->ripPtr = NULL;
#else  // __x86_64__
        si->eipPtr = NULL;
#endif // __x86_64__
        return;
    }

    Prt_Task *pt = (Prt_Task*)si->task_for_this_stack;
    // lvse now points to the newest M2U VSE, and so the newest M2U frame.
    Prt_M2uFrame *psf = (Prt_M2uFrame *)lvse;

    psf->realM2uUnwinder(si, lvse);

#if 0
    if(psf->realM2uUnwinder != (PrtCodeAddress)prtPillarCompilerUnwinder) {
        printf("Real M2U unwinder is not prtPillarCompilerUnwinder.\n");
    }

    // Note: the callee-save register values are incorrect, but they will be fixed after the M2U unwind.
#ifdef __x86_64__
    prtSetStackIteratorFields(si,
                              NULL,
                              /*esp*/    (PrtRegister)lvse,
                              /*rbxPtr*/ si->rbxPtr,
                              /*rbpPtr*/ si->rbpPtr,
                              /*r12Ptr*/ si->r12Ptr,
                              /*r13Ptr*/ si->r13Ptr,
                              /*r14Ptr*/ si->r14Ptr,
                              /*r15Ptr*/ si->r15Ptr,
                              /*vsh*/    (PrtVseHandle)lvse,
                              /*virtualFrameNumber*/ 0);
#else  // __x86_64__
    prtSetStackIteratorFields(si,
                              NULL,
                              /*esp*/    (PrtRegister)lvse,
                              /*ebxPtr*/ si->ebxPtr,
                              /*ebpPtr*/ si->ebpPtr,
                              /*esiPtr*/ si->esiPtr,
                              /*ediPtr*/ si->ediPtr,
                              /*vsh*/    (PrtVseHandle)lvse,
                              /*virtualFrameNumber*/ 0);
#endif // __x86_64__

    standardUnwind(/*context*/   si,
                   /*vse*/       psf,
                   /*vseProlog*/ &psf->prolog.calleeSaves,
                   /*eip*/       &psf->prolog.eip,
                   /*framesize*/ sizeof(Prt_M2uFrame));
#endif // 0

#ifdef __x86_64__
	// Update watermark if necessary.
    if (si->ripPtr &&
        *(si->ripPtr) >= pt->mWatermarkStubs &&
        *(si->ripPtr) <= pt->mWatermarkStubEnd) {
        si->watermarkPtr = si->ripPtr;
        si->ripPtr = pt->get_real_eip_ptr(*(si->watermarkPtr));
    } else {
        si->watermarkPtr = NULL; // not a special watermark frame
    }
#else  // __x86_64__
	// Update watermark if necessary.
    if (si->eipPtr &&
        *(si->eipPtr) >= pt->mWatermarkStubs &&
        *(si->eipPtr) <= pt->mWatermarkStubEnd) {
        si->watermarkPtr = si->eipPtr;
        si->eipPtr = pt->get_real_eip_ptr(*(si->watermarkPtr));
    } else {
        si->watermarkPtr = NULL; // not a special watermark frame
    }
#endif // __x86_64__
} //prt_unwindToLatestM2u


static void u2mGetPreviousFrame(PRT_INOUT PrtStackIterator *si, PRT_INOUT PrtCimSpecificDataType opaqueData)
{
    prt_unwindToLatestM2u(si, (Prt_Vse *)si->vsh);
} //u2mGetPreviousFrame


#define _snprintf(x,y,...) sprintf(x, __VA_ARGS__ )

static char *u2mGetStringForFrame(PrtStackIterator *si, char *buffer, size_t bufferSize, PrtCimSpecificDataType opaqueData)
{
    assert(si);
#ifdef __x86_64__
    assert(si->ripPtr);
    _snprintf(buffer, bufferSize, "U2M frame: ip=%p, rsp=%p, vsh=%p", *si->ripPtr, si->rsp, si->vsh);
#else  // __x86_64__
    assert(si->eipPtr);
    _snprintf(buffer, bufferSize, "U2M frame: ip=%p, esp=%p, vsh=%p", *si->eipPtr, si->esp, si->vsh);
#endif // __x86_64__
    return buffer;
} //u2mGetStringForFrame


static PrtDataPointer u2mGetSpanDescriptor(PrtStackIterator *si, unsigned key, PrtCimSpecificDataType opaqueData)
{
    if (key != PRT_SPAN_KEY_U2M)
        return NULL;
#ifdef __x86_64__
    PrtContinuation *cont = (PrtContinuation *) si->rsp;
    cont->eip = (PrtCodeAddress) &prt_InvokeManagedFuncUnwindContinuation;
    cont->vsh = si->vsh;
#else  // __x86_64__
    PrtContinuation *cont = (PrtContinuation *) si->esp;
    cont->eip = (PrtCodeAddress) &prt_InvokeManagedFuncUnwindContinuation;
    cont->vsh = si->vsh;
#endif // __x86_64__
    return (PrtDataPointer) cont;
} //u2mGetSpanDescriptor


static void registerU2mCim(void)
{
    static struct PrtCodeInfoManagerFunctions u2mFuncs = {
        u2mGetStringForFrame, u2mGetPreviousFrame, 0, 0, 0, u2mGetSpanDescriptor
    };

    PrtCodeInfoManager u2mCim = prtRegisterCodeInfoManager("U2M CodeInfoManager", u2mFuncs);
    prtAddCodeRegion(u2mCim, (PrtCodeAddress)&prt_InvokeManagedFuncStart, (PrtCodeAddress)&prt_InvokeManagedFuncEnd - 1, NULL);
} //registerU2mCim


// =================================================================================
// M2U CodeInfoManager
// =================================================================================

static void m2uGetPreviousFrame(PrtStackIterator *si, PrtCimSpecificDataType opaqueData)
{
    // Note: do not rely on the values of the callee-save registers coming into
    // this unwinder.  They are probably incorrect since there are no unwinders
    // for unmanaged frames.
    Prt_M2uFrame *psf = (Prt_M2uFrame *)si->vsh;
    assert(psf);
    assert(psf->entryTypeCode == PRT_VSE_M2U);
    standardUnwind(/*context*/   si,
                   /*vse*/       psf,
                   /*vseProlog*/ &psf->prolog.calleeSaves,
                   /*eip*/       &psf->prolog.eip,
                   /*framesize*/ sizeof(Prt_M2uFrame));
} // m2uGetPreviousFrame


static char *m2uGetStringForFrame(PrtStackIterator *si, char *buffer, size_t bufferSize, PrtCimSpecificDataType opaqueData)
{
    assert(si);
#ifdef __x86_64__
    assert(si->ripPtr);
    _snprintf(buffer, bufferSize, "M2U frame: ip=%p, rsp=%p, vsh=%p", *si->ripPtr, si->rsp, si->vsh);
#else  // __x86_64__
    assert(si->eipPtr);
    _snprintf(buffer, bufferSize, "M2U frame: ip=%p, esp=%p, vsh=%p", *si->eipPtr, si->esp, si->vsh);
#endif // __x86_64__
    return buffer;
} //m2uGetStringForFrame


static PrtDataPointer m2uGetSpanDescriptor(PrtStackIterator *si, unsigned key, PrtCimSpecificDataType opaqueData)
{
    if (key != PRT_SPAN_KEY_M2U)
        return NULL;
    Prt_M2uFrame *psf = (Prt_M2uFrame *)si->vsh;
    assert(psf);
    assert(psf->entryTypeCode == PRT_VSE_M2U);
    PrtContinuation *cont = (PrtContinuation *) &psf->unwindCont;
    cont->eip = (PrtCodeAddress) &prt_InvokeUnmanagedFuncUnwindContinuation;
    cont->vsh = si->vsh;
    return (PrtDataPointer) cont;
} //m2uGetSpanDescriptor


static void registerM2uCim(void)
{
    static struct PrtCodeInfoManagerFunctions m2uFuncs = {
        m2uGetStringForFrame, m2uGetPreviousFrame, 0, 0, 0, m2uGetSpanDescriptor
    };

    PrtCodeInfoManager m2uCim = prtRegisterCodeInfoManager("M2U CodeInfoManager", m2uFuncs);
    prtAddCodeRegion(m2uCim, (PrtCodeAddress)&prt_InvokeUnmanagedFuncStart, (PrtCodeAddress)&prt_InvokeUnmanagedFuncEnd - 1, NULL);

    struct PrtEipUnwinder descriptor;
#ifdef __x86_64__
    descriptor.rspAdjustment.base = PrtSiRegisterBaseVsh;
    descriptor.rspAdjustment.adjustment = sizeof(Prt_M2uFrame);
    descriptor.ripAdjustment.base = PrtSiRegisterBaseVsh;
    descriptor.ripAdjustment.adjustment = offsetof(Prt_M2uFrame,prolog.eip);
    descriptor.rbpAdjustment.base = PrtSiRegisterBaseVsh;
    descriptor.rbpAdjustment.adjustment = offsetof(Prt_M2uFrame,prolog.calleeSaves.rbp);
    descriptor.rbxAdjustment.base = PrtSiRegisterBaseVsh;
    descriptor.rbxAdjustment.adjustment = offsetof(Prt_M2uFrame,prolog.calleeSaves.rbx);
    descriptor.r12Adjustment.base = PrtSiRegisterBaseVsh;
    descriptor.r12Adjustment.adjustment = offsetof(Prt_M2uFrame,prolog.calleeSaves.r12);
    descriptor.r13Adjustment.base = PrtSiRegisterBaseVsh;
    descriptor.r13Adjustment.adjustment = offsetof(Prt_M2uFrame,prolog.calleeSaves.r13);
    descriptor.r14Adjustment.base = PrtSiRegisterBaseVsh;
    descriptor.r14Adjustment.adjustment = offsetof(Prt_M2uFrame,prolog.calleeSaves.r14);
    descriptor.r15Adjustment.base = PrtSiRegisterBaseVsh;
    descriptor.r15Adjustment.adjustment = offsetof(Prt_M2uFrame,prolog.calleeSaves.r15);
    descriptor.vshFramesToPop = 1;
#else  // __x86_64__
    descriptor.espAdjustment.base = PrtSiRegisterBaseVsh;
    descriptor.espAdjustment.adjustment = sizeof(Prt_M2uFrame);
    descriptor.eipAdjustment.base = PrtSiRegisterBaseVsh;
    descriptor.eipAdjustment.adjustment = offsetof(Prt_M2uFrame,prolog.eip);
    descriptor.ebpAdjustment.base = PrtSiRegisterBaseVsh;
    descriptor.ebpAdjustment.adjustment = offsetof(Prt_M2uFrame,prolog.calleeSaves.ebp);
    descriptor.ebxAdjustment.base = PrtSiRegisterBaseVsh;
    descriptor.ebxAdjustment.adjustment = offsetof(Prt_M2uFrame,prolog.calleeSaves.ebx);
    descriptor.esiAdjustment.base = PrtSiRegisterBaseVsh;
    descriptor.esiAdjustment.adjustment = offsetof(Prt_M2uFrame,prolog.calleeSaves.esi);
    descriptor.ediAdjustment.base = PrtSiRegisterBaseVsh;
    descriptor.ediAdjustment.adjustment = offsetof(Prt_M2uFrame,prolog.calleeSaves.edi);
    descriptor.vshFramesToPop = 1;
#endif // __x86_64__
    prtAddEipUnwinder((PrtCodeAddress)&prt_InvokeUnmanagedFuncPostCall, &descriptor);
} //registerM2uCim


// =================================================================================
// prtYield CodeInfoManager
// =================================================================================

static void yieldGetPreviousFrame(PrtStackIterator *si, PrtCimSpecificDataType opaqueData)
{
    assert(si);
#ifdef __x86_64__
    Prt_DefaultVseProlog *defVse = (Prt_DefaultVseProlog *)si->rsp;
    prtSetStackIteratorFields(si,
                              /*eipPtr*/ &defVse->eip,
                              /*esp*/    ((PrtRegister)si->rsp + sizeof(Prt_DefaultVseProlog)),
                              /*ebxPtr*/ &defVse->calleeSaves.rbx,
                              /*ebpPtr*/ &defVse->calleeSaves.rbp,
                              /*r12Ptr*/ &defVse->calleeSaves.r12,
                              /*r13Ptr*/ &defVse->calleeSaves.r13,
                              /*r14Ptr*/ &defVse->calleeSaves.r14,
                              /*r15Ptr*/ &defVse->calleeSaves.r15,
                              /*vsh*/    si->vsh,
                              /*virtualFrameNumber*/ 0);
#else  // __x86_64__
    Prt_DefaultVseProlog *defVse = (Prt_DefaultVseProlog *)si->esp;
    prtSetStackIteratorFields(si,
                              /*eipPtr*/ &defVse->eip,
                              /*esp*/    ((PrtRegister)si->esp + sizeof(Prt_DefaultVseProlog)),
                              /*ebxPtr*/ &defVse->calleeSaves.ebx,
                              /*ebpPtr*/ &defVse->calleeSaves.ebp,
                              /*esiPtr*/ &defVse->calleeSaves.esi,
                              /*ediPtr*/ &defVse->calleeSaves.edi,
                              /*vsh*/    si->vsh,
                              /*virtualFrameNumber*/ 0);
#endif // __x86_64__
} //yieldGetPreviousFrame


static char *yieldGetStringForFrame(PrtStackIterator *si, char *buffer, size_t bufferSize, PrtCimSpecificDataType opaqueData)
{
    assert(si);
#ifdef __x86_64__
    assert(si->ripPtr);
    _snprintf(buffer, bufferSize, "prtYield frame: ip=%p, rsp=%p, vsh=%p", *si->ripPtr, si->rsp, si->vsh);
#else  // __x86_64__
    assert(si->eipPtr);
    _snprintf(buffer, bufferSize, "prtYield frame: ip=%p, esp=%p, vsh=%p", *si->eipPtr, si->esp, si->vsh);
#endif // __x86_64__
    return buffer;
} //yieldGetStringForFrame


static void registerYieldCim(void)
{
    static struct PrtCodeInfoManagerFunctions yieldFuncs = {
        yieldGetStringForFrame, yieldGetPreviousFrame
    };

    PrtCodeInfoManager yieldCim = prtRegisterCodeInfoManager("prtYield CodeInfoManager", yieldFuncs);
    prtAddCodeRegion(yieldCim, (PrtCodeAddress)&prt_YieldStart, (PrtCodeAddress)&prt_YieldEnd - 1, NULL);
} //registerYieldCim


// =================================================================================
// Support for registering all built-in CodeInfoManagers
// =================================================================================

void prt_registerBuiltinCodeInfoManagers(void)
{
    registerU2mCim();
    registerM2uCim();
    registerYieldCim();
} //prt_registerBuiltinCodeInfoManagers
