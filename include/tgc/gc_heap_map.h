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

#ifndef _GC_HEAP_MAP_H_
#define _GC_HEAP_MAP_H_

#include "tgc/gc_header.h"

// The id uniquely indentifying a type, for example the address of a vtable would be sufficient.
typedef POINTER_SIZE_INT metadata_id;

// The object id associated with an unused area is FREE_SPACE_ID
#define FREE_SPACE_ID 0


typedef struct _heap_map_header {
    char version[64];
    void *heap_base;
    void *heap_ceiling;
    unsigned int map_start;
    unsigned int metadata_tuple_count;
    unsigned int object_tuple_count;
    unsigned int tuple_grouping_count;

} heap_map_header;

//
// The mapfile is at the location where one should start to dumping the metadata information
// The number of types + metadata tuples that are dumped are returned so they can be placed
// in the header.
//

typedef struct _metadata_tuple {
    void *vt;
    char class_name[128];
    unsigned int tuple_index;   // Not yet used but could might provide a simple direct index optimization
                                // at the cost of maintaining an index in the p_loaded_vtable_directory.
    unsigned int size;          // For objects the size in bytes of the object, or array element size.
} metadata_tuple;

// This is the information dumped for each life object in the system.
// It includes the metadata id as well as the size of the object.
typedef struct _heap_map_object_tuple {
    metadata_id id;
    POINTER_SIZE_INT size;
} heap_map_object_tuple;


// Create a heap map and place it inthe filename specified.
void create_heap_map(const char *filename);

#endif // _GC_HEAP_MAP_H_
