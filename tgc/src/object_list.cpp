/*
 * COPYRIGHT_NOTICE_1
 */

// System header files
#include <iostream>

// GC header files
#include "gc_cout.h"
#include "gc_header.h"
#include "gc_v4.h"
#include "remembered_set.h"
#include "block_store.h"
#include "object_list.h"
#include "work_packet_manager.h"
#include "garbage_collector.h"
#include "gcv4_synch.h"


///////////////////////////////////////////////////////////////////////////////////////////////////////////////


Object_List::Object_List() 
{
    _size_in_entries = DEFAULT_OBJECT_SIZE_IN_ENTRIES;

    _store = (Partial_Reveal_Object **)malloc(_size_in_entries * 
                                          sizeof(Partial_Reveal_Object *));

    if (_store==NULL) {
        printf ("Error: malloc failed while creating object list.\n");
        assert(0);
        orp_exit(17023);
    }

    _resident_count  = 0;
}

Object_List::~Object_List()
{
    free(_store);
}

unsigned
Object_List::add_entry(Partial_Reveal_Object *p_obj)
{
    if (_resident_count >= (_size_in_entries - 1)) {
        _extend();
    }

    _store[_resident_count++] = p_obj;

    return (_resident_count - 1);
}

bool 
Object_List::exists(Partial_Reveal_Object *p_obj)
{
	for (unsigned int i = 0; i < _resident_count; i++) {
		if (_store[i] == p_obj) {
			return true;
		} 
	} 
	return false;
}

void
Object_List::_extend()
{

    Partial_Reveal_Object **old_store = _store;

    //
    // Present policy: double it.
    //
    _size_in_entries = _size_in_entries * 2;

    _store = (Partial_Reveal_Object **)malloc(_size_in_entries * 
                                          sizeof (Partial_Reveal_Object *));

    if (_store==NULL) {
        printf ("Error: malloc failed while creating object list.\n");
        assert(0);
        orp_exit(17024);
    }

    memcpy((void *)_store, 
           (void *)old_store, 
           _resident_count * sizeof(Partial_Reveal_Object *));

	free(old_store);
}

Partial_Reveal_Object *
Object_List::next()
{
    if (_current_pointer >= _resident_count) {
        return NULL;
    }

    return _store[_current_pointer++];
}

void 
Object_List::debug_dump_list()
{
    printf ("Dump of object list:\n");
    for (unsigned idx = 0; idx < _resident_count; idx++) {
        printf ("entry[%d] = %p\n", idx, _store[idx]);
    }
}


void
Object_List::rewind()
{
    _current_pointer = 0;
}

void
Object_List::reset()
{
    rewind();
    _resident_count = 0;
}

unsigned
Object_List::size()
{
    return _resident_count;
}

// end Object_List.cpp

// Start Root list which is similar to object list but hold pointers to 
// slots not in the heap that hold references to objects in the heap.
//

Root_List::Root_List() 
{
    _size_in_entries = DEFAULT_OBJECT_SIZE_IN_ENTRIES;

    _store = (Partial_Reveal_Object ***)malloc(_size_in_entries * 
                                          sizeof(Partial_Reveal_Object **));
#ifdef GC_ROOT_PINNING
	// Allocate _is_root_pinned[] when root _store is allocated too.
	_is_root_pinned = (bool *) malloc(_size_in_entries * sizeof(bool));
#endif // GC_ROOT_PINNING

    if (_store==NULL) {
        printf ("Error: malloc failed while creating object list.\n");
        assert(0);
        orp_exit(17025);
    }
    reset();

}

Root_List::~Root_List()
{
    free(_store);
#ifdef GC_ROOT_PINNING
	free(_is_root_pinned);
#endif // GC_ROOT_PINNING
}

unsigned
#ifdef GC_ROOT_PINNING
Root_List::add_entry(Partial_Reveal_Object **pp_obj, bool root_is_pinned)
#else
Root_List::add_entry(Partial_Reveal_Object **pp_obj)
#endif // GC_ROOT_PINNING
{
    if (_resident_count >= (_size_in_entries - 1)) {
        _extend();
    }
#ifdef PINNING_GC
	// Record pinned property
	_is_root_pinned[_resident_count] = root_is_pinned;
#endif

    _store[_resident_count++] = pp_obj;

    return (_resident_count - 1);
}

