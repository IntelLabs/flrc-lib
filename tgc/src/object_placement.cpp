/*
 * COPYRIGHT_NOTICE_1
 */

// System header files
#include <iostream>
#include "stdio.h"

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
#include "tgc/mark.h"
#include "tgc/descendents.h"
#include "tgc/gc_debug.h"
#include "tgc/gcv4_synch.h"


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef GC_LIVE_OBJECT_LISTS_PER_COMPACTION_BLOCK


#ifndef GC_NO_FUSE

//
// Run through all the objects in the blocks assigned to this heap colocating the interesting ones.
// An object is interesting if there is some value to colocating it with another object and if
// they are not already colocated.
//
// For this version to avoid race conditions with other threads both objects being colocated need
// to be owned by this thread. In addition the referent must not have already been moved.
// We need to see if this is sufficient to colocate a wide number of objects.
//


// Given an object place move it to dest.

void insert_object_location(GC_Thread *gc_thread, void *dest, Partial_Reveal_Object *p_obj)
{
#ifdef ROTOR
    // adjusting for negative offset bytes in Rotor
    dest = (void*)(((Byte*)dest) + orp_get_amount_of_negative_space_before_vtable_ptr() + sizeof(Obj_Info_Type));
#endif //ROTOR

    assert ((p_obj->get_obj_info() & FORWARDING_BIT_MASK) == FORWARDING_BIT_MASK); // Make sure we have the forwarding bit.
    Obj_Info_Type orig_obj_info = p_obj->get_obj_info() & ~FORWARDING_BIT_MASK; // The original obj_info without the forwarding bit set.
    if (orig_obj_info) {
        // obj_info is not zero so remember it.
	    object_lock_save_info *obj_info = (object_lock_save_info *) malloc(sizeof(object_lock_save_info));
        if (!obj_info) {
            orp_cout << "Internal but out of c malloc space.\n" << std::endl;
        }
	    // Save what needs to be restored.
	    obj_info->obj_header = orig_obj_info;
	    // I need to keep track of the new after-slided address
	    obj_info->p_obj = (Partial_Reveal_Object *) dest;
	    gc_thread->insert_object_header_info_during_sliding_compaction(obj_info);
	    // problem_locks++; // placement code does not deal with this.
        gc_trace (p_obj, "Object being compacted or colocated needs obj_info perserved.");
    }

    assert ((p_obj->get_obj_info() & FORWARDING_BIT_MASK) == FORWARDING_BIT_MASK); // Make sure we have the forwarding bit.
    // clobber the header with the new address.
    assert (dest);
    assert (((POINTER_SIZE_INT)dest | FORWARDING_BIT_MASK) != (POINTER_SIZE_INT)dest);
    // This might break on Linux if the heap is in the upper 2 gig of memory. (high bit set)
    // If it does we need to change to the low bit.

    p_obj->set_forwarding_pointer(dest);
    gc_trace (p_obj, "Insert new object location for this object");

    gc_trace (dest, "Eventual object location installed.");
}


//
// This code breaks down into 3 pieces.
// First we build a queue of all objects that need to be colocated
//    For each object we grab the forwarding pointer using a lock instruction. This means we own it.
//    If we can't gain ownership to the base object we just return.
//    If we can't gain ownership to a single referent we don't colocate that referent
//    If we can't gain ownership to any referent we release ownership of the base object and return.
// Second we install the forwarding pointer in the base object.
// Third we go through the queue and install forwarding pointers in the remaining objects.
// We return true if we can colocate objects.
// No objects are actually moved, that is done later.
//
// Sliding compaction presents some problems when moving the objects. First the invariant that
// if we slide objects to the left we will always have room is no longer obvious since colocated
// objects are also interspersed. If I have dead A B C dead D E and I am colocating D and E after A
// Then I slide                               A  D E
// and I have overwritten B before I can move it into place.
// One way to deal with this is to move D and E to a seperate area until the entire block has been slid
//                                            A  x  x B                stash    D  E
// But this overwwrites C with A. So we need to stash B also.
//                                            A  x  x x C              stash    D E B
// Now we can move D E and B
//                                            A  D  E  B C
// This means that each block has a stash of up to an additional single block. This would seem to
// double the space requirements of this algorithm.
//
//
// Each GC thread has a stash list associated with each GC thread including itself. All objects
// that can not be immediately placed where they belong go onto this stash list. This introduces an
// additional synchronization point where all sliding is done and no futher objects will be placed in
// the stash blocks. After this synchronization point all stash blocks can be emptied.
// If we start with x A B C x x D E
// and we want      A D E B C x x x
//
// These are the steps.
//                   Stash
// x A B C x x D E                         Slide A
// A x x C x x D E        B                Stash B
// A x x x x x D E        B C              Stash C
// A D x x x x x E        B C              Move D
// A D E x x x x x        B C              Move E
// later we will move the stashed objects into place so we end up with
// A D E B C x x x
//

//
// The rule about whether something goes into the stash block or into the "to" block is simple.
// Each gc thread makes available its scanning pointer. If the target is to the left of the scan pointer
// then the object gets copied directly into its destination. If it is to the right it goes
// into the stash block. If there is not room in the stash block and we are out of stash blocks then
//

