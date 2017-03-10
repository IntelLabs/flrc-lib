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

// System header files
#include <iostream>

// GC header files
#include "tgc/gc_cout.h"
#include "tgc/gc_header.h"
#include "tgc/gc_v4.h"
#include "tgc/remembered_set.h"
#include "tgc/block_store.h"
#include "tgc/object_list.h"
#include "tgc/work_packet_manager.h"
#include "tgc/garbage_collector.h"
#include "tgc/slot_offset_list.h"
#include "tgc/gcv4_synch.h"


///////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if 0

// This code is used to support interior pointers.
//

slot_offset_list::slot_offset_list()
{
    _size_in_entries = DEFAULT_OBJECT_SIZE_IN_ENTRIES;

    _store = (store_node*)malloc(sizeof(store_node));

    if (_store==NULL) {
        printf ("Error: malloc failed while creating slot offset list.\n");
        assert(0);
        orp_exit(17035);
    }

    start_node = _store;

    _store->_store = (slot_offset_entry *)malloc(DEFAULT_OBJECT_SIZE_IN_ENTRIES *
                                          sizeof(slot_offset_entry));
    _store->next = NULL;

    if (_store->_store==NULL) {
        printf ("Error: malloc failed while creating slot offset list.\n");
        assert(0);
        orp_exit(17036);
    }

    _resident_count  = 0;
    _total_count = 0;
}

slot_offset_list::~slot_offset_list()
{
    store_node *temp = start_node;
    while(temp) {
        _store = temp->next;
        free(temp->_store);
        free(temp);
        temp = start_node;
    }
}

void
slot_offset_list::add_entry(void **slot, Partial_Reveal_Object *base, POINTER_SIZE_INT offset)
{
    if (_resident_count >= (DEFAULT_OBJECT_SIZE_IN_ENTRIES)) {
        _extend();
    }

    _store->_store[_resident_count].slot = slot;
    _store->_store[_resident_count].base = base;
    _store->_store[_resident_count].offset = offset;
    _resident_count++;
    _total_count++;
}

void
slot_offset_list::_extend()
{
#if 1
    if(_store->next) {
        _store = _store->next;
        _resident_count = 0;
    } else {
        _store->next = (store_node*)malloc(sizeof(store_node));

        if (_store->next==NULL) {
            printf ("Error: malloc failed while creating slot offset list.\n");
            assert(0);
            orp_exit(17037);
        }

        _store = _store->next;
        _resident_count = 0;

        _store->_store = (slot_offset_entry *)malloc(DEFAULT_OBJECT_SIZE_IN_ENTRIES *
                                            sizeof(slot_offset_entry));
        _store->next = NULL;

        if (_store->_store==NULL) {
            printf ("Error: malloc failed while creating slot offset list.\n");
            assert(0);
            orp_exit(17038);
        }

        _size_in_entries += DEFAULT_OBJECT_SIZE_IN_ENTRIES;
    }
#else
    slot_offset_list::slot_offset_entry *old_store = _store;

    //
    // Present policy: double it.
    //
    _size_in_entries = _size_in_entries * 2;

    _store = (slot_offset_list::slot_offset_entry *)malloc(_size_in_entries *
                                          sizeof (slot_offset_entry));

    if (_store==NULL) {
        printf ("Error: malloc failed while creating slot offset list.\n");
        assert(0);
        orp_exit(17039);
    }

    memcpy((void *)_store,
           (void *)old_store,
           _resident_count * sizeof(Partial_Reveal_Object *));

	free(old_store);
#endif
}

void
slot_offset_list::next()
{
    _current_pointer++;
    if(_current_pointer % DEFAULT_OBJECT_SIZE_IN_ENTRIES == 0) {
        _cur_scan_node = _cur_scan_node->next;
    }
    // to avoid infinite loops of nexts make sure it is called only
    // once when the table is empty.
//    assert (_current_pointer < (_resident_count + 1));
}

void **slot_offset_list::get_slot ()
{
    return _cur_scan_node->_store[_current_pointer % DEFAULT_OBJECT_SIZE_IN_ENTRIES].slot;
//    return _store[_current_pointer].slot;
}

Partial_Reveal_Object *slot_offset_list::get_base ()
{
    return _cur_scan_node->_store[_current_pointer % DEFAULT_OBJECT_SIZE_IN_ENTRIES].base;
//    return _store[_current_pointer].base;
}

POINTER_SIZE_INT slot_offset_list::get_offset ()
{
    return _cur_scan_node->_store[_current_pointer % DEFAULT_OBJECT_SIZE_IN_ENTRIES].offset;
//    return _store[_current_pointer].offset;
}

Partial_Reveal_Object **slot_offset_list::get_last_base_slot ()
{
	// REVIEW vsm 10-May-2002 -- Made this change to fix a crash when called from
	// gc_add_root_set_entry_interior_pointer.
    //return &_store[_current_pointer - 1].base;
    return &(_store->_store[_resident_count - 1].base);
}

void
slot_offset_list::debug_dump_list()
{
#if 0
    printf ("Dump of slot_offset_list:\n");
    for (unsigned idx = 0; idx < _resident_count; idx++) {
        printf ("entry[%d].base = %p\n", idx, _store[idx].base);
        printf ("entry[%d].slot = %p\n", idx, _store[idx].slot);
        printf ("entry[%d].offset = %d\n", idx, _store[idx].offset);
    }
#endif
}

void
slot_offset_list::rewind()
{
    _current_pointer = 0;
    _cur_scan_node = start_node;
}

bool
slot_offset_list::available()
{
    return (_current_pointer < _total_count);
//    return (_current_pointer < _resident_count);
}

// Clear the table.
void
slot_offset_list::reset()
{
    rewind();
    _resident_count = 0;
    _total_count = 0;
    _store = start_node;
}

unsigned
slot_offset_list::size()
{
//    return _resident_count;
    return _total_count;
}

#endif
