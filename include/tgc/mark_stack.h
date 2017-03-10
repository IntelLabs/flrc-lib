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

#ifndef _MARK_STACK_H
#define _MARK_STACK_H

//
// Provides the two routines needed to support an mark_stack. 
// mark_stacks are not synchronized and support a single
// writer and a single readers but they can access the mark_stack concurrently
// on the IA32.
//

// This can be reduced or increased. We made it this size so that 
// for IA32 it basically takes up a single page. This is why we have the - 6 which
// takes into account the other fields in the mark stack bucket.

// Buffers can be removed if there is more than one of them. They can be added when
// a thread is through helping out the primary marker. Ultimately it is up to 
// the primary marker pop the elments off the stack.

const int FIELDS_IN_MARK_STACK = 4;
const int SLOT_IN_MARK_STACK = 1024 * 8;
const int MARK_STACK_ELEMENTS = (SLOT_IN_MARK_STACK - FIELDS_IN_MARK_STACK);

// An mark_stack is a chained list of mark_stack buffers, each of holding mark_stack_SIZE entries.
typedef struct mark_stack {
    Partial_Reveal_Object    **p_top_of_stack;       // points to top of stack                    (producer/consumer modifies)
    mark_stack               *next;
    mark_stack               *previous;
    Partial_Reveal_Object    *data[MARK_STACK_ELEMENTS];    // array of objects
    POINTER_SIZE_INT         ignore;           // Location 1 past the data, 
                                               //just so we never point outside this structure with p_top_of_stack++.
} mark_stack;


//
// An mark_stack_container provides safe support for a single reader (consumer) 
// and a single writer (producer). The fielders the reader can modify
// are mark_stack_read_ptr current_mark_stack. It can read the data at *mark_stack_read_ptr.
// 
// The write (consumer) modifies to the data array, *mark_stack_write_ptr, first_mark_stack and
// last_mark_stack. It can only write to the last_mark_stack. 

// For IA64 all the fields are volatile which means that a st.rel will be used
// when updating any of these fields. This will make sure that the reader will see
// what the writer has written.
typedef struct mark_stack_container {    
    // Each mark_stack container has an mark_stack pointer. The first slot in an mark_stack holds the next mark_stack
    // or NULL if this is the last mark_stack associated with this thread. These are void *
    // to simplify interface needed by the dll.
    // a pointer to a slot which is a pointer to a pointer, thus the ***
    // Modified by the writer.
    mark_stack    *first_mark_buffer;      // First mark_stack associated with this thread or NULL if none. 
    mark_stack    *active_mark_buffer;        // The mark_stack associated with this that is active.
    mark_stack    *last_mark_buffer;        // Last mark_stack associated with this thread of NULL if none.
} mark_stack_container;

// The recommended use here is to have the container and first mark stack either in some thread local
// area or on the stack. The hope is that there will be few calls to these routines since the stack allocated
// mark_stack will be big enough to deal with all normal case situations.
//

// 
// Creates a new mark_stack - if this pops up on vtune then increase the size of the mark stack data.
//
inline static void add_mark_stack(mark_stack_container *a_mark_stack_container)
{
    mark_stack *new_mark_stack = (mark_stack *)malloc(sizeof(mark_stack));
    new_mark_stack->next = NULL;
    new_mark_stack->p_top_of_stack = &(new_mark_stack->data[0]);
    assert (a_mark_stack_container->last_mark_buffer->next == NULL);
    a_mark_stack_container->last_mark_buffer->next = new_mark_stack;
    new_mark_stack->previous = a_mark_stack_container->last_mark_buffer;
    a_mark_stack_container->last_mark_buffer = new_mark_stack;
    return;
};

inline static void release_empty_mark_stack(mark_stack *the_mark_stack)
{
    /*
    free(the_mark_stack);
    */
}

/* Initialize a stack mark_stack_container and its first mark stack. */
inline void mark_stack_init (mark_stack_container *a_mark_stack_container, mark_stack *a_mark_stack)
{
    a_mark_stack->next = NULL;
    a_mark_stack->previous = NULL;
    a_mark_stack->p_top_of_stack = &a_mark_stack->data[0];

    a_mark_stack_container->first_mark_buffer = a_mark_stack;
    a_mark_stack_container->last_mark_buffer = a_mark_stack;
    a_mark_stack_container->active_mark_buffer = a_mark_stack;
}