//
// If all objects that remain in a GCs area are always moved into another block then we don't have to worry
// about stashing these objects. If we also limit the number of cross thread objects to the amount of stash
// blocks available then we will never end up with the situation where we are out of stash blocks.
//

//
// What if we stash the collocaing objects at pointer creating time and then ignore them during sliding time.
// Finally we move them into place after the sliding.
//

//
// What if we smash the colocating if it means that we can't fit it into lower addresses than the next object
// that is to be slid. This means that the sliding will never have to deal with not having a place to slide
// an object. This is good. The invariant is that if an object must be slid to the right as a result of colocating
// then we do not colocate.
//

// *******************************************************
//
// Objects can be colocated or slid. An object that is to be colocated within the same area and colocation
// is to a lower address is considered a "sliding colocation." All other colocation are considered stash
// colocation.
//
// So we colocate up to the number of object we have stash blocks for.
// We colocate only if it does not cause an object to be slid to a "high" address.
//
// *******************************************************


// If two objects are in the same area then we may not want to fuse them. T
// SAME_AREA_SHIFT is what we shift 2 by to get the distance between objects that
// qualify them for being in the same area.

const int SAME_AREA_SHIFT = 16;
// Total Number of objects colocated by Delta Force.



static unsigned int delta_force_objects_colocated = 0;

unsigned int get_delta_force_objects_colocated ()
{
    unsigned int result = delta_force_objects_colocated;
    delta_force_objects_colocated = 0;
    return result;
}


