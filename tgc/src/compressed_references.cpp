/*
 * COPYRIGHT_NOTICE_1
 */

#include "gc_header.h"
#include "remembered_set.h"
#include "compressed_references.h"


#ifndef DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES
bool gc_references_are_compressed = false;
#endif // !DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES


void *Slot::cached_heap_base = NULL;
void *Slot::cached_heap_ceiling = NULL;



// This function returns TRUE if references within objects and vector elements 
// will be treated by GC as uint32 offsets rather than raw pointers.
GCEXPORT(Boolean, gc_supports_compressed_references) ()
{
#ifdef DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES
    return gc_references_are_compressed ? TRUE : FALSE;
#else // !DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES
    return orp_references_are_compressed();
#endif // !DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES
}
