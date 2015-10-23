/*
 * COPYRIGHT_NOTICE_1
 */

#include <assert.h>
#include <gc_header.h>
#include "gc_v4.h"
// to get the locked compare exchange routines.
#include "mark.h"
//
// The low level heap access routines. All access to the heap must be done through these routines.
//

/*
 * Simple getters and setters for values of any reasonable size plus HeapObjects
 */

//
// The scheme here is to only let ORP and the Heap see pointers that have their
// bits flipped. The routines in this file will unflip the bits to get the real pointer
// and then flip them before they are passed to ORP or stored in the heap.
// I theory at least this should be sufficient to ensure that any accesses to the heap
// that aren't done via these routines will blow up almost immediately.
//
// The bits need to be mangled when they are passed back by the allocator.
//

// If manglerInt is set to 0 then the pointer is not mangled
// otherwise this int is added to the pointer to mangle it.
//
#ifdef MANGLEPOINTERS
// If MANGLEDPOINTERS is not defined the mangleBits and unmangelBits are identity macros that
// return the arguments passed wihtout modification. 
static int manglerInt = 0x040000000;

HeapObject *mangleBits(HeapObject *mangledObject)
{
    if (mangledObject == NULL) {
        return NULL;
    }
    if (manglerInt != 0) {
        if ( ((int)mangledObject & manglerInt) == manglerInt ) {
            printf ("In mangleBits manglerInt 0x0%x interfers with object address. 0x0%x\n", manglerInt, (uint32)mangledObject);
        }
    }   
    return (HeapObject *)((char *)mangledObject + manglerInt);
}

HeapObject *unmangleBits(HeapObject *mangledObject)
{
    if (mangledObject == NULL) {
        return NULL;
    }
    if (manglerInt != 0) {
        if ( ((int)mangledObject & manglerInt) != manglerInt ) {
            printf ("In unmangleBits manglerInt 0x0%x interfers with object address. 0x0%x\n", manglerInt, (uint32)mangledObject);
        }
    }
    return (HeapObject *)((char *)mangledObject - manglerInt);
}
#endif

/* Helper routines that take an object and an offset and return a slot of the desired type. */
static HeapObject **objectSlot(HeapObject *object, uint32 offset)
{
    return (HeapObject **)((char *)unmangleBits(object)+offset);
}
static g1 *g1Slot(HeapObject *object, uint32 offset)
{
    return (g1 *)((char *)unmangleBits(object)+offset);
}
static g2 *g2Slot(HeapObject *object, uint32 offset)
{
    return (g2 *)((char *)unmangleBits(object)+offset);
}
static g4 *g4Slot(HeapObject *object, uint32 offset)
{
    return (g4 *)((char *)unmangleBits(object)+offset);
}
static g8 *g8Slot(HeapObject *object, uint32 offset)
{
    return (g8 *)((char *)unmangleBits(object)+offset);
}
static f4 *f4Slot(HeapObject *object, uint32 offset)
{
    return (f4 *)((char *)unmangleBits(object)+offset);
}
static f8 *f8Slot(HeapObject *object, uint32 offset)
{
    return (f8 *)((char *)unmangleBits(object)+offset);
}
/* Routines that get and set fields in objects. */
GCEXPORT(HeapObject *, heapGetObject)(HeapObject *object, uint32 offset)
{
    return *objectSlot(object, offset);
}
GCEXPORT(void, heapSetObject)(HeapObject *object, uint32 offset, HeapObject *value)
{
    *objectSlot(object, offset) = value;
}

GCEXPORT(g1, heapGetG1)(HeapObject *object, uint32 offset)
{
    return *g1Slot(object, offset);
}
GCEXPORT(void, heapSetG1)(HeapObject *object, uint32 offset, g1 value)
{
    *g1Slot(object, offset) = value;
}

GCEXPORT(g2, heapGetG2)(HeapObject *object, uint32 offset)
{
    return *g2Slot(object, offset);
}
GCEXPORT(void, heapSetG2)(HeapObject *object, uint32 offset, g2 value)
{
    *g2Slot(object, offset) = value;
}

GCEXPORT(g4, heapGetG4)(HeapObject *object, uint32 offset)
{
    return *g4Slot(object, offset);
}
GCEXPORT(void, heapSetG4)(HeapObject *object, uint32 offset, g4 value)
{
    *g4Slot(object, offset) = value;
}

GCEXPORT(g8, heapGetG8)(HeapObject *object, uint32 offset)
{
    return *g8Slot(object, offset);
}
GCEXPORT(void, heapSetG8)(HeapObject *object, uint32 offset, g8 value)
{
    *g8Slot(object, offset) = value;
}


GCEXPORT(f4, heapGetF4)(HeapObject *object, uint32 offset)
{
    return *f4Slot(object, offset);
}
GCEXPORT(void, heapSetF4)(HeapObject *object, uint32 offset, f4 value)
{
    *f4Slot(object, offset) = value;
}


GCEXPORT(f8, heapGetF8)(HeapObject *object, uint32 offset)
{
    return *f8Slot(object, offset);
}
GCEXPORT(void, heapSetF8)(HeapObject *object, uint32 offset, f8 value)
{
    *f8Slot(object, offset) = value;
}

/*
 * References to the heap that are not in the heap or on the local threads stack.
 * For example Java statics.
 */