//
// Input/Output next_obj_start_arg - a pointer to a pointer to where the
//  fused objects will eventually reside. Updated to the end of the fused
//  objects.
//
bool fuse_objects (GC_Thread *gc_thread,
                   Partial_Reveal_Object *p_obj,
                   void **next_obj_start_arg,
                   unsigned int *problem_locks)
{


    bool debug_printf_trigger = false;

    unsigned int            moved_count = 0;
    unsigned int            unmoved_count = 0;
    // If we can fuse an object we do and return it.
    assert (p_obj->vt()->get_gcvt()->gc_fuse_info);
    gc_trace (p_obj, "This object is a candidate for fusing with next object.");

    Partial_Reveal_Object   *scan_stack[MAX_FUSABLE_OBJECT_SCAN_STACK];
    unsigned                top = 0;

    Partial_Reveal_Object   *fuse_queue[MAX_FUSED_OBJECT_COUNT];
    unsigned                last = 0;

    bool                    all_fused = true;

    scan_stack[top++] = p_obj;
    unsigned int fused_size = get_object_size_bytes(p_obj);
    unsigned int base_obj_size = fused_size;

    void *to_obj = *next_obj_start_arg;
    void *debug_orig_to_obj = to_obj;

    // The object that is next to the base object in the "from" area. We need to make
    // sure that fusing objects does not result in overwriting this object. This object
    // is not fused, it is simply the next object in the "from" space.

    Partial_Reveal_Object *p_next_from_obj = p_obj + base_obj_size;

    // This object might have already been colocated. If so just return false. If not grab the forwarding bit
    // and claim the right and *obligation* to colocate this object. If this fails don't worry since it will be moved during sliding to the
    // correct place, just remember to release the fowarding bit.

    // Claim the Forwading bit if you can. If you loose the race you can't fuse since someone else is.
    Obj_Info_Type old_base_value = p_obj->get_obj_info();
    Obj_Info_Type new_base_value = old_base_value;
    if ((old_base_value & FORWARDING_BIT_MASK) == FORWARDING_BIT_MASK)  {
        return false; // Some other thread is going to move this object.
    }

    new_base_value = old_base_value | FORWARDING_BIT_MASK;

    if (p_obj->compare_exchange(new_base_value, old_base_value) != old_base_value) {
        // We did not get the forwarding pointer successfully, some other thread got it.
        // Since this is the base object we can just return false.
        return false;
    }

    // Build a queue of objects to colocate but do not grab the FORWARDING_BIT until the queue is built.
    while (top > 0) {
        Partial_Reveal_Object *p_cur_obj = scan_stack[--top];
        unsigned *offset_scanner = init_fused_object_scanner(p_cur_obj);
        Slot pp_next_object(NULL);
        Partial_Reveal_Object *p_last_object = p_obj;
        while (pp_next_object.set(p_get_ref(offset_scanner, p_cur_obj)) != NULL) {
            // Move the scanner to the next reference.
            offset_scanner = p_next_ref (offset_scanner);
            // This object is to be fused with the object located at the gc_fuse_info so calculate the required size.
            Partial_Reveal_Object *p_next_from_obj = pp_next_object.dereference();
            gc_trace (p_next_from_obj, "This object is a candidate to be fused with previous object.");

            if (p_next_from_obj) {
                // Check NULL.
                block_info *fuse_block_info = GC_BLOCK_INFO(p_next_from_obj);
                void * next_natural_obj = (void *) (POINTER_SIZE_INT(p_last_object) + get_object_size_bytes(p_last_object));
                Obj_Info_Type new_value = p_next_from_obj->get_obj_info();
                Obj_Info_Type old_value = new_value;
                bool is_colocation_natural = (next_natural_obj == (void *)p_next_from_obj);
                bool overflow = (((POINTER_SIZE_INT)to_obj + fused_size + get_object_size_bytes(p_next_from_obj)) > (POINTER_SIZE_INT)(GC_BLOCK_CEILING(to_obj)));

                bool already_forwarded = ((new_value & FORWARDING_BIT_MASK) == FORWARDING_BIT_MASK);
                bool in_compaction_block = gc_thread->_p_gc->is_compaction_block(fuse_block_info);
                // Don't colocate if both objects are in the same block.
                bool both_in_same_page = ( ((POINTER_SIZE_INT)p_next_from_obj >> SAME_AREA_SHIFT) == ((POINTER_SIZE_INT)p_cur_obj >> SAME_AREA_SHIFT) );

                bool can_fuse = ((!already_forwarded)
                    && (!is_colocation_natural)
                    && (!overflow)
                    && in_compaction_block
                    // && !both_in_same_page
                    );

                if (can_fuse){
                    if (p_next_from_obj->vt()->get_gcvt()->gc_fuse_info) {
                        scan_stack[top++] = p_next_from_obj;
                    }

                    fuse_queue[last] = p_next_from_obj;

                    fused_size += get_object_size_bytes(p_next_from_obj);
                    last++;
                } else {
                    p_obj->set_obj_info(old_base_value); // Release the forwarding bit and don't colocate this object.
#if 0
                    // No objects to relocate.
                    if (already_forwarded) {
                        printf ("F");
                    }else if (is_colocation_natural) {
                        printf ("N");
                    }else if (overflow) {
                        printf("O");
                    }else if (!in_compaction_block) {
                        printf ("C");
                    }else {
                        printf ("             xxxxxxxxxxxxxxxxxxx              ");
                    }

#endif
                     return false;
                }
            }
        }
    }

    unsigned i;
    // Grab the forwarding bits for the other object in the queue.. If you can't get a bit
    // remove the object from the queue.
    for (i = 0; i < last; i++) {
        Partial_Reveal_Object *p_fuse_obj = fuse_queue[i];
        Obj_Info_Type new_value = p_fuse_obj->get_obj_info();
        Obj_Info_Type old_value = new_value;
        bool already_forwarded = ((new_value & FORWARDING_BIT_MASK) == FORWARDING_BIT_MASK);
        new_value = old_value | FORWARDING_BIT_MASK; // Create the value with a the forwarding bit set.
        if (!already_forwarded) {
            // install the forwarding bit if it has not already been forwarded.
            already_forwarded = (p_fuse_obj->compare_exchange(new_value, old_value) != old_value);
        }

        if (already_forwarded) {

            debug_printf_trigger = true;
//            printf ("REMOVING FROM FUSE QUEUE.\n");
            // Remove this object from the queue since we can colocate it.
            unsigned int j;
            for (j = i; j < last - 1; j++) {
                fuse_queue[j] = fuse_queue[j+1];
            }
            // We have one less object on the queue.
            fuse_queue[last] = NULL;
            last--;
            i--; // Redo since fuse_queue[i] now holds a new object.
            unmoved_count++;
        }
        gc_trace (p_fuse_obj, "No space so this object is not fused with parent.");
    }

    // We don't fuse more than a single block worth of objects.
    assert (fused_size <= GC_BLOCK_ALLOC_SIZE);
    // We own all the forwarding bits in all the objects in the fuse_queue.

    // If we only have the base object and no other object to colocate with it just return.
    if (last == 0) {
        p_obj->set_obj_info(old_base_value); // Release the forwarding bit and don't colocate this object.
        // No objects to relocate.
//        printf("3");
        return false;
    }

    // At this point all objects in the queue will be fused, we have the forwarding bits
    // so we now figure out where they will be colocated.

    gc_trace (p_obj, "Fusing this object with offspring.");
    Partial_Reveal_Object *p_old_start = p_obj;
    assert ((POINTER_SIZE_INT)(GC_BLOCK_INFO (to_obj + get_object_size_bytes(p_obj) - 1)) <= (POINTER_SIZE_INT)(GC_BLOCK_CEILING(to_obj)));
    assert ((p_obj->get_obj_info() & FORWARDING_BIT_MASK) == FORWARDING_BIT_MASK);

    if (object_header_is_non_zero(p_obj)) {
            if ((p_obj->get_obj_info() & ~FORWARDING_BIT_MASK) != 0) {
                object_lock_save_info *obj_info = (object_lock_save_info *) malloc(sizeof(object_lock_save_info));
                assert(obj_info);
                // Save what needs to be restored.
                obj_info->obj_header = object_header_save_word(p_obj);
                obj_info->obj_header = obj_info->obj_header & ~FORWARDING_BIT_MASK; // Clear forwarding bit.
                // I need to keep track of the new after-slided address
                obj_info->p_obj = (Partial_Reveal_Object *) to_obj;
                gc_thread->insert_object_header_info_during_sliding_compaction(obj_info);
                *problem_locks = *problem_locks + 1;; // placement code does not deal with this so this is likely to be wrong.
                gc_trace (p_obj, "Object being fused needs obj_info preserved.");
                debug_printf_trigger = true;
                printf ("preserving base fused object header\n");
            }
        }

    // Finally deal with this placement, moving the base object first.
    insert_object_location (gc_thread, to_obj, p_obj);
    gc_trace (to_obj, " In allocate_forwarding_pointers_for_compaction_live_objects forwarding *to* this location. (vtable not yet legal)");
    gc_trace(p_obj, " was forwarded...\n");
    if (verify_live_heap) {
        add_repointed_info_for_thread(p_obj, (Partial_Reveal_Object *) to_obj, gc_thread->get_id());
    }
    if (delta_dynopt && mississippi_delta->object_is_delinquent_instance(p_obj)) {
            // register with delta that delinquent instance is being moved.
            // Needs to be done before object header is clobbered with the forwarding pointer
            mississippi_delta->record_moved_delinquent_instance(p_obj, (Partial_Reveal_Object *) to_obj, gc_thread->get_id());
    }
    assert (base_obj_size == get_object_size_bytes(p_obj));
    to_obj = (void *) ((POINTER_SIZE_INT) to_obj + base_obj_size);

    // Now figure out where the referent objects belong and set up their forwarding pointers.

    for (i = 0; i < last; i++) {
        Partial_Reveal_Object *p_fuse_obj = fuse_queue[i];
        unsigned int fused_obj_size = get_object_size_bytes(p_fuse_obj);
        gc_trace (p_fuse_obj, "Fusing this object with parent.");
        // Finally deal with this colocations.
        assert (p_fuse_obj != p_obj); // Nulls should have been filtered out up above.

        if (object_header_is_non_zero(p_fuse_obj)) {
            if ((p_fuse_obj->get_obj_info() & ~FORWARDING_BIT_MASK) != 0) {
                object_lock_save_info *obj_info = (object_lock_save_info *) malloc(sizeof(object_lock_save_info));
                assert(obj_info);
                // Save what needs to be restored.
                obj_info->obj_header = object_header_save_word(p_fuse_obj);
                obj_info->obj_header = obj_info->obj_header & ~FORWARDING_BIT_MASK; // Clear forwarding bit.
                // I need to keep track of the new after-slided address
                obj_info->p_obj = (Partial_Reveal_Object *) to_obj;
                gc_thread->insert_object_header_info_during_sliding_compaction(obj_info);
                *problem_locks = *problem_locks + 1;; // placement code does not deal with this so this is likely to be wrong.
                gc_trace (p_fuse_obj, "Object being fused needs obj_info preserved.");
                debug_printf_trigger = true;
                printf ("preserving fused object header\n");
            }
        }

        // Counts are not thread safe but it is just an approximation....
        delta_force_objects_colocated++;
        moved_count++;

        // The object in the queue its forwarding bit set.
        {
            POINTER_SIZE_INT next_available = (POINTER_SIZE_INT)to_obj + get_object_size_bytes(p_fuse_obj) -1;
            assert ((fuse_queue[i]->get_obj_info() & FORWARDING_BIT_MASK) == FORWARDING_BIT_MASK);
            assert (next_available <=  ((POINTER_SIZE_INT)(GC_BLOCK_CEILING(to_obj))));
        }
        insert_object_location(gc_thread, to_obj, p_fuse_obj);
        gc_trace (to_obj, " In allocate_forwarding_pointers_for_compaction_live_objects forwarding *to* this location. (vtable not yet legal)");
        gc_trace(p_obj, " was forwarded...\n");
        if (verify_live_heap) {
            add_repointed_info_for_thread(p_fuse_obj, (Partial_Reveal_Object *) to_obj, gc_thread->get_id());
        }

        if (delta_dynopt && mississippi_delta->object_is_delinquent_instance(p_fuse_obj)) {
            // register with delta that delinquent instance is being moved.
            // Needs to be done before object header is clobbered with the forwarding pointer
            mississippi_delta->record_moved_delinquent_instance(p_fuse_obj, (Partial_Reveal_Object *) to_obj, gc_thread->get_id());
        }
        to_obj = (void *) ((POINTER_SIZE_INT) to_obj + fused_obj_size);
    }

    *next_obj_start_arg = to_obj; // Update and return.
    if (debug_printf_trigger) {
        printf ("next_obj_start_arg addr: %p, old_val %p, new_val %p\n", next_obj_start_arg, debug_orig_to_obj, to_obj);
    }
#if 0
    if (unmoved_count) {
        printf("-%d+%d",unmoved_count, moved_count);
    }else{
        printf("+%d",moved_count);
    }
#endif
    return true;
}


