/*
 * COPYRIGHT_NOTICE_1
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
#include "tgc/gc_plan.h"
#include "tgc/gc_globals.h"
#include "tgc/gc_thread.h"
#include "tgc/descendents.h"
#include "tgc/gcv4_synch.h"

const int MAX_OBJECTS = 1024 * 1024 * 8;
#define JAVA_OBJECT_OVERHEAD (Partial_Reveal_Object::object_overhead_bytes())


typedef struct {
	Partial_Reveal_Object *p_obj;
	Partial_Reveal_Object *p_obj_copied;
	Partial_Reveal_Object *p_obj_moved;
} one_live_object;



unsigned int num_lives_before_gc = 0;
one_live_object *all_lives_before_gc = NULL;
unsigned int *cursors = NULL;
one_live_object **all_moved_lives_during_gc = NULL;


void init_verify_live_heap_data_structures() {
    if(!all_lives_before_gc) {
        all_lives_before_gc = new one_live_object[MAX_OBJECTS];
        if(!all_lives_before_gc) {
            printf("Couldn't allocate verify data structures.\n");
            orp_exit(17042);
        }
    }
    if(!cursors) {
        cursors = new unsigned int[g_num_cpus];
        if(!cursors) {
            printf("Couldn't allocate verify data structures.\n");
            orp_exit(17043);
        }
    }
    if(!all_moved_lives_during_gc) {
        all_moved_lives_during_gc = new one_live_object*[g_num_cpus];
        if(!all_moved_lives_during_gc) {
            printf("Couldn't allocate verify data structures.\n");
            orp_exit(17044);
        }
        for(unsigned int i=0;i<g_num_cpus;++i) {
            all_moved_lives_during_gc[i] = new one_live_object[MAX_OBJECTS];
            if(!all_moved_lives_during_gc[i]) {
                printf("Couldn't allocate verify data structures.\n");
                orp_exit(17045);
            }
        }
    }

	memset(all_lives_before_gc, 0, sizeof(one_live_object) * MAX_OBJECTS);
	memset(cursors, 0, sizeof(unsigned int) * g_num_cpus);
    for(unsigned int i=0;i<g_num_cpus;++i) {
        memset(all_moved_lives_during_gc[i], 0, sizeof(one_live_object) * MAX_OBJECTS);
    }
}


void add_repointed_info_for_thread(Partial_Reveal_Object *p_old, Partial_Reveal_Object *p_new, unsigned int thread_id) {
	assert(p_old);
	assert(p_new);
	if (thread_id > (g_num_cpus - 1)) {
        //	if (thread_id > (MAX_THREADS - 1)) {
		printf("add_repointed_info_for_thread() -- Only 4 threads allowed\n");
		orp_exit(17046);
	}
	unsigned int curr_index_for_thread = cursors[thread_id];
	if (curr_index_for_thread >= MAX_OBJECTS) {
		printf("add_repointed_info_for_thread() -- OVERFLOW of all_moved_lives_during_gc\n");
		orp_exit(17047);
	}
	all_moved_lives_during_gc[thread_id][curr_index_for_thread].p_obj = p_old;
	all_moved_lives_during_gc[thread_id][curr_index_for_thread].p_obj_moved = p_new;
	cursors[thread_id]++;
}


static int verify_live_heap_compare( const void *arg1, const void *arg2 ) {
	POINTER_SIZE_INT a = (POINTER_SIZE_INT) ((one_live_object *)arg1)->p_obj;
	POINTER_SIZE_INT b = (POINTER_SIZE_INT) ((one_live_object *)arg2)->p_obj;
	if (a < b) {
		return -1;
	}
	if (a == b) {
		return 0;
	}
	return 1;
}

void take_snapshot_of_lives_before_gc(unsigned int num_lives, Object_List *all_lives) {
	unsigned int total_bytes_allocated_by_verifier_to_replicate_live_heap = 0;

	all_lives->rewind();
	num_lives_before_gc = num_lives;
	for (unsigned int i = 0; i < num_lives; i++) {
		Partial_Reveal_Object *p_obj = all_lives->next();
		assert(p_obj);
		all_lives_before_gc[i].p_obj = p_obj;
		int obj_sz = get_object_size_bytes(p_obj);
		all_lives_before_gc[i].p_obj_copied = (Partial_Reveal_Object *) malloc(obj_sz);
		total_bytes_allocated_by_verifier_to_replicate_live_heap += obj_sz;
		memcpy(all_lives_before_gc[i].p_obj_copied, all_lives_before_gc[i].p_obj, obj_sz);
	}
	all_lives->rewind();

	// Sort and return the array of objects
	qsort(all_lives_before_gc, num_lives, sizeof(one_live_object), verify_live_heap_compare);

	// Verify integrity of qsort
	for (unsigned int j = 0; j < num_lives; j++) {
		if (j > 0) {
			// stricly sorted and no duplicates...
			assert((POINTER_SIZE_INT) all_lives_before_gc[j-1].p_obj < (POINTER_SIZE_INT) all_lives_before_gc[j].p_obj);
			assert(all_lives_before_gc[j].p_obj_copied->vt() == all_lives_before_gc[j].p_obj->vt());
		}
	}

	printf("total_bytes_allocated_by_verifier_to_replicate_live_heap = %d\n", total_bytes_allocated_by_verifier_to_replicate_live_heap);
}


void insert_moved_reference(Partial_Reveal_Object *p_old, Partial_Reveal_Object *p_new) {
	assert(p_new);
	// Insertion using binary search.
	unsigned int low = 0;
	unsigned int high = num_lives_before_gc;
	bool done = false;
	while (low <= high) {
		unsigned int mid = (high + low) / 2;
		if (all_lives_before_gc[mid].p_obj == p_old) {
			assert(all_lives_before_gc[mid].p_obj_moved == NULL);
			all_lives_before_gc[mid].p_obj_moved = p_new;
			// only objects resident in compaction block actually move
			assert(p_global_gc->is_compaction_block(GC_BLOCK_INFO(p_old)));
			// DONE
			done = true;
			break;
		} else if ((POINTER_SIZE_INT) all_lives_before_gc[mid].p_obj <  (POINTER_SIZE_INT) p_old) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}
	if (!done) {
		assert((low > high) || ((low == high) && (all_lives_before_gc[low].p_obj != p_old)));
		printf("insert_moved_reference() -- CANT FIND LIVE OBJECT %p \n", p_old);
		assert(0);
	}
}


// The input arg is an object that was moved during GC (p_old, is the heap address before GC started)
// return address is in C malloc space...it is the saved away copy of p_old before GC mutated the heap..

Partial_Reveal_Object * find_saved_away_copy_of_live_before_GC_started(Partial_Reveal_Object *p_old) {
	assert(p_old);
	// Insertion using binary search.
	unsigned int low = 0;
	unsigned int high = num_lives_before_gc;
	while (low <= high) {
		unsigned int mid = (high + low) / 2;
		if (all_lives_before_gc[mid].p_obj == p_old) {
			// This object was moved during GC
			assert(all_lives_before_gc[mid].p_obj_moved != NULL);
			// only objects resident in compaction block actually move
			assert(p_global_gc->is_compaction_block(GC_BLOCK_INFO(p_old)));
			// A copy was made before GC started
			assert(all_lives_before_gc[mid].p_obj_copied != NULL);
			// DONE
			return all_lives_before_gc[mid].p_obj_copied;
		} else if ((POINTER_SIZE_INT) all_lives_before_gc[mid].p_obj <  (POINTER_SIZE_INT) p_old) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}

	assert((low > high) || ((low == high) && (all_lives_before_gc[low].p_obj != p_old)));
	printf("find_saved_away_copy_of_live_before_GC_started() -- CANT FIND LIVE OBJECT %p \n", p_old);
	assert(0);
	return NULL;
}


// first arg  -- Copy of object that was made before GC....in C malloc space and outside the heap
// second arg -- the object after GC (a pointer to it into the heap)


void verify_live_object_was_not_corrupted_by_gc(Partial_Reveal_Object *p_obj_before_gc, Partial_Reveal_Object *p_obj_after_gc) {
	// ZZZZZ
	assert(p_obj_before_gc != p_obj_after_gc);
	assert(p_obj_before_gc);
	assert(p_obj_after_gc);

	// VTable needs to be the same before and after GC, otherwise this is a corruption problem
	assert(p_obj_before_gc->vt() == p_obj_after_gc->vt());

	// Now verify the rest of the bytes....
	struct Partial_Reveal_VTable *obj_vt = p_obj_before_gc->vt();
	unsigned int obj_sz = get_object_size_bytes(p_obj_before_gc);
	assert(obj_sz == get_object_size_bytes(p_obj_after_gc));

	if (obj_vt->get_gcvt()->gc_object_has_slots) {

		// Either an array of references....or an object with reference slots...
		assert(!is_array_of_primitives(p_obj_before_gc));

		if (is_array(p_obj_before_gc)) {
			// This is an array of references....we will step through the array one slot at a time...

			// Make sure that array lengths match up...
			int32 array_length = vector_get_length_with_vt((Vector_Handle)p_obj_before_gc,p_obj_before_gc->vt());
			assert(array_length == vector_get_length_with_vt((Vector_Handle)p_obj_after_gc,p_obj_before_gc->vt()));

            for (int32 i= array_length - 1; i >= 0; i--) {
                Slot p_old_slot(vector_get_element_address_ref_with_vt((Vector_Handle)p_obj_before_gc, i, p_obj_before_gc->vt()), false);
                Slot p_new_slot(vector_get_element_address_ref_with_vt((Vector_Handle)p_obj_after_gc , i, p_obj_after_gc->vt()));
                if (p_old_slot.is_null()) {
                    assert(p_new_slot.is_null()) ;
                    continue;
                }
                // Non-NULL slot before GC
                if (p_old_slot.dereference() == p_new_slot.dereference()) {
                    // Slot was not updated or updated to the same value...it may have pointed to an object
                    // in a non-moving part of the heap or was in the moving part but did not move..
                    continue;
                }
                assert(p_global_gc->is_compaction_block(GC_BLOCK_INFO(p_new_slot.dereference())));
                // The types of the objects pointed to before and after GC from this slot need to be the same...
                Partial_Reveal_Object *p_saved_copy = find_saved_away_copy_of_live_before_GC_started(p_old_slot.dereference());
                assert(p_saved_copy->vt() == p_new_slot.dereference()->vt());
            } // for

		} else {
			// This is a regular object with reference slots...we need to step through here...
			assert(obj_vt->get_gcvt()->gc_number_of_slots > 0);

			uint8* p_byte_old = (uint8 *) ((POINTER_SIZE_INT)p_obj_before_gc + JAVA_OBJECT_OVERHEAD);
			uint8* p_byte_new = (uint8 *) ((POINTER_SIZE_INT)p_obj_after_gc + JAVA_OBJECT_OVERHEAD);
			uint8* p_search_end = (uint8 *) ((POINTER_SIZE_INT)p_obj_before_gc + obj_sz);

			unsigned int *old_obj_offset_scanner = init_object_scanner (p_obj_before_gc);
			unsigned int *new_obj_offset_scanner = init_object_scanner (p_obj_after_gc);

			while (true) {

				Slot p_old_slot(p_get_ref(old_obj_offset_scanner, p_obj_before_gc), false);
				Slot p_new_slot(p_get_ref(new_obj_offset_scanner, p_obj_after_gc));

				if (p_old_slot.get_value() == NULL) {
					// Went past the last reference slot in this object...
					assert(p_new_slot.get_value() == NULL);
					break;
				}

				// Skip and compare bytes till we hit the next reference slot..
				while (p_byte_old != (uint8*) p_old_slot.get_value()) {
					assert(*p_byte_old == *p_byte_new);
					p_byte_old++;
					p_byte_new++;
				}
				assert(p_byte_new == (uint8*) p_new_slot.get_value());

				// Now check the reference slots for integrity
				// Non-NULL slot before GC
				if (p_old_slot.dereference() != p_new_slot.dereference()) {
					assert(p_global_gc->is_compaction_block(GC_BLOCK_INFO(p_new_slot.dereference())));
					// The types of the objects pointed to before and after GC from this slot need to be the same...
					Partial_Reveal_Object *p_saved_copy = find_saved_away_copy_of_live_before_GC_started(p_old_slot.dereference());
					assert(p_saved_copy->vt() == p_new_slot.dereference()->vt());
				} else {
					// Slot was not updated or updated to the same value...it may have pointed to an object
					// in a non-moving part of the heap or was in the moving part but did not move..
				}
				// Move the scanners to the next reference slot
				old_obj_offset_scanner = p_next_ref (old_obj_offset_scanner);
				new_obj_offset_scanner = p_next_ref (new_obj_offset_scanner);

				// Move the byte pointers beyond the reference slot...
                p_byte_old = (uint8 *) ((POINTER_SIZE_INT) p_byte_old + Partial_Reveal_Object::vtable_bytes());
				p_byte_new = (uint8 *) ((POINTER_SIZE_INT) p_byte_new + Partial_Reveal_Object::vtable_bytes());

			} // while

			// We have gone past the last reference slot in this object

			// Check for equality the remaining bytes in this object..
			while (p_byte_old != (uint8 *) p_search_end) {
				assert(*p_byte_old == *p_byte_new);
				p_byte_old++;
				p_byte_new++;
			}
			assert(p_byte_new == (uint8 *) ((POINTER_SIZE_INT)p_obj_after_gc + obj_sz));
		}

	} else {
		// This is either an array of primitives or an object with no reference slots...
		assert(is_array_of_primitives(p_obj_before_gc) || (obj_vt->get_gcvt()->gc_number_of_slots == 0));

		if (obj_sz > JAVA_OBJECT_OVERHEAD) {
			// Need simple byte compare of rest of the bytes...regardless of what this is....
			if (memcmp(	(void *)((POINTER_SIZE_INT)p_obj_before_gc + JAVA_OBJECT_OVERHEAD),
						(void *)((POINTER_SIZE_INT)p_obj_after_gc  + JAVA_OBJECT_OVERHEAD),
						obj_sz - JAVA_OBJECT_OVERHEAD
						) != 0) {
				// BAD
				assert(0);
				printf("Object %p of type %s was corrupted by GC\n", p_obj_before_gc, p_obj_before_gc->vt()->get_gcvt()->gc_class_name);
				while(1);
			} // if

		} // if

	} // if

	// ALL DONE
	return;
}



void verify_live_heap_before_and_after_gc(unsigned int num_lives_after_gc, Object_List *all_lives_after_gc) {
	assert(num_lives_after_gc == num_lives_before_gc);
	// First, process all the repointing information collected during GC
	for (unsigned int j = 0; j < g_num_cpus; j++) {
		for (unsigned int k = 0; k < cursors[j]; k++) {
			insert_moved_reference(all_moved_lives_during_gc[j][k].p_obj, all_moved_lives_during_gc[j][k].p_obj_moved);
			assert(all_moved_lives_during_gc[j][k].p_obj_copied == NULL);
		}
	}

	// Lets move the "all_lives_after_gc" from a Object_List to a HashTable so that has a faster "exists" implementation
	// check for duplicates in the process....
	all_lives_after_gc->rewind();
	Partial_Reveal_Object *p_obj = NULL;
	Hash_Table *all_lives_after_gc_ht = new Hash_Table();

	while ((p_obj = all_lives_after_gc->next()) != NULL) {
		// It is not already present...
		assert(all_lives_after_gc_ht->is_not_present(p_obj));
		all_lives_after_gc_ht->add_entry(p_obj);
	}
	assert(all_lives_after_gc_ht->size() == all_lives_after_gc->size());

	// Now we can sit down and verify all the moved and non-moved objects and bit-level integrity
	for (unsigned int i = 0; i < num_lives_before_gc; i++) {
		// some object exists here....and it was copied properly before GC
		assert(all_lives_before_gc[i].p_obj);
		assert(all_lives_before_gc[i].p_obj_copied);

		if (all_lives_before_gc[i].p_obj_moved) {
			// Was in a compaction block
			assert(p_global_gc->is_compaction_block(GC_BLOCK_INFO(all_lives_before_gc[i].p_obj)));
			// Make sure that this object is still live after GC
			all_lives_after_gc_ht->is_present(all_lives_before_gc[i].p_obj_moved);
			// Compare object bytes with snapshot before GC began
			verify_live_object_was_not_corrupted_by_gc(all_lives_before_gc[i].p_obj_copied, all_lives_before_gc[i].p_obj_moved);
		} else {
			// Object was not moved...but it still needs to be live
			all_lives_after_gc_ht->is_present(all_lives_before_gc[i].p_obj);
			// Compare object bytes with snapshot before GC began...it is still in the same place...
			verify_live_object_was_not_corrupted_by_gc(all_lives_before_gc[i].p_obj_copied, all_lives_before_gc[i].p_obj);
		} // if
	} // for

	// Free the hash table that we just constructed...
	delete all_lives_after_gc_ht;

	// We need to free up the live heap that was malloced befored we leave...
	for (unsigned int x = 0; x < num_lives_before_gc; x++) {
		// some object exists here....and it was copied properly before GC
		assert(all_lives_before_gc[x].p_obj);
		assert(all_lives_before_gc[x].p_obj_copied);
		free(all_lives_before_gc[x].p_obj_copied);
	}

	printf("LIVE HEAP VERIFICATION COMPLETED WITH NO ERRORS FOUND!!\n");
}