GCEXPORT(HeapObject*, heapGetGlobalObject)(volatile uint32 *txnRec, HeapObject **slot)
{
    return *slot;
}

GCEXPORT(void, heapSetGlobalObject)(volatile uint32 *txnRec, HeapObject **slot, HeapObject *value)
{
    *slot = value;
}


/* 
 * References to heap ojbects that are not in the heap or on local threads stacks but are not
 * application visible, end up here. For example object handles used for JNI constructs.
 */
GCEXPORT(HeapObject*, heapGetInternalObject)(HeapObject **slot)
{
    return *slot;
}

GCEXPORT(void, heapSetInternalObject)(HeapObject **slot, HeapObject *value)
{
    *slot = value;
}

/*
 * Accesses that are fenced to produce SC semantics. Used for example with Java volatiles.
 */

/* Routines that get and set fields in objects. */
GCEXPORT(HeapObject*, heapGetObjectFenced)(volatile uint32 *txnRec, HeapObject *object, uint32 offset)
{
    return *objectSlot(object, offset);
}
GCEXPORT(void, heapSetObjectFenced)(volatile uint32 *txnRec, HeapObject *object, uint32 offset, HeapObject *value)
{
    *objectSlot(object, offset) = mangleBits(value);
}

GCEXPORT(g1, heapGetG1Fenced)(HeapObject *object, uint32 offset)
{
    return *g1Slot(object, offset);
}
GCEXPORT(void, heapSetG1Fenced)(HeapObject *object, uint32 offset, g1 value)
{
    *g1Slot(object, offset) = value;
}

GCEXPORT(g2, heapGetG2Fenced)(HeapObject *object, uint32 offset)
{
    return *g2Slot(object, offset);
}
GCEXPORT(void, heapSetG2Fenced)(HeapObject *object, uint32 offset, g2 value)
{
    *g2Slot(object, offset) = value;
}

GCEXPORT(g4, heapGetG4Fenced)(HeapObject *object, uint32 offset)
{
    return *g4Slot(object, offset);
}
GCEXPORT(void, heapSetG4Fenced)(HeapObject *object, uint32 offset, g4 value)
{
    *g4Slot(object, offset) = value;
}

GCEXPORT(g8, heapGetG8Fenced)(HeapObject *object, uint32 offset)
{
    return *g8Slot(object, offset);
}
GCEXPORT(void, heapSetG8Fenced)(HeapObject *object, uint32 offset, g8 value)
{
    *g8Slot(object, offset) = value;
}

GCEXPORT(f4, heapGetF4Fenced)(HeapObject *object, uint32 offset)
{
    return *f4Slot(object, offset);
}
GCEXPORT(void, heapSetF4Fenced)(HeapObject *object, uint32 offset, f4 value)
{
    *f4Slot(object, offset) = value;
}

GCEXPORT(f8, heapGetF8Fenced)(HeapObject *object, uint32 offset)
{
    return *f8Slot(object, offset);
}
GCEXPORT(void, heapSetF8Fenced)(HeapObject *object, uint32 offset, f8 value)
{
    *f8Slot(object, offset) = value;
}

/*
 * References to the heap that are not in the heap or on the local threads stack.
 * For example Java statics. 
 * We do not need the fenced verison of routines like heapGetInternalObjectFenced
 */

GCEXPORT(HeapObject*, heapGetGlobalObjectFenced)(volatile uint32 *txnRec, HeapObject **slot)
{
    return *slot;
}

GCEXPORT(void, heapSetGlobalObjectFenced)(HeapObject **slot, HeapObject *value)
{
    *slot = value;
}

/*
 * The atomic CAS version accesses. Used for example to update object version for the STM or
 * for object locks.
 */

GCEXPORT(Boolean,      heapCASG1)(HeapObject *object, uint32 offset, g1 oldValue, g1 newValue)
{
    Boolean success = FALSE;

    success = (oldValue == LockedCompareExchangeUint8((uint8 *)((char *)object+offset), newValue, oldValue));

    return success;
}
GCEXPORT(Boolean,      heapCASG2)(HeapObject *object, uint32 offset, g2 oldValue, g2 newValue)
{    
    Boolean success = FALSE;

    success = (oldValue == LockedCompareExchangeUint16((uint16 *)((char *)object+offset), newValue, oldValue));

    return success;
}

GCEXPORT(Boolean,      heapCASG4)(HeapObject *object, uint32 offset, g4 oldValue, g4 newValue)
{
    Boolean success = FALSE;
#ifdef _IA64_
        success = (oldValue == InterlockedCompareExchange((volatile LONG *)((char *)object+offset), newValue, oldValue));
#else // !_IA64_
#ifdef ORP_POSIX
        success = (oldValue == (g4)LockedCompareExchangeUint32((uint32*)((char *)object+offset), newValue, oldValue));
#else // !oldValue
        success = (oldValue == (g4)LockedCompareExchangeUint32((uint32*)((char *)object+offset), newValue, oldValue));
//        success = (oldValue == (g4)InterlockedCompareExchange((volatile long *) ((char *)object+offset), newValue, oldValue));
#endif // !ORP_POSIX
#endif // !_IA64_
    return success;
}

GCEXPORT(Boolean,      heapCASG8)(HeapObject *object, uint32 offset, g8 oldValue, g8 newValue)
{
    assert("Not Implemented" && 0);
    return FALSE;
}