#ifdef GC_PREFETCH_CHAIN

#if 1
void prefetch_trace (char *output)
{
    orp_cout << output << std::endl;
}
#else
inline prefetch_trace (char *output)
{

}
#endif

// The depth to search the type graph.
const int SEARCH_DEPTH = 2;

//
// Builds a tree of SEARCH_DEPTH of all types (vtables) reachable by this type.
// Each node has an offspring field which points to a NULL terminated list of nodes.
// Each node links to sibling types which are types of fields in the parent node.
//
struct type_node;

typedef struct type_node {
    Partial_Reveal_VTable *vt;
    type_node *parent;
    unsigned int field_offset;
    char *field_name;
    type_node *sibling;
    type_node *offspring;
    type_node *next;                // Links together interesting types.
    type_node *path;
    char *path_field_name;
    type_node *d_chain;             // List of delinquent types in a delinquent chain
    unsigned int d_chain_distance;  // The number of links from the base to reach this d_chain
	char *class_name;				// For debugging and testing this seems worth while.
} type_node;

// Print routines to display a type graph.
// Forward refs.

void print_node (type_node *node, unsigned int depth)
{
    unsigned int i = 0;
    for (i = 0; i < depth; i++) {
        printf("  ");
    }
    printf ("Type is  %s \n", node->class_name);
}

