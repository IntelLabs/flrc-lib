/*
 * COPYRIGHT_NOTICE_1
 */

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/include/prtcodeinfointernal.h,v 1.3 2011/06/23 17:58:52 taanders Exp $

// This header file exists only to isolate the C++ template code used by Pillar's code information manager functionality
// from the rest of Pillar's code. This, in turn, avoids a fluury of Visual Studio 6.0 warnings about the overly long 
// identifiers that result from the use of those templates.

#ifndef _PRTCODEINFOINTERNAL_H_
#define _PRTCODEINFOINTERNAL_H_

#include <pthread.h>

#include <map>
using namespace std;

struct Prt_IpRange {
    Prt_IpRange(PRT_IN PrtCodeAddress s, 
                PRT_IN PrtCodeAddress e) : start(s), end(e) { assert(s <= e); }

    // The "<" operator returns true if the left range is entirely below the right range,
    // with no overlap.  If ranges A and B overlap, then (A<B)==false && (B<A)==false,
    // therefore the STL map judges A and B to be equal.  This is how we detect attempts
    // to insert overlapping code regions into the code interval tree.
    bool operator < (PRT_IN const Prt_IpRange &b) const {
        return (end < b.start);
    }
    const PrtCodeAddress start, end;
};

typedef pair<class Prt_Cim*, PrtCimSpecificDataType> Prt_CimDataPair;
typedef map<Prt_IpRange, Prt_CimDataPair> Prt_CodeInfoTreeMap;

// A Prt_CodeIntervalTree is used to record which CodeInfoManagers are responsible for what [start..end] IP ranges.
class Prt_CodeIntervalTree
{
public:
    Prt_CodeIntervalTree(void) {
        pthread_mutex_init(&mCodeIntervalTreeMux,NULL);
    }

    ~Prt_CodeIntervalTree(void)
    {
    }

    // Look up the interval node that includes the specified IP. If found, set *cim to the corresponding Prt_Cim 
    // and return true; otherwise return false.
    bool find(PRT_IN PrtCodeAddress ip, PRT_OUT Prt_Cim **cim, PRT_OUT PrtCimSpecificDataType *opaqueData) const;

    // Insert a code interval into the tree and remember that it is managed by the specified Prt_Cim.  
    // Assert or hard error if the some portion of the specified code range is already in the tree.
    void insert(PRT_IN Prt_Cim *theCim, PRT_IN PrtCodeAddress start, PRT_IN PrtCodeAddress end, PRT_IN PrtCimSpecificDataType opaqueData);

    // Remove a code interval from the tree.  Returns true if successful.  Returns false if the specified Prt_Cim 
    // is not the one managing the code interval or if the exact code interval is not found.
    bool remove(PRT_IN Prt_Cim *theCim, PRT_IN PrtCodeAddress start, PRT_IN PrtCodeAddress end);

protected:
    Prt_CodeInfoTreeMap ranges;
    pthread_mutex_t mCodeIntervalTreeMux;

private:
    // shouldn't need to ever copy one of these...assert if we do so we'll know
    Prt_CodeIntervalTree(const Prt_CodeIntervalTree &) { assert(0); }
    Prt_CodeIntervalTree &operator=(const Prt_CodeIntervalTree &) { assert(0); return *this; }
}; //class Prt_CodeIntervalTree

#endif // !_PRTCODEINFOINTERNAL_H_