void
Root_List::_extend()
{

#ifdef GC_ROOT_PINNING
	// Extend pinned array too.
    bool *old_is_root_pinned = _is_root_pinned;
    // Present policy: double it.
    _size_in_entries = _size_in_entries * 2;
    _is_root_pinned = (bool *)malloc(_size_in_entries * sizeof (bool *));
    if (_is_root_pinned==NULL) {
        orp_cout << "Error: malloc failed while creating object list" << std::endl;
        assert(0);
        orp_exit(17026);
    }
    memcpy((void *)_is_root_pinned, 
           (void *)old_is_root_pinned, 
           _resident_count * sizeof(bool *));
	free(old_is_root_pinned);
#endif // GC_ROOT_PINNING

    Partial_Reveal_Object ***old_store = _store;

    //
    // Present policy: double it.
    //
    _size_in_entries = _size_in_entries * 2;
  
//    orp_cout << "Extending root list to " << _size_in_entries << std::endl;

    _store = (Partial_Reveal_Object ***)malloc(_size_in_entries * 
                                          sizeof (Partial_Reveal_Object *));

    if (_store==NULL) {
        printf ("Error: malloc failed while creating object list.\n");
        assert(0);
        orp_exit(17027);
    }

    memcpy((void *)_store, 
           (void *)old_store, 
           _resident_count * sizeof(Partial_Reveal_Object *));

	free(old_store);
}


#ifdef GC_ROOT_PINNING
// New API that also returns by reference the pinning attribute of a root ref.
Partial_Reveal_Object **
Root_List::pp_next(bool *is_pinned)
{
    if (_current_pointer >= _resident_count) {
        return NULL;
    }
	// Set the is_pinned attribute upon return.
	*is_pinned = _is_root_pinned[_current_pointer];
    return _store[_current_pointer++];
}
#endif // GC_ROOT_PINNING


Partial_Reveal_Object **
Root_List::pp_next()
{    if (_current_pointer >= _resident_count) {
        return NULL;
    }

    return _store[_current_pointer++];
}

void 
Root_List::debug_dump_list()
{
    printf ("Dump of object list: ");
    for (unsigned idx = 0; idx < _resident_count; idx++) {
        printf ("entry[%d] = %p\n", idx, _store[idx]);
    }
}

void
Root_List::rewind()
{
    _current_pointer = 0;
}

void
Root_List::reset()
{
    rewind();
#ifdef _DEBUG
    unsigned int i;
    for (i = 0; i < _size_in_entries; i++) {
        _store[i] = NULL;
    }
#endif
    _resident_count = 0;
}

unsigned
Root_List::size()
{
    return _resident_count;
}




VTable_List::VTable_List() 
{
    _size_in_entries = DEFAULT_OBJECT_SIZE_IN_ENTRIES;

    _store = (Partial_Reveal_VTable **)malloc(_size_in_entries * 
                                          sizeof(Partial_Reveal_VTable *));

    memset (_store, 0, _size_in_entries * sizeof(Partial_Reveal_VTable *));
    if (_store==NULL) {
        printf ("Error: malloc failed while creating VTable list.\n");
        assert(0);
        orp_exit(17028);
    }

    _resident_count  = 0;
}

VTable_List::~VTable_List()
{
    free(_store);
}

unsigned
VTable_List::add_entry(Partial_Reveal_VTable *p_obj)
{
    if (_resident_count >= (_size_in_entries - 1)) {
        _extend();
    }

    _store[_resident_count] = p_obj;
    _resident_count++;
    return (_resident_count - 1);
}

bool 
VTable_List::exists(Partial_Reveal_VTable *p_obj)
{
	for (unsigned int i = 0; i < _resident_count; i++) {
		if (_store[i] == p_obj) {
			return true;
		} 
	} 
	return false;
}


void
VTable_List::_extend()
{

    Partial_Reveal_VTable **old_store = _store;

    //
    // Present policy: double it.
    //
    _size_in_entries = _size_in_entries * 2;

    _store = (Partial_Reveal_VTable **)malloc(_size_in_entries * 
                                          sizeof (Partial_Reveal_VTable *));
    
    if (_store==NULL) {
        printf ("Error: malloc failed while creating object list.\n");
        assert(0);
        orp_exit(17029);
    }

    memset (_store, 0, _size_in_entries * sizeof(Partial_Reveal_VTable *));
    memcpy((void *)_store, 
           (void *)old_store, 
           _resident_count * sizeof(Partial_Reveal_VTable *));

	free(old_store);
}

Partial_Reveal_VTable *
VTable_List::next()
{
    if (_current_pointer >= _resident_count) {
        return NULL;
    }

    return _store[_current_pointer++];
}

void 
VTable_List::debug_dump_list()
{
    printf ("Dump of object list:\n");
    for (unsigned idx = 0; idx < _resident_count; idx++) {
        printf ("entry[%d] = %p\n", idx, _store[idx]);
    }
}


void
VTable_List::rewind()
{
    _current_pointer = 0;
}

void
VTable_List::reset()
{
    rewind();
    _resident_count = 0;
}

unsigned
VTable_List::size()
{
    return _resident_count;
}

// end Object_List.cpp
// end file gc\Object_List.cpp
