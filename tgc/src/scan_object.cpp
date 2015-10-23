/*
 * COPYRIGHT_NOTICE_1
 */

//
// This file implements interfaces of how to scan an object.
//

// System header files
#include <iostream>
#include <assert.h>

// GC header files
#include "gcv4_synch.h"
#include "gc_cout.h"
#include "gc_header.h"
#include "gc_v4.h"
#include "descendents.h"

GCEXPORT(Boolean, object_has_gc_slots)(Managed_Object_Handle obj)
{
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)obj;
    return p_obj->vt()->get_gcvt()->gc_object_has_slots;
}

GCEXPORT(const char*, object_get_class_name)(Managed_Object_Handle obj)
{
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)obj;
    return class_get_name(p_obj->vt()->get_gcvt()->gc_clss);
}

GCEXPORT(Boolean, object_is_array) (Managed_Object_Handle obj)
{
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)obj;
    return is_array(p_obj);
}

GCEXPORT(Boolean, object_is_array_of_primitives)(Managed_Object_Handle obj)
{
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)obj;
    return is_array_of_primitives(p_obj);
}


#if 0 //According to JMS, use vector routine to replace array scanner
//
// Return the last offset in the array that points to a reference.
// This will be decremented with each p_next_ref call.
//

GCEXPORT(unsigned, object_init_array_scanner) (Managed_Object_Handle array) 
{
    Partial_Reveal_Object *p_array = (Partial_Reveal_Object *)array;
    return init_array_scanner(p_array);
}

//
// Move the scanner to the next ref.
//
GCEXPORT(unsigned, object_next_array_ref) (unsigned offset) 
{
    return next_array_ref(offset);
}

//
// Take the offset returned from init_array_scanner and 
// return the reference it referes to. Decrement the offset
// since we go through array backwards.
//
GCEXPORT(Managed_Object_Handle*, object_get_array_ref)(Managed_Object_Handle obj, unsigned offset) 
{
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)obj;
    return (Managed_Object_Handle*)p_get_array_ref(p_obj, offset);
}

#endif //#if 0 //According to JMS, use vector routine to replace array scanner

//
//  Return a pointer to the array of object offsets associated
//  with this object.
//
GCEXPORT(unsigned*, object_init_object_scanner) (Managed_Object_Handle obj) {
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object*)obj;
    return (unsigned*)init_object_scanner(p_obj);
}
//
// Given an offset and the pointer gotten from initObjectScanner return
// the next location of a pointer in the object. If none are left return null;
//

// Offsets are unsigned int which should be a structure that is independent
// of the word length.

GCEXPORT(unsigned*, object_next_ref) (unsigned *offset) {
    return p_next_ref(offset);
}

GCEXPORT(Managed_Object_Handle*, object_get_ref) (unsigned *offset, Managed_Object_Handle obj) {
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)obj;
    return (Managed_Object_Handle*)p_get_ref(offset, p_obj);
}

/////End of scan_object.cpp
