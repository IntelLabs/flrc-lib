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

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/base/prtcodeinfo.cpp,v 1.7 2012/04/02 23:19:29 taanders Exp $

/*
 * Pillar code information manager functions. The Prt_Cim for a code generator (static compiler, JIT, stub generator)
 * supports operations on its methods and other code. This includes root enumeration, stack walking, and whether the code generator
 * is responsible for a specified code address.
 */

#include "prt/prtcodegenerator.h"

#include "prt/prtcodeinfo.h"
#include "prt/prtcodeinfointernal.h"
#include "prt/prtvse.h"
#include "prt/prttls.h"
#include "prt/prtglobals.h"
#include "prt/prtcims.h"
#include <stdlib.h>

// Registers a set of callback functions for a component that is responsible for understanding one or more regions of code
// for stackwalking, exceptions, root set enumeration, etc.
PILLAR_EXPORT PrtCodeInfoManager prtRegisterCodeInfoManager(const char *theCimName, PrtCodeInfoManagerFunctions theCimFns)
{
    Prt_Cim *newCimMgr = new Prt_Cim(theCimName, theCimFns);
    return (PrtCodeInfoManager)newCimMgr;
} //prtRegisterCodeInfoManager


// Indicates that the given Code Info Manager is responsible for the closed interval of code
// addresses as specified by start and end.
PILLAR_EXPORT void prtAddCodeRegion(PrtCodeInfoManager     theCim,
                                    PrtCodeAddress         start,
                                    PrtCodeAddress         end,
                                    PrtCimSpecificDataType opaqueData)
{
    assert(theCim);
    Prt_Cim *cimMgr = (Prt_Cim *)theCim;
    prt_GetGlobals()->codeInfoManagers->insert(cimMgr, start, end, opaqueData);
} //prtAddCodeRegion


// Returns a pointer to the code info manager for the current activation described by si, or NULL if none was found.
// If the code info manager was found and the pointer opaqueData is not NULL, then *opaqueData will be set to the
// opaque data associated with this stack iterator.
Prt_Cim *prt_LookupCodeInfoManager(PrtCodeAddress eip,
                                   PrtBool isIpPast,
                                   PrtCimSpecificDataType *opaqueData)
{
    Prt_Cim *cim;
    if (isIpPast)
        eip --;
    bool success = prt_GetGlobals()->codeInfoManagers->find(eip, &cim, opaqueData);
    if (!success) {
        return NULL;
    }
    return cim;
} //prt_LookupCodeInfoManager


void prt_ErrorNoCim(PrtStackIterator *si, const char *message)
{
#ifdef __x86_64__
    printf("%s: no CodeInfoManager for rip=0x%p, rsp=0x%p, vsh=0x%p\n",
        message, (si->ripPtr == NULL ? 0 : *si->ripPtr), si->rsp, si->vsh);
    if (si->originalVsh) {
        printf("Stack trace leading to this frame:\n");
        PrtStackIterator _si = *si;
        prt_unwindToLatestM2u(&_si, (Prt_Vse *)_si.originalVsh);
        _si.originalVsh = NULL; // prevent an infinite recursive loop
        while (_si.ripPtr != si->ripPtr && !prtIsActivationPastEnd(&_si)) {
            char buf[1000];
            prtGetActivationString(&_si, buf, sizeof(buf));
            printf("  %s\n", buf);
            prtNextActivation(&_si);
        }
    }
#else  // __x86_64__
    printf("%s: no CodeInfoManager for eip=0x%p, esp=0x%p, vsh=0x%p\n",
        message, (si->eipPtr == NULL ? 0 : *si->eipPtr), si->esp, si->vsh);
    if (si->originalVsh) {
        printf("Stack trace leading to this frame:\n");
        PrtStackIterator _si = *si;
        prt_unwindToLatestM2u(&_si, (Prt_Vse *)_si.originalVsh);
        _si.originalVsh = NULL; // prevent an infinite recursive loop
        while (_si.eipPtr != si->eipPtr && !prtIsActivationPastEnd(&_si)) {
            char buf[1000];
            prtGetActivationString(&_si, buf, sizeof(buf));
            printf("  %s\n", buf);
            prtNextActivation(&_si);
        }
    }
#endif // __x86_64__
    assert(0);
    exit(-2);
} //prt_ErrorNoCim


Prt_CodeIntervalTree *prt_CreateCodeIntervalTree(void)
{
    return new Prt_CodeIntervalTree;
} //prt_CreateCodeIntervalTree


void prt_DeleteCodeIntervalTree(Prt_CodeIntervalTree *cit)
{
    delete cit;
} //Prt_CodeIntervalTree


// Perform a lookup in the Prt_CodeIntervalTree.  Find the interval node that includes the specified IP.
// If such a node is found, set *cim to the corresponding Prt_Cim and return true; otherwise return false.
bool Prt_CodeIntervalTree::find(PrtCodeAddress ip, Prt_Cim **cim, PrtCimSpecificDataType *opaqueData) const
{
    Prt_CodeInfoTreeMap::const_iterator iter = ranges.find(Prt_IpRange(ip, ip));
    if (iter == ranges.end())
        return false;
    Prt_CimDataPair cimAndData = iter->second;
    *cim = cimAndData.first;
    if (opaqueData)
        *opaqueData = cimAndData.second;
    return true;
} //Prt_CodeIntervalTree::find


// Insert a code interval into the tree and remember that it is managed by the specified Prt_Cim.
// Return true if successful.  Return false if the some portion of the specified code range is already in the tree.
void Prt_CodeIntervalTree::insert(Prt_Cim *theCim,
                                  PrtCodeAddress start,
                                  PrtCodeAddress end,
							      PrtCimSpecificDataType opaqueData)
{
    prt_SuspendWrapper lock();

    Prt_IpRange r(start, end);
    Prt_CodeInfoTreeMap::const_iterator iter = ranges.find(r);
    if (iter != ranges.end()) {
        printf("Prt_CodeIntervalTree::insert: region [0x%p..0x%p] overlaps\n"
            "with existing region [0x%p..0x%p]\n", start, end, iter->first.start, iter->first.end);
        assert(0);
        return;
    }
    ranges.insert(pair<Prt_IpRange, Prt_CimDataPair>(r, Prt_CimDataPair(theCim, opaqueData)));
} //Prt_CodeIntervalTree::insert


// Remove a code interval from the tree.  The CIM, start, and end parameters must be
// an exact match, otherwise the tree is unchanged and false is returned.  On success,
// true is returned.
bool Prt_CodeIntervalTree::remove(Prt_Cim *theCim, PrtCodeAddress start, PrtCodeAddress end)
{
    prt_SuspendWrapper lock();
    Prt_IpRange r(start, end);
    Prt_CodeInfoTreeMap::iterator iter = ranges.find(r);
    if (iter == ranges.end())
        return false; // the range wasn't there
    if (iter->first.start != start || iter->first.end != end)
        return false; // the range wasn't an exact match
    if (iter->second.first != theCim)
        return false; // wrong code info manager
    ranges.erase(r);
    return true;
} //Prt_CodeIntervalTree::remove
