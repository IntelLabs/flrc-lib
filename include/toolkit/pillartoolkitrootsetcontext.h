/*
 * COPYRIGHT_NOTICE_1
 */

#ifndef _PILLAR_TOOLKIT_ROOT_SET_
#define _PILLAR_TOOLKIT_ROOT_SET_

#include "prt/prt.h"
#include <assert.h>
#include <string.h>

/*
 * ref - Points to a managed object in the garbage collected heap.
 *      _OpaqueRef ensures that ref's can't accidently be cast to/from other types
 */
struct _OpaqueRef;
typedef struct _OpaqueRef *ref;

/*
 * mp - may point into the middle of a managed object.
 */
struct _OpaqueMP;
typedef struct _OpaqueMP *mp;

typedef PrtVseType  PrtToolkitRootContextStackHandle;
typedef void       *PrtToolkitRootContextHandle;
typedef ref        *PrtToolkitRefHandle;
typedef mp         *PrtToolkitMpHandle;
#define LATEST_ROOT_CONTEXT NULL

#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////////////////////////////
// Root context support: these hold ref handles in unmanaged code
//////////////////////////////////////////////////////////////////////////

// This function creates a root context stack. One of these stacks must be
// created before you can allocate any root contexts.
PrtToolkitRootContextStackHandle pillarToolkitCreateRootContextStack(void);

#define ROOT_CONTEXT_SIZE (sizeof(class RootContext))

// This function creates a context in which handles to heap roots can be
// held in native code.  You can use this function however you like but
// the number of prtCreateRootContext()s must be balanced with the
// number of prtDeleteRootContext()s.  You can create one context for an
// entire native method invocation or a different context for each
// activation record.  RootContexts are a LIFO stack.
// The second argument is a pointer to caller-provided storage of
// ROOT_CONTEXT_SIZE bytes for storing the root context, generally on the
// stack, and this argument is returned.
PrtToolkitRootContextHandle pillarToolkitCreateRootContext(PrtToolkitRootContextStackHandle, PrtToolkitRootContextHandle);

// Removes the last RootContext created in this task. (Pops last RootContext
// on the RootContext LIFO stack.)
void pillarToolkitReleaseLastRootContext(PrtToolkitRootContextStackHandle);

// Returns a handle to the latest root context on the context LIFO stack.
PrtToolkitRootContextHandle pillarToolkitGetLatestRootContext(PrtToolkitRootContextStackHandle);

// Allocates a ref handle in the specified RootContext.
// Using the reserved RootContext LATEST_ROOT_CONTEXT will allocate the handle
// from the context on the top of the RootContext stack.
// numHandles = the number of requested handles.  If you request more than 1 then
// the return value will point to the first handle of an array of the requested size.
// "external" is true if the true root is external and we hold a pointer to it.  It is
// false if the true root is inside the root context node itself.
PrtToolkitRefHandle pillarToolkitAllocateRefHandle(PrtToolkitRootContextHandle,unsigned numHandles, PrtBool external);

// Allocates a MP handle in the specified RootContext.
// Using the reserved RootContext LATEST_ROOT_CONTEXT will allocate the handle
// from the context on the top of the RootContext stack.
// numHandles = the number of requested handles.  If you request more than 1 then
// the return value will point to the first handle of an array of the requested size.
// "external" is true if the true root is external and we hold a pointer to it.  It is
// false if the true root is inside the root context node itself.
PrtToolkitMpHandle pillarToolkitAllocateMpHandle(PrtToolkitRootContextHandle,unsigned numHandles, PrtBool external);

// Deallocates a ref or MP handle.  If LATEST_ROOT_CONTEXT is specified
// the Pillar will search for the handle to delete.  If the RootContext is
// specified then Pillar will search only that context.
// returns 1 if the specified handle is found in the specified context, 0 otherwise
int pillarToolkitDeallocateRefHandle(PrtToolkitRootContextHandle, PrtToolkitRefHandle);
int pillarToolkitDeallocateMpHandle(PrtToolkitRootContextHandle, PrtToolkitMpHandle);

#ifdef __cplusplus
}
#endif