// Forward ref.
void print_graph (type_node *graph, unsigned int depth);

void print_siblings (type_node *graph, unsigned int depth)
{
    if (!graph) {
        return;
    }
    type_node *child = graph;
    while (child) {
        char *name = child->field_name;
        if (!name) {
            name = "*unknown_name*";
        }
        printf ("The field %s.\n", child->field_name);
        print_graph (child, depth);
        child = child->sibling;
    }
    return;
}

void print_graph (type_node *graph, unsigned int depth)
{
    if (!graph) {
        return;
    }
    if (depth > SEARCH_DEPTH) {
        return;
    }
    print_node (graph, depth);
    print_siblings (graph->offspring, depth+1);
}

void print_type_graph (type_node *graph)
{
	if (!graph) {
		return;
	}
	printf ("Type Graph for root %s\n", graph->class_name);
    print_graph (graph, 0);
}

void print_type_chain (type_node *chain, unsigned int depth)
{
    if (!chain) {
        return;
    }
    print_node (chain, depth);
    print_type_chain (chain->next, depth+1);
}



void print_type_d_chain (type_node *chain, unsigned int depth)
{
    if (!chain) {
        return;
    }
    print_node (chain, chain->d_chain_distance);
    printf ("d_chain_distance is %u\n", chain->d_chain_distance);
    print_type_d_chain (chain->d_chain, depth);
}


void print_type_path_chain (type_node *chain, unsigned int depth)
{
    if (!chain) {
        return;
    }

    if (chain->path_field_name) {
        printf ("    using field %s\n", chain->path_field_name);
    }
    print_node (chain, depth);
    print_type_path_chain (chain->path, depth);
}


//
// It is what it is, same member function I wrote in Lisp so many years ago.
// Assumes nodes are linked through next field.
// I don't think this is used for now, since I use the type_graph_member code instead. RLH.
//

boolean type_list_member (type_node *types, Partial_Reveal_VTable *vt)
{
    if (!types) {
        return false;
    }
    if (vt == types->vt) {
        return true;
    }
    return type_list_member (types->next, vt);
}

boolean chain_all_delinquent (type_node *a_chain, type_node *all_nodes)
{
    if (a_chain == NULL) {
        return true;
    }

    if (!type_list_member (all_nodes, a_chain->vt)) {
        // We have a node that is not a delinquent type return false.
        return false;
    }
    // Check the rest of the list.

    return chain_all_delinquent (a_chain->path, all_nodes);;
}

//
// Given the type structure see if this vt is in that type structure up to a
// depth of depth. If it is return the path to the type specified by vt.


//
//  Input
//    vt is the type you are looking for.
//    node is the root to start looking at.
//    depth is the depth to look
//    skip_count is the number of paths to skip before you return the next path.
//  Output
//    a path to the node of the type vt.
//


type_node *type_graph_member_path (Partial_Reveal_VTable *vt, type_node *node, unsigned int depth, unsigned int *skip_count)
{
    if (vt == node->vt) {
        node->path = NULL;
        // Should this be a subtype check instead of an equality check?
        return node;
    }
    if (depth == SEARCH_DEPTH) {
        return NULL;
    }
    // Check each of the siblings
    type_node *sibling = node->offspring;
    while (sibling) {
        type_node *result = type_graph_member_path (vt, sibling, (depth + 1), skip_count);
        if (result) {
            if ((*skip_count) == 0) {
                // Return this path.
                result->path_field_name = sibling->field_name;
                node->path = result;
                return node;
            } else {
                // decrement skip count and go for next path.
                *skip_count = *skip_count - 1;
            }
        }
        sibling = sibling->sibling;
    }
    return 0;
}

//
// Given the type structure see if this vt is in that type structure up to a
// depth of depth. This is a simple depth first traveral.
// If the type is not present it returns 0;
// Otherwise it returns the number of links away the type is.

unsigned int type_graph_member_depth (Partial_Reveal_VTable *vt, type_node *node, unsigned int depth)
{
    if (vt == node->vt) {
        // Should this be a subtype check instead of an equality check?
        return depth;
    }
    if (depth == SEARCH_DEPTH) {
        return 0;
    }
    // Check each of the siblings
    type_node *sibling = node->offspring;
    while (sibling) {
        unsigned int result = type_graph_member_depth (vt, sibling, (depth + 1));
        if (result) {
            return result;
        }
        sibling = sibling->sibling;
    }
    return 0;
}

