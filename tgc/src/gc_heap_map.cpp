/*
 * COPYRIGHT_NOTICE_1
 */

// This code generates heap maps and allows querying of the maps.

/**************
	As a first cut, we would like to map GC metadata information to memory addresses.  
    Amongst other things this would then be used by a cache simulator in conjunction with SoftSDV to figure 
    out high-level information about cache contents.

Overview

    The cache simulator will execute some annotated Java code starting in functional mode. Annotations
    will consist of a distinguished method call that will change from functional mode to simulated mode.
    As part of turning on the cache simulator a second distinguished method will be called that will
    inform the GC that cache simulation has started. The simulated mode will run for a relatively small 
    amount of time and generate information a cache activity report. (the term trace is overloaded so I 
    am avoiding it here.) This cache activity report will contain heap addresses. The Java code will
    execute a method to turn the cache simulator off. It will also execute a method that informs the GC
    that the next time it is executed it is to generate a heap map. Generation of this heap map is what
    the code in this file is responsible for. Given a cache activity report and a heap map one has 
    sufficient information to generate an cache activity report that includes type information.
   
Contents of heap map
This is a binary file that can be read and written using C stdio.h fwrite/fread and the associated struct defs.

Metadata part.
"	VTable start addresses
"   String containing the name of the type.
Heap part
"	Object start address plus size plus VTable start address of associated type.
"   Free area start plus size.
****************/

#include <stdio.h>
#include "gc_v4.h"
#include "garbage_collector.h"
#include "gc_header.h"
#include "gc_heap_map.h"

//
// This is the handle passed back and forth between the GC and SoftSVD and indicates the heap map to inspect.
//
typedef void *Heap_Map_Handle;

const char *heap_map_file_name = "heap_map_test.hmp";

metadata_tuple *get_object_metadata_id (void *addr) {
    // For testing open the file, print out the version and close the file.
    
    FILE *map_file = fopen(heap_map_file_name, "rb"); // Open a binary file.
    if (!map_file) {
        printf ("Internal bug - Error creating map file, no map file created.\n");
    }

    heap_map_header header;
    
    size_t bytes_read = fread (&header, 1, sizeof(heap_map_header), map_file);

    printf ("Read %d bytes into header with version %s\n", bytes_read, &(header.version[0]));

    fclose(map_file);

    return (metadata_tuple *)0x0411;
}


void *heap_map_get_next_object (void *addr) {
    return NULL;
}

metadata_id get_object_type_id(Partial_Reveal_VTable *obj) {
    return 411;
}

unsigned int dump_metadata_info (FILE *map_file) {
    // The GC knows about all the types because of gc_call_prepare had populated p_loaded_vtable_directory
    // with the appropriate vtables. So now we will loop through them dumping the vtable (an void *) as
    // well as the string representing the class name.

    Partial_Reveal_VTable *this_vt = NULL;
    unsigned int tuple_index = 1;
    p_loaded_vtable_directory->rewind();
    while ((this_vt = (Partial_Reveal_VTable *)p_loaded_vtable_directory->next())) {
        metadata_tuple this_tuple;
        this_tuple.vt = this_vt;
        char *the_class_name = (char *)this_vt->get_gcvt()->gc_class_name;
        strcpy ((char *)&this_tuple.class_name, the_class_name);
#if 0
        printf ("vt = %p, this_tuple.vt = %p, the_class_name = %s this_tuple.class_name = %s\n", 
            this_vt, this_tuple.vt, the_class_name, (char *)&this_tuple.class_name);
#endif
        fwrite (&this_tuple, 1, sizeof(metadata_tuple), map_file);
        tuple_index++;   
    }
    
    tuple_index--;
//    printf("dumped %d\n", tuple_index);
    return tuple_index;
}

// Dump the objects out to the map file.
unsigned int dump_object_info (FILE *map_file) {
    unsigned int object_count = 0;
    unsigned int free_space_count = 0;
    unsigned int marker_interval_count = 0; // TBD used to indicate where to place markers

    block_info *current_block;
    heap_map_object_tuple local_tuple;
    local_tuple.id = 0;
    local_tuple.size = 0;
    size_t fwrite_status = 0;

    p_global_gc->init_block_iterator();
    while ((current_block = p_global_gc->get_next_block())) {
        current_block->init_object_iterator();
//        printf ("dumping block at %p\n", current_block);
        unsigned int size_check = 0;
        unsigned int block_object_count = 0;
        unsigned int block_free_space_count = 0;
        assert(0);
        exit(-1);
#if 0
        while (current_block->get_next_heap_map_object_tuple(&local_tuple)) { 
            fwrite_status =  fwrite ((const void *)&local_tuple, 1L, sizeof(heap_map_object_tuple), map_file);
            if (local_tuple.id == FREE_SPACE_ID) {
                free_space_count++;
                block_free_space_count++;
            } else {
                object_count++;
                block_object_count++;
            }
            size_check += local_tuple.size;
            if (fwrite_status != sizeof(heap_map_object_tuple)) {
                printf ("--------------------------------------- Bummer internal problem in dump_object_info\n");
            }
           // printf ("o");
        }
        if (size_check == GC_BLOCK_SIZE_BYTES) {
            // printf ("Size Check OK\n");
        } else if (size_check < GC_BLOCK_SIZE_BYTES) {
            printf (" --------------------------------------- Bummer too small Size %d count %d free areas %d does not check \n", 
                size_check, block_object_count, block_free_space_count);
        } else if (block_object_count <= 1) {
            // printf ("Size is larger than block and we have one object which is fine and of size = %d\n", size_check);
        } else {
            printf (" --------------------------------------- Bummer too weird size %d count %d and free areas %d\n",
                size_check, block_object_count, block_free_space_count);
        }
//        printf ("b");
#endif
    }
//    printf ("%d total objects and %d free_spaces have been dumped. \n", object_count, free_space_count);
    return object_count + free_space_count + marker_interval_count;
}