class RootContextNode
{
public:
    RootContextNode(unsigned num_slots=8) :
      mCurFreeIndex(0),
      mRefMpMask(0), // all entries default to ref
      mExternalRefMask(0), // all entries default to contained within the node
      mNext(NULL)
    {
        // using an unsigned for the ref_mp mask so can't have more than 32 entries per block
        if (num_slots > 32) {
            num_slots = 32;
        }
        // allocate the heap pointers
        mHeapPointers = new void *[num_slots];
        memset(mHeapPointers,0,num_slots * sizeof(void*));
        mTotalNumSlots = num_slots;
    } // RootContextNode::RootContextNode

    ~RootContextNode(void)
    {
        // JMS 2006-07-07.  Use a non-recursive destructor to avoid blowing out the stack.
        delete mHeapPointers;
        //delete mNext;
        RootContextNode *node, *next;
        for (node = this; node != NULL; node = next) {
            next = node->mNext;
            node->mNext = NULL;
            if (node != this)
                delete node;
        }
    } // RootContextNode::~RootContextNode

    RootContextNode *getNext(void) const
    {
        return mNext;
    } // RootContextNode::getNext

    void setNext(RootContextNode *rcn)
    {
        mNext = rcn;
    } // RootContextNode::setNext

    bool canHold(unsigned numHandles) const
    {
        return mCurFreeIndex + numHandles <= mTotalNumSlots;
    } // RootContextNode::canHold

    unsigned getNumSlots(void) const
    {
        return mTotalNumSlots;
    } // RootContextNode::getNumSlots

    // allocate a ref heap handle from this node
    PrtToolkitRefHandle allocateRefHandle(unsigned numHandles, PrtBool external);

    // allocate a mp heap handle from this node
    PrtToolkitMpHandle allocateMpHandle(unsigned numHandles, PrtBool external);

    // enumerate the roots in this context
    void enumerate(struct PrtRseInfo *rootSetInfo);

protected:
    unsigned createRefMpMask(unsigned index) const;

    void setMask(unsigned index)
    {
        mRefMpMask |= createRefMpMask(index);
    } // RootContextNode::setMask

    unsigned getMask(unsigned index)
    {
        return mRefMpMask & createRefMpMask(index);
    } // RootContextNode::getMask

    void setExternalRefMask(unsigned index)
    {
        mExternalRefMask |= createRefMpMask(index);
    } // RootContextNode::setExternalRefMask

    unsigned getExternalRefMask(unsigned index)
    {
        return mExternalRefMask & createRefMpMask(index);
    } // RootContextNode::getExternalRefMask

protected:
    unsigned          mCurFreeIndex;
    unsigned          mTotalNumSlots;
    // If bit N is 0 then slot N contains a ref; otherwise it contains an mp.
    unsigned          mRefMpMask;
    unsigned          mExternalRefMask;
    void            **mHeapPointers;
    RootContextNode  *mNext; // next chunk of heap pointers (ref's or mp's)

private:
    RootContextNode(const RootContextNode &)   { assert(0); }
}; //class RootContextNode


class RootContext
{
    // RootContext is essentially a subclass of PrtVse, so we make sure the
    // PrtVse fields come first in the structure.
private:
    char vseOverhead[PILLAR_VSE_SIZE];
public:
    unsigned safe_esp;
	RootContext(void)
	{
        init();
	} // RootContext::Rootcontext

    ~RootContext(void)
    {
        destroy();
    } // RootContext::~RootContext

    void init(void)
    {
        nodes = new RootContextNode;
    }

    void destroy(void)
    {
        delete nodes;
    }

    // allocate a ref heap handle from this context
    PrtToolkitRefHandle allocateRefHandle(unsigned numHandles, PrtBool external);

    // allocate a mp heap handle from this context
    PrtToolkitMpHandle  allocateMpHandle(unsigned numHandles, PrtBool external);

    // enumerate the roots in this context
    void enumerate(struct PrtRseInfo *rootSetInfo);

protected:
    RootContextNode *nodes;
    RootContextNode *findAvailableNode(unsigned numHandles);

private:
	RootContext(const RootContext &) { assert(0); }
}; //class RootContext


#endif // _PILLAR_TOOLKIT_ROOT_SET_