unsigned int type_node_length (type_node *nodes)
{
    unsigned int length = 0;
    while (nodes) {
        length++;
        nodes = nodes->next;
        if (length > 100) {
            assert (0);
        }
    }
    return length;
}


unsigned int d_chain_length (type_node *nodes)
{
    unsigned int length = 0;
    while (nodes) {
        length++;
        nodes = nodes->d_chain;
        if (length > 100) {
            assert (0);
        }
    }
    return length;
}
//
// Given a base type figure and a list of delinquent types create a delinquent chain.
//

type_node *build_d_chain (type_node *base, type_node *interesting_types)
{
    type_node *result = NULL;
    type_node *temp = NULL;
    if (interesting_types == NULL) {
        return NULL;
    }
    if (base == interesting_types) {
        // skip the base
        return build_d_chain (base, interesting_types->next);
    }
    unsigned int depth = 1;
    unsigned int member_depth = type_graph_member_depth (base->vt, interesting_types, depth);
    if (member_depth) {
        result = interesting_types;
        type_node *rest = build_d_chain (base, interesting_types->next);
        result->d_chain = rest;
        result->d_chain_distance = member_depth - 1; // if it is a recursive type this is 0, 1 away it is 1.
//        orp_cout << "In build_chain I found graph member number " << d_chain_length(result) << std::endl;
        return result;
    }
    return build_d_chain (base, interesting_types->next);
}

// Returns the number of interesting types that are reachable from this vt.
unsigned int member_count (Partial_Reveal_VTable *vt, type_node *interesting_types)
{
    unsigned int result = 0;
    type_node *temp_list = interesting_types;
    while (temp_list) {
        if (type_graph_member_depth (vt, temp_list, 0)) {
//            orp_cout << "member_count finds a hit." << std::endl;
//            print_node (temp_list, result);
            result++;
        }
        temp_list = temp_list->next;
    }
//    orp_cout << "member_count result is " << result << std::endl;
    return result;
}

//
// Given a list of interesting types and a single vt find the interesting type that
// has the largest number of subtypes.
//

type_node *max_count (type_node *interesting_types)
{
//    orp_cout << "Max count length is " << type_node_length(interesting_types) << std::endl;
    type_node *bubble = interesting_types;
    type_node *all = interesting_types;
    unsigned int bubble_count = member_count(all->vt, all);
    type_node *rest = all->next;
    while (rest) {
        if (bubble_count < member_count (bubble->vt, rest)) {
            bubble = rest;
        }
        rest = rest->next;
    }
    return bubble;
}

//
// Given a list of interesting types linked useing the next field
// delete this node and return the list.
//

type_node *remove_node (type_node *node, type_node *list)
{
    type_node *result = list;
    if (node == list) {
        return node->next;
    }
    // This will blow up if the node is not a member of the list.
    result->next = remove_node(node, list->next);
    return result;
}

//
// Boring n**2 bubble sort for types based upon other reachable types.
//
type_node *sort_interesting_types (type_node *interesting_types)
{
    if (!interesting_types) {
//        orp_cout << "Result length from sort is " << 0 << std::endl;
        return NULL;
    }
    // Grab the type with the most instances of delinquent types.
    type_node *result = max_count (interesting_types);
    // Remove it from the list.
    type_node *rest = remove_node (result, interesting_types);
    // recurse to sort what is remaining and link it to the max.
    result->next = sort_interesting_types(rest);
    // Return the result
//    orp_cout << "Result length from sort is " << type_node_length(result) << std::endl;
    return result;
}

//
// Construct the class graph consisting of type_nodes, do it up to a depth of SEARCH_DEPTH
//

type_node *build_class_graph(Partial_Reveal_VTable *formal_vt, unsigned int depth)
{
//    orp_cout << "building a class graph at depth" << depth << std::endl;
    VTable_Handle vt = (VTable_Handle) formal_vt;
    Class_Handle cl = formal_vt->gc_clss;
    if (depth > SEARCH_DEPTH) {
        return NULL;
    }
    type_node *result = (type_node *) malloc (sizeof(type_node));
    memset (result, 0, sizeof(type_node)); // Clear everything.
    result->vt = formal_vt;
    unsigned int num_fields = class_num_instance_fields_recursive (cl);
//    orp_cout << "This type has num_fields == " << num_fields << std::endl;
    unsigned int i = 0;
    for (i = 0; i < num_fields; i++) {
        Field_Handle field = class_get_instance_field_recursive(cl, i);
        if (field_is_reference(field)) {
//            orp_cout << "We have a reference field at depth " << depth << std::endl;
            type_node *offspring =
                build_class_graph ((Partial_Reveal_VTable *)class_get_vtable(field_get_class_of_field_value(field)), (depth+1));
            if (offspring) {
//                orp_cout << "offspring is class " << offspring->class_name << std::endl;
                offspring->sibling = result->offspring;
                result->offspring = offspring;
                offspring->parent = result;
                offspring->field_offset = field_get_offset(field);
                offspring->field_name = (char *)field_get_name(field);
//                printf ("Parent %s has field named %s at offset %u \n", offspring->parent->class_name, offspring->field_name, offspring->field_offset);
            }
        }
    }
	result->class_name = (char *)class_get_name(cl);
//    orp_cout << "done with class " << result->class_name << std::endl;
	return result;
}

