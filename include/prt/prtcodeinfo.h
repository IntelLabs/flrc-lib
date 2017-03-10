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

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/include/prtcodeinfo.h,v 1.3 2011/03/09 19:09:53 taanders Exp $

/*
 * Pillar code information manager functions. The Prt_Cim for a code generator (static compiler, JIT, stub generator)
 * supports operations on its methods and other code. This includes root enumeration, stack walking, and whether the code generator
 * is responsible for a specified code address.
 */

#ifndef _PRT_CODE_INFO_H_
#define _PRT_CODE_INFO_H_

#include <assert.h>
#include <stdio.h>

#ifndef _WINDOWS
#define _snprintf(x,y,...) sprintf(x, __VA_ARGS__ )
#endif // !_WINDOWS

// Cim = code info manager
// The Prt_Cim for a code generator (static compiler, JIT, stub generator) supports operations on its methods and other code. 
// This includes root enumeration, stack walking, and whether the code generator is responsible for a specified code address.
class Prt_Cim
{
public:
    // Initializes a new Prt_Cim.
    Prt_Cim(PRT_IN const char *theCimName, 
            PRT_IN const PrtCodeInfoManagerFunctions &theCimFns) : cimName(theCimName), cimFunctions(theCimFns)
    {
        assert(theCimName);
        assert(theCimFns.cimGetPreviousFrame);
    } //Prt_Cim::Prt_Cim

    // Update the stack iterator to contain information about the previous frame on the stack.
    // opaqueData is a conduit for opaqueData registered by the Cim and sent back to it for this frame.
    void getPreviousFrame(PRT_INOUT PrtStackIterator *si, 
                          PRT_IN    PrtCimSpecificDataType opaqueData) const
    {
        assert(cimFunctions.cimGetPreviousFrame);
        cimFunctions.cimGetPreviousFrame(si, opaqueData);
    } //Prt_Cim::getPreviousFrame

    // Enumerate the roots from the current stack frame as described in the stack iterator.  Invoke rootCallback
    // once for each identified root.
    void enumerateRoots(PRT_IN PrtStackIterator *si, 
                        PRT_IN struct PrtRseInfo *rootSetInfo, 
                        PRT_IN PrtCimSpecificDataType opaqueData) const
    {
        if (cimFunctions.cimEnumerateRoots) {
            cimFunctions.cimEnumerateRoots(si, rootSetInfo, opaqueData);
        }
    } //Prt_Cim::enumerateRoots

    // Enumerates the roots from the given continuation.  Invokes rootCallback once for each root.
    void enumerateContinuationRoots(PRT_IN PrtContinuation *continuation, 
                                    PRT_IN struct PrtRseInfo *rootSetInfo, 
                                    PRT_IN PrtCimSpecificDataType opaqueData) const
    {
        if (cimFunctions.cimEnumerateClosureRoots) {
            cimFunctions.cimEnumerateClosureRoots(continuation->eip, &(continuation->args), rootSetInfo, opaqueData);
        }
    } //Prt_Cim::enumerateContinuationRoots

    void enumerateClosureRoots(PRT_IN PrtCodeAddress closureEip, 
                               PRT_IN void *closureArgs,
                               PRT_IN struct PrtRseInfo *rootSetInfo, 
                               PRT_IN PrtCimSpecificDataType opaqueData) const
    {
        if (cimFunctions.cimEnumerateClosureRoots) {
            cimFunctions.cimEnumerateClosureRoots(closureEip, closureArgs, rootSetInfo, opaqueData);
        }
    } //Prt_Cim::enumerateClosureRoots


    // Returns a textual description of the current stack frame as described in the stack iterator.
    char *getStringForFrame(PRT_IN  PrtStackIterator *si, 
                            PRT_OUT char *buffer, 
                            PRT_IN  size_t bufferSize, 
                            PRT_IN  PrtCimSpecificDataType opaqueData) const
    {
        if (cimFunctions.cimGetStringForFrame == NULL) {
            // Return a generic string.
            PrtCodeAddress ip = prtGetActivationIP(si);
            _snprintf(buffer, bufferSize, "Pillar %s frame: ip=%p", cimName, ip);
            return buffer;
        }
        return cimFunctions.cimGetStringForFrame(si, buffer, bufferSize, opaqueData);
    } //Prt_Cim::getStringForFrame

    struct PrtContinuation *getUnwindContinuation(PRT_IN struct PrtStackIterator *si, 
                                                  PRT_IN unsigned continuationIndex, 
                                                  PRT_IN PrtCimSpecificDataType opaqueData) const
    {
        assert(cimFunctions.cimGetUnwindContinuation);
        return cimFunctions.cimGetUnwindContinuation(si, continuationIndex, opaqueData);
    } //Prt_Cim::getUnwindContinuation

	PrtDataPointer getSpanDescriptor(PRT_IN struct PrtStackIterator *si, 
                                     PRT_IN unsigned key, 
                                     PRT_IN PrtCimSpecificDataType opaqueData) const
	{
        if (cimFunctions.cimGetSpanDescriptor == NULL) {
            return NULL;
        }
        return cimFunctions.cimGetSpanDescriptor(si, key, opaqueData);
	} //Prt_Cim::getSpanDescriptor

    const char *getCimName(void) const
    {
        return cimName;
    } //Prt_Cim::getCimName

    // Return the PrtCodeInfoManagerFunctions for the given input Prt_Cim.
    PrtCodeInfoManagerFunctions getCimFunctions(void) const
    {
        return cimFunctions;
    } //Prt_Cim::getCimFunctions

private:
    // shouldn't ever copy these...can't hurt but there is no forseen reason to have two copies
    Prt_Cim(const Prt_Cim &) : cimFunctions(nullCimFunctions)  { assert(0); }
    Prt_Cim &operator=(const Prt_Cim &) { assert(0); return *this; }

private:
    static PrtCodeInfoManagerFunctions  nullCimFunctions;
    const char                         *cimName;
    PrtCodeInfoManagerFunctions         cimFunctions;
}; //class Prt_Cim


// Returns a pointer to the code info manager for eip, or NULL if none was found.
// The isIpPast describes whether eip points just past the instruction in question,
// or is the precise eip.  In general, if eip is derived from a PrtStackIterator, then
// isIpPast should be PrtTrue, otherwise PrtFalse (e.g., the eip from a PrtContinuation).
// If the code info manager was found and the pointer opaqueData is not NULL, then *opaqueData will be set to the 
// opaque data associated with this stack iterator.
Prt_Cim *prt_LookupCodeInfoManager(PrtCodeAddress eip, PrtBool isIpPast, PrtCimSpecificDataType *opaqueData);

// Prints an error message and asserts when no code info manager is found for a stack iterator.
void prt_ErrorNoCim(PrtStackIterator *si, const char *message);


class Prt_CodeIntervalTree *prt_CreateCodeIntervalTree(void);

void prt_DeleteCodeIntervalTree(class Prt_CodeIntervalTree *);

#endif // _PRT_CODE_INFO_H_
