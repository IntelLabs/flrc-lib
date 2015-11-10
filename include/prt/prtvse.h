/*
 * COPYRIGHT_NOTICE_1
 */

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/include/prtvse.h,v 1.2 2011/03/09 19:09:53 taanders Exp $

#ifndef _PRTVSE_H
#define _PRTVSE_H

// Stubs rely on this VSE format so don't change this without also changing all your stubs!
// Also, the definition of PILLAR_VSE_SIZE in prt.h depends on this.
struct Prt_Vse
{
    // Lay out fields to look like a 1-argument PrtContinuation.
    PrtVseType       entryTypeCode;
    struct Prt_Vse  *nextFrame;
    PrtContinuation *targetContinuation;
}; // struct Prt_Vse



#endif // !_PRTVSE_H