void free_class_graph (type_node *node)
{
    orp_cout << "free_class_graph not implemented in object_placement line 372." << std::endl;
    // TBD....
}



//
// I think these are the magnificent 7. Figure out how to get their vtables.
// Call build_class_graph on each of them and then link them into a list
// using the ->next field.
// Sort this list. The first item on the list should be the most interesting
// base type.
// This list will include several different delinquent chains. We need
// to distinguish which type participate in which chains.
//

type_node *fake_delinquent_types_input ()
{
    /*

        spec/jbb/Orderline
        spec/jbb/Stock
        spec/jbb/Item
        java/lang/String
        spec/jbb/infra/Collections/longBTreeNode
        spec/jbb/infra/Util/DisplayScreen
        ]C


        ]J
        ]Object
        */

    orp_cout << "Fake delinquent types input is beginning." << std::endl;

    Loader_Exception lexc;
    void *class_handle;

    char *class_str = "java/lang/String";
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "java/lang/String", &lexc);
    if (class_handle) {
        printf ("1 class loaded %s\n", class_str);
    }else {
        printf ("No class loaded %s", class_str);
    }

    class_str = "spec/jbb/Stock";
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "spec/jbb/Stock", &lexc);

    if (class_handle) {
        printf ("2 class loaded %s\n", class_str);
    }else {
        printf ("No class loaded %s", class_str);
    }

    class_str = "spec/jbb/Item";
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "spec/jbb/Item", &lexc);

    if (class_handle) {
        printf ("3 class loaded %s\n", class_str);
    }else {
        printf ("No class loaded %s", class_str);
    }

    class_str = "spec/jbb/infra/Collections/longBTreeNode";
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "spec/jbb/infra/Collections/longBTreeNode", &lexc);

    if (class_handle) {
        printf ("4 class loaded %s\n", class_str);
    }else {
        printf ("No class loaded %s", class_str);
    }

    class_str = "[C";
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "[C", &lexc);

    if (class_handle) {
        printf ("6 class loaded %s\n", class_str);
    }else {
        printf ("No class loaded %s", class_str);
    }

    class_str = "spec/jbb/Orderline";
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "spec/jbb/Orderline", &lexc);

    if (class_handle) {
        printf ("7 class loaded %s\n", class_str);
    }else {
        printf ("No class loaded %s\n", class_str);
    }

    class_str = "spec/infra/Util/DisplayScreen";
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "spec/infra/Util/DisplayScreen", &lexc);

    if (class_handle) {
        printf ("5 class loaded %s\n", class_str);
    } else {
        printf ("No class loaded %s\n", class_str);
    }



    class_str = "[J";
//    printf ("-------- Trying [J ----------");
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "[J", &lexc);

    if (class_handle) {
        printf ("8 class loaded %s\n", class_str);
    }else {
        printf ("No class loaded %s", class_str);
    }

//    printf ("------------- Trying [Ljava/lang/Object ------------");
    class_str = "[java/lang/Object";
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "[Ljava/lang/Object", &lexc);

    if (class_handle) {
        printf ("9 class loaded %s\n", class_str);
    }else {
        printf ("No class loaded %s", class_str);
    }



// END VALIDATION CODE





    class_handle = class_load_class_by_name_using_system_class_loader(
                   "spec/jbb/Orderline", &lexc);
    assert (class_handle);
//    orp_cout << "Exiting class_load_class_by_name_using_system_class_loader" << std::endl;

    type_node *head = build_class_graph ((Partial_Reveal_VTable *)class_get_vtable(class_handle), 0);
//    print_type_graph (head);

    class_handle = class_load_class_by_name_using_system_class_loader(
                   "spec/jbb/Stock",&lexc);
    assert (class_handle);
    type_node *temp = build_class_graph ((Partial_Reveal_VTable *)class_get_vtable(class_handle), 0);
    temp->next = head;
    head = temp;
//    print_type_graph (head);


//    orp_cout << "----------------------------------------------------2" << std::endl;
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "spec/jbb/Item" ,&lexc);
    assert (class_handle);
    temp = build_class_graph ((Partial_Reveal_VTable *)class_get_vtable(class_handle), 0);
    temp->next = head;
    head = temp;
//    print_type_graph (head);


//    orp_cout << "-------------------------------3" << std::endl;
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "spec/jbb/infra/Collections/longBTreeNode", &lexc);
    assert (class_handle);
    temp = build_class_graph ((Partial_Reveal_VTable *)class_get_vtable(class_handle), 0);
    temp->next = head;
    head = temp;
//    print_type_graph (head);
#if 0
    orp_cout << "--------------------------------------------4" << std::endl;
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "spec/infra/Util/DisplayScreen", &lexc);
    temp = build_class_graph ((Partial_Reveal_VTable *)class_get_vtable(class_handle), 0);

    temp->next = head;
    head = temp;
    print_type_graph (head, 0);
#endif