// 
// release all but the first mark stack from the container.
//
inline void mark_stack_cleanup (mark_stack_container *a_mark_stack_container)
{
    mark_stack *stale_extra_mark_stack = a_mark_stack_container->first_mark_buffer->next;
    mark_stack *to_free_mark_stack;
    while (stale_extra_mark_stack) {
        to_free_mark_stack = stale_extra_mark_stack;
        stale_extra_mark_stack = stale_extra_mark_stack->next;
        release_empty_mark_stack(to_free_mark_stack);
    }
    // Reinitialize to produce an empty stack.
    mark_stack_init(a_mark_stack_container, a_mark_stack_container->first_mark_buffer);
}

//
// mark_stack_pop - pops an address from the mark_stack
// Arguments - a mark_stack container.
// Returns an object from an mark_stack if one is available
//    otherwise returns NULL
//
// Implementation
// The read pointer result->mark_stack_tos points at the value to return.
// If one is available it returns it and adjusts the tos pointer to
// point at the next lower object on the stack. It is possible that 
// a peeker has stolen an object and done the work required. Unfortuntely
// only the thread that put the object on the stack can pop it off.
//


// On entry and exit the TOS points one past a valid entry.

inline Partial_Reveal_Object *mark_stack_pop(mark_stack_container *a_mark_stack_container)
{
    mark_stack *the_mark_stack = a_mark_stack_container->active_mark_buffer;
	// Do fast nothing waiting exit first.
	if (the_mark_stack->p_top_of_stack <= &(the_mark_stack->data[0])) {
        assert (the_mark_stack->p_top_of_stack == &(the_mark_stack->data[0]));
		if (the_mark_stack->previous == NULL) {
			return NULL;
        } else {
            mark_stack *temp_mark_stack = the_mark_stack; 
            the_mark_stack = the_mark_stack->previous;
            a_mark_stack_container->active_mark_buffer = the_mark_stack;
            assert(&the_mark_stack->data[MARK_STACK_ELEMENTS] == (the_mark_stack->p_top_of_stack + 1) );
        }
	}
    // OK we control the_mark_stack so no race condition.
    
    the_mark_stack->p_top_of_stack--;
    Partial_Reveal_Object *result = *(the_mark_stack->p_top_of_stack);
    // pop the stack;
    gc_trace (result, "This object is being popped from a mark_stack.");
    assert (the_mark_stack->p_top_of_stack <= &(the_mark_stack->data[MARK_STACK_ELEMENTS])); 
    assert (the_mark_stack->p_top_of_stack >= &(the_mark_stack->data[0]));
    assert ( ((POINTER_SIZE_INT)result & 0x3) == 0); /* Make sure it is a valid pointer */
    gc_trace (result, "This object is popped off a mark_stack.");
    assert(result);
    return result;
}

//
// These are the only two routines available outside this file.
//
// mark_stack_push - pushes an object on the mark stack.
// Arguments
//    void *address the address to insert into the mark_stack
//    mark_stack_container a pointer to an_mark_stack_container.
//    We can add another buffer to this mark_stack container simply by linking
//    it onto the next field.
// Returns nothing
//

// On entry and exit the TOS points one past a valid entry.
inline void mark_stack_push (mark_stack_container *an_mark_stack_container, Partial_Reveal_Object *p_obj)
{
    assert(p_obj);
    gc_trace (p_obj, "This object is pushed onto a mark_stack.");
    mark_stack *the_mark_stack = an_mark_stack_container->active_mark_buffer;
    if (the_mark_stack->p_top_of_stack > &(the_mark_stack->data[MARK_STACK_ELEMENTS])) {
        if (the_mark_stack->next == NULL) {
            add_mark_stack(an_mark_stack_container);
        }
        the_mark_stack = the_mark_stack->next;
    }
    *(the_mark_stack->p_top_of_stack) = p_obj;
    the_mark_stack->p_top_of_stack++;
}

#endif