void create_heap_map(const char *filename) {
    // careful this is limited in size to 64 characters including the null terminating character.
    //              0        1         2         3         4         5         6  |
    char version[64] ="Heap Map Strawman 0.0 Hello World";
    void *base = gc_heap_base_address();
    void *ceiling = gc_heap_ceiling_address();
    
    //
    
    unsigned int map_start = 0;
    unsigned int object_tuple_count = 0;
    unsigned int metadata_tuple_count = 0; 
    unsigned int tuple_grouping_count = 0;
    
    FILE *map_file = fopen(heap_map_file_name, "wb");
    if (!map_file) {
        printf ("Internal bug - Error creating map file, no map file created.\n");
    }
    
    heap_map_header header;

    strcpy((char *)(header.version), (const char *)version);
    header.heap_base = base;
    header.heap_ceiling = ceiling;
    header.map_start = map_start;
    header.object_tuple_count = object_tuple_count;
    header.metadata_tuple_count = metadata_tuple_count;
    header.tuple_grouping_count = tuple_grouping_count;

        // Skip over header information, this is written once the heap has been dumped and 
    // we have all the information we need.
    fseek (map_file, sizeof(heap_map_header), SEEK_SET);

    // dump the type and metadata information.
    header.metadata_tuple_count = dump_metadata_info(map_file);

    header.object_tuple_count = dump_object_info(map_file);

    // Now dump the header at the top of the file.
    int should_be_zero = fseek (map_file, 0, SEEK_SET);
    if (should_be_zero) {
        printf ("Internal bug - Error fseek did not work, map file may be broken. \n");
    }

    fwrite (&header, 1, sizeof(heap_map_header), map_file);

    should_be_zero = fflush (map_file);
    if (should_be_zero) {
        printf ("Internal bug - Error flushing map file, map file may be broken. \n");
    }
    should_be_zero = fclose (map_file);
    if (should_be_zero) {
        printf ("Internal bug - Error closing map file, map file may be broken. \n");
    }
#if 0
    // Forward reference.
    void sample_code_to_read_map_file (const char *heap_map_file_name);
    sample_code_to_read_map_file(heap_map_file_name);
#endif
    return;
}


unsigned int read_and_print_metadata_info (FILE *map_file, heap_map_header header) {
    metadata_tuple this_tuple;
    unsigned int i;
    for (i=0; i< header.metadata_tuple_count; i++) {
        fread (&this_tuple, 1, sizeof(metadata_tuple), map_file);
        // TBD   Need to use ferror and feof to determine if this works. 
        printf ("this_tuple.vt = %p, this_tuple.class_name = %s\n", 
                 this_tuple.vt, (char *)&this_tuple.class_name);

    }
    return i;
}

unsigned int read_and_print_tuple_info (FILE *map_file, heap_map_header header) {
    heap_map_object_tuple this_tuple;
    unsigned int i;
    for (i=0; i< header.object_tuple_count; i++) {
        fread (&this_tuple, 1, sizeof(heap_map_object_tuple), map_file);
        // TBD   Need to use ferror and feof to determine if this works. 
        printf ("this_tuple.size = %d, this_tuple.id = %x\n", 
                 this_tuple.size, this_tuple.id);

    }
    return i;
}

#if 0
void sample_code_to_read_map_file (const char *heap_map_file_name) {
    printf (" Now read it back in and take a look at it.\n");
    
    FILE *read_map_file = fopen(heap_map_file_name, "rb"); // Open a binary file.

    heap_map_header metadata_read_header;
    
    fread (&metadata_read_header, 1, sizeof(heap_map_header), read_map_file);

    unsigned int metadata_number_read = read_and_print_metadata_info (read_map_file, metadata_read_header);

    // Now readt he tuples.
        
    unsigned int tuples_number_read = read_and_print_tuple_info(read_map_file, metadata_read_header);
    
    // Close the file and exit.

    fclose(read_map_file);

    printf ("Read in %d metadata tuples and %d heap tuples.\n", metadata_number_read, tuples_number_read);
}
#endif