//    orp_cout << "-------------------------5" << std::endl;
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "java/lang/String", &lexc);
    assert (class_handle);
    temp = build_class_graph ((Partial_Reveal_VTable *)class_get_vtable(class_handle), 0);

    temp->next = head;
    head = temp;
//    print_type_graph (head);

//    orp_cout << "--------------------------------6" << std::endl;
#if 0
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "[C", &lexc);
    assert (class_handle);
    temp = build_class_graph ((Partial_Reveal_VTable *)class_get_vtable(class_handle), 0);
    temp->next = head;
    head = temp;
//    print_type_graph (head);

#endif

//    orp_cout << "--------------------------------6" << std::endl;
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "[J", &lexc);
    assert (class_handle);
    temp = build_class_graph ((Partial_Reveal_VTable *)class_get_vtable(class_handle), 0);
    temp->next = head;
    head = temp;
//    print_type_graph (head);


//    orp_cout << "--------------------------------6" << std::endl;
    class_handle = class_load_class_by_name_using_system_class_loader(
                   "[Ljava/lang/Object", &lexc);
    assert (class_handle);
    temp = build_class_graph ((Partial_Reveal_VTable *)class_get_vtable(class_handle), 0);
    temp->next = head;
    head = temp;
//    print_type_graph (head);


//    orp_cout << "Fake delinquent types input is done, chain is " << std::endl;
//    print_type_chain (head, 0);
//    orp_cout << "-- end of original chain -- " << std::endl;
    return head;
}

void prefetch_chain_driver ()
{
    type_node *d_types = fake_delinquent_types_input();
    type_node *d_types_sorted = sort_interesting_types(d_types);
    type_node *all_nodes = d_types_sorted;
    unsigned int length = type_node_length(all_nodes);
#if 0
    orp_cout << "Length of all nodes is " << length << std::endl;
    printf ("Here are the delinquent chains that I have discovered.\n");
    while (d_types_sorted) {
        printf ("--|---|-- The first type is reachable from the following\n");
        d_types_sorted->d_chain = NULL; // Clear the chain from previous iterations. THIS NEEDS TO BE MORE ROBUST...
        print_node (d_types_sorted, 0);
        type_node *a_chain = build_d_chain(d_types_sorted, all_nodes);
        print_type_d_chain (a_chain, 1);
        d_types_sorted = d_types_sorted->next;
        printf ("---\n");
    }

    d_types_sorted = all_nodes;
#endif

    printf ("================= Now the paths ========== Searching up to a depth of %u \n", SEARCH_DEPTH);
    // Each time we find a chain we print it out and then skip it on the next search for a chain.
    // temp_skip_count is decremented each time we find a chain until it reaches 0 by the type_graph_member_path code.
    unsigned int total_paths_found = 0;
    unsigned int pruned_paths = 0;
    unsigned int chain_skip_count;
    unsigned int temp_skip_count;
    while (d_types_sorted) {
        printf ("-------vvvvvvvvvvvv---  Paths ending at %s \n", d_types_sorted->class_name);
        type_node *this_node = all_nodes;
        while (this_node) {
            printf ("--- %s <-(from)- %s \n", d_types_sorted->class_name, this_node->class_name);
            d_types_sorted->path = NULL; // Clear the chain from previous iterations. THIS NEEDS TO BE MORE ROBUST...
            d_types_sorted->path_field_name = NULL;
            boolean more_chains;
            more_chains = true;
            chain_skip_count = 0;
            while (more_chains) {
                temp_skip_count = chain_skip_count; // Skip this many paths and return the following path
                type_node *a_chain = type_graph_member_path (d_types_sorted->vt, this_node, 0, &temp_skip_count);
                if (a_chain) {
                    if (a_chain->path){

                            printf ("-v- \n");
                            print_type_path_chain (a_chain, 1);

                            if (!chain_all_delinquent (a_chain, all_nodes)) {
                                printf ("------------ Pruned chain has non-delinquent member. ------------");
                                pruned_paths++;
                            }
                            printf ("-^-\n");
                            chain_skip_count++;
                    } else {
                        more_chains = false;
                    }
                } else {
                    more_chains = false;
                    temp_skip_count = chain_skip_count + 1;
                    if (type_graph_member_path (d_types_sorted->vt, this_node, 0, &temp_skip_count)) {
                        printf ("Found additional chain when none was expected************* bummer \n");
                    }
                }

            }
            printf ("--- %u paths to %s from %s.\n", chain_skip_count, d_types_sorted->class_name, this_node->class_name);
            total_paths_found += chain_skip_count;
            this_node = this_node->next;
        }
        d_types_sorted = d_types_sorted->next;
        printf ("-------^^^^^^^^^^^^---\n");
    }

    if (type_node_length(all_nodes) != length) {
        printf ("************ Length has changed ************** ");
    }

    printf ("We have found a total of %u paths between these types.\n", total_paths_found);
    printf ("We pruned %u due to non-delinquent members.\n", pruned_paths);

    printf ("********************* all done dumping information just bail for now  *******************");
    exit(17030);
}

#endif // GC_PREFETCH_CHAIN


#endif // GC_NO_FUSE


#endif // GC_LIVE_OBJECT_LISTS_PER_COMPACTION_BLOCK
