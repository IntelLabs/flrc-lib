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

#ifndef ORP_POSIX
#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>
#include <sys\stat.h>
#endif // ORP_POSIX

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
#include "tgc/gcv4_synch.h"

extern bool g_gen;
extern bool g_gen_all;

void *_p_heap_base = NULL;
void *_p_heap_ceiling = NULL;
POINTER_SIZE_INT _heap_size_bytes = 0;
extern POINTER_SIZE_INT g_cmd_line_heap_base;

#ifdef _IA64_
#include <windows.h>
#include <ostream.h>
#include <stdio.h>

// the following code is donated by the Electron guys.
// to use large pages, you need to modify 'lock pages' privileges
// go to Administrator Tools, Local Security Settings, User Rights
//  assignments, and enable 'lock pages in memory'

typedef UINT (WINAPI *PGetLargePageMinimum) ();

static BOOL
SetPrivilege (
              HANDLE hToken,          // access token handle
              LPCTSTR lpszPrivilege,  // name of privilege to enable/disable
              BOOL bEnablePrivilege)  // to enable or disable privilege
{
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if ( !LookupPrivilegeValue(
        NULL,            // lookup privilege on local system
        lpszPrivilege,   // privilege to lookup
        &luid ) ) {      // receives LUID of privilege
        printf("LookupPrivilegeValue error: %u\n", GetLastError() );
        return FALSE;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    if (bEnablePrivilege)
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    else
        tp.Privileges[0].Attributes = 0;

    // Enable the privilege or disable all privileges.

    AdjustTokenPrivileges(
        hToken,
        FALSE,
        &tp,
        sizeof(TOKEN_PRIVILEGES),
        (PTOKEN_PRIVILEGES) NULL,
        (PDWORD) NULL);

    // Call GetLastError to determine whether the function succeeded.

    if (GetLastError() != ERROR_SUCCESS) {
        printf("AdjustTokenPrivileges failed: %u\n", GetLastError() );
        return FALSE;
    }

    return TRUE;
} // SetPrivilege


// return the page minimum size, if the user has privileges to access
// large pages, otherwise 0

static int adjustprivileges() {

    HMODULE h;
    HANDLE accessToken;
    //UINT_PTR uipMemSize, uipMinPageSize, uipPerAllocSize, uipAllocCalls, uipCount, uipTotalAlloc;
    //PVOID pAllocArray;
    PGetLargePageMinimum m_GetLargePageMinimum;

    if (!(OpenProcessToken (GetCurrentProcess (),
        TOKEN_ALL_ACCESS, &accessToken)
        && SetPrivilege (accessToken, "SeLockMemoryPrivilege", TRUE))) {
        orp_cout << "Lock Page Privilege not set.\n" << std::endl;
        return 0;
        //exit(EXIT_FAILURE);
    }

    h = GetModuleHandleA("kernel32.dll");

    m_GetLargePageMinimum = (PGetLargePageMinimum) GetProcAddress(h, "GetLargePageMinimum");
    if (!m_GetLargePageMinimum) {
        orp_cout << "Cannot locate GetLargePageMinimum.\n" << std::endl;
        return 0;
        //exit(EXIT_FAILURE);
    }

    return m_GetLargePageMinimum();
}
#endif // _IA64_

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern bool use_large_pages;
extern void get_next_set_bit(set_bit_search_info *info);
extern void verify_get_next_set_bit_code(set_bit_search_info *);
extern unsigned g_heap_compact_cmd_override;
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// JMS 2003-08-11.  The variable force_unaligned_heap and all associated code
// are for measuring the performance benefit of having a 4GB-aligned heap when
// compressed references are used.  Since 4GB alignment practically always
// succeeds on IPF, we need a way to force misalignment.
bool force_unaligned_heap = false;

Block_Store::Block_Store(POINTER_SIZE_INT initial_heap_size, POINTER_SIZE_INT final_heap_size, unsigned int block_size_bytes) {
    assert(initial_heap_size == final_heap_size);
    assert(block_size_bytes == GC_BLOCK_SIZE_BYTES);

    POINTER_SIZE_INT final_heap_size_extra = (POINTER_SIZE_INT) Slot::additional_padding_for_heap_alignment();

    // If compressed vtable pointers are used, and moving GC is used,
    // then the heap size must be within 4GB.  This is because the
    // obj_info field in the object header needs to be used as a
    // forwarding offset.
    extern bool incremental_compaction;
    if (orp_vtable_pointers_are_compressed() && incremental_compaction &&
        final_heap_size > Partial_Reveal_Object::max_supported_heap_size()) {
			orp_cout << "Error: If compressed vtable pointers are used and moving GC is specified," << std::endl;
        orp_cout << "       the maximum heap size must be " <<
            (int)(Partial_Reveal_Object::max_supported_heap_size() / (1024*1024)) << "MB or less." << std::endl;
        orp_exit(17001);
    }

#ifdef ALLOW_COMPRESSED_REFS
    // If compressed object references are used, then the heap size must be
    // within 4GB.  In the future, this limit could expand to 16GB or 32GB if
    // we exploit the fact that objects are aligned on a 4 or 8 byte boundary.
    if (gc_references_are_compressed && (final_heap_size - 1) > 0xFFFFffff) {
        orp_cout << "Error: If compressed referencess are used," << std::endl;
        orp_cout << "the maximum heap size must be less than 4GB." << std::endl;
        orp_exit(17002);
    }
#endif


    _block_size_bytes = block_size_bytes;

    //
    // Get information about the current system. (Page size, num processors, etc.)
    //
    int64 page_size = _machine_page_size_bytes;

    void *gc_free = NULL;
#ifdef CONCORD
    gc_free = malloc_shared(final_heap_size + 0xFFFF);
    // alignment happens later

    if(gc_free == NULL) {
#ifdef _WINDOWS
        DWORD error_code = GetLastError();
        orp_cout << "Error: Garbage Collector failed to reserve ";
        orp_cout << final_heap_size_bytes;
        orp_cout << " bytes of virtual address space. Error code = ";
        orp_cout << error_code << std::endl;
        assert(0);
        orp_exit(error_code);
#else
        perror("malloc failure");
        printf("%d %d %d\n",final_heap_size,final_heap_size+0xFFFF,page_size);
        assert(0);
        orp_exit(-1);
#endif
    }
#else
#ifdef ORP_POSIX

    gc_free = malloc(final_heap_size + 0xFFFF);
    // alignment happens later

    if(gc_free == NULL) {
#ifdef _WINDOWS
        DWORD error_code = GetLastError();
        orp_cout << "Error: Garbage Collector failed to reserve ";
        orp_cout << final_heap_size_bytes;
        orp_cout << " bytes of virtual address space. Error code = ";
        orp_cout << error_code << std::endl;
        assert(0);
        orp_exit(error_code);
#else
        perror("malloc failure");
        printf("%d %d %d\n",final_heap_size,final_heap_size+0xFFFF,page_size);
        assert(0);
        orp_exit(-1);
#endif
    }
#else // !ORP_POSIX

    // JMS 2003-05-23.  Getting virtual memory from Windows.  If large_pages is
    // specified, we either acquire the entire heap with large pages or we exit.
    // If compressed_references is specified, we ask for an additional 4GB and
    // then adjust the result so that it is 4GB aligned.  If we don't manage to
    // get that much, we default to asking for the original amount and accept that
    // it's not 4GB aligned.  We only commit the actual heap size requested even
    // though an extra 4GB is reserved, so the memory footprint still appears
    // reasonable.
    //
    // Since VirtualAlloc with large pages seems to require committing up front,
    // we actually waste 4GB.

    DWORD action = MEM_RESERVE;
    DWORD action_large = 0;
    DWORD protection = PAGE_NOACCESS;
#ifdef _IA64_
    if (use_large_pages) {
        // Using large pages on Win64 seems to require MEM_COMMIT and PAGE_READWRITE.
        int64 minimum_page_size = (int64)adjustprivileges();
        page_size = minimum_page_size;
        action = MEM_COMMIT;
        action_large = MEM_LARGE_PAGES;
        protection = PAGE_READWRITE;
        final_heap_size = (final_heap_size+minimum_page_size-1)&
            ~(minimum_page_size-1);
    }
#endif // _IA64_
    if ((gc_free = VirtualAlloc((LPVOID)g_cmd_line_heap_base,
        final_heap_size + final_heap_size_extra,
        action | action_large,
        protection)) == NULL) {
        if (final_heap_size_extra > 0) {
            orp_cout << "Failed to allocate 4GB-aligned memory with large pages." << std::endl;
            orp_cout << "  Trying non-aligned memory." << std::endl;
            orp_cout << "  (Error code was " << GetLastError() << ")" << std::endl;
        }
        // Try again without trying to align at a 4GB boundary.
        final_heap_size_extra = 0;
        if ((gc_free = VirtualAlloc((LPVOID)g_cmd_line_heap_base,
            final_heap_size + final_heap_size_extra,
            action | action_large,
            protection)) == NULL) {
            DWORD error_code = GetLastError();
            orp_cout << "Error: Garbage Collector failed to reserve ";
            orp_cout << (void *) (final_heap_size_bytes + final_heap_size_extra);
            orp_cout << " bytes of virtual address space. Error code = ";
            orp_cout << error_code << std::endl;
            assert(0);
            orp_exit(error_code);
        }
    }

    if (final_heap_size_extra > 0) {
        void *old_gc_free = gc_free;
        gc_free = (void *) (((POINTER_SIZE_INT)gc_free + final_heap_size_extra) &
            ~(final_heap_size_extra - 1));
        if (force_unaligned_heap) {
            // If the original allocation was aligned, bump it forward by the page size.
            // Otherwise, revert to the original value.
            if (old_gc_free == gc_free) {
                gc_free = (void *) ((char *)gc_free + page_size);
            }
            else
            {
                gc_free = old_gc_free;
            }
        }
    }

    // Commit of NT (and W2K)
    if (protection == PAGE_NOACCESS) {
        protection = PAGE_READWRITE;
        action = MEM_COMMIT;
        if ((gc_free = VirtualAlloc(gc_free,
            final_heap_size,
            action | action_large,
            protection)) == NULL) {
            DWORD error_code = GetLastError();
            orp_cout << "Error: GC failed to commit initial heap of size ";
            orp_cout << (void *) initial_heap_size_bytes << " bytes. Error code = " ;
            orp_cout << error_code << std::endl;
            assert(0);
            orp_exit(error_code);
        }
    }

#endif // !ORP_POSIX
#endif // CONCORD


    void *gc_ceiling = (void *) ((uintptr_t) gc_free + final_heap_size);

    // Align ceiling to block_size_bytes
    gc_ceiling = (void *) (((uintptr_t) gc_ceiling) & GC_BLOCK_HIGH_MASK);

    // Align base to block_size_bytes
    gc_free =   (void *) ( (((uintptr_t) gc_free + (GC_BLOCK_SIZE_BYTES - 1)) & (~(GC_BLOCK_SIZE_BYTES - 1))) );


    _p_heap_base     = gc_free;
    _p_heap_ceiling  = gc_ceiling;
    _heap_size_bytes = (uintptr_t) _p_heap_ceiling - (uintptr_t) _p_heap_base;

    if (verbose_gc) {
        orp_cout << " Heap base    is " << gc_free    << std::endl;
        orp_cout << " Heap ceiling is " << gc_ceiling << std::endl;
    }

    _initialize_block_tables(_p_heap_base, _heap_size_bytes);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    _heap_compaction_ratio = GC_FRACTION_OF_HEAP_INCREMENTAL_COMPACTED_DURING_EACH_GC;
    if(g_heap_compact_cmd_override) {
        _heap_compaction_ratio = g_heap_compact_cmd_override;
    }

    _compaction_type_for_this_gc = gc_bogus_compaction;
    _compaction_blocks_low_index = 0;
    // Compact an eigth of the heap each GC cycle
    _compaction_blocks_high_index = _number_of_blocks_in_heap / _heap_compaction_ratio;
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    _per_gc_thread_compaction_blocks_low_index = (unsigned int *) malloc(g_num_cpus * sizeof(unsigned int));
    if (!(	_per_gc_thread_compaction_blocks_low_index)) {
        orp_cout << "malloc failed" << std::endl;
        exit (411);
    }
    memset(_per_gc_thread_compaction_blocks_low_index, 0, g_num_cpus * sizeof(unsigned int));
    _per_gc_thread_compaction_blocks_high_index = (unsigned int *) malloc(g_num_cpus * sizeof(unsigned int));
    if (!_per_gc_thread_compaction_blocks_high_index) {
        orp_cout << "malloc failed" << std::endl;
        exit (411);
    }
    memset(_per_gc_thread_compaction_blocks_high_index, 0, g_num_cpus * sizeof(unsigned int));
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Record the heap base for compressed 32-bit pointers.
    // This requires a single contiguous block of memory, so be sure that
    // this isn't done twice.
    assert(Partial_Reveal_Object::heap_base == 0);
    Partial_Reveal_Object::heap_base = (uintptr_t) _p_heap_base;
    Slot::init(gc_free, gc_ceiling);
}


Block_Store::~Block_Store() {
    //
    // Release the memory we obtained for the GC heap, including
    // the ancilliary table(s).
    //
    assert(0);
}


static volatile void *block_store_lock = (void *)0;


// Gets the lock
static inline void get_block_store_lock() {
    while (InterlockedCompareExchangePointer ((PVOID *)&block_store_lock,
        (PVOID)1,
        (PVOID)0) == (PVOID)1) {
        while (block_store_lock == (void *)1) {
            orp_thread_sleep(1);
        }
    }
}

static inline void release_block_store_lock() {
    assert (block_store_lock == (void *)1);
    block_store_lock = 0;
}


void Block_Store::_initialize_block_tables(void *p_heap_start_address, POINTER_SIZE_INT heap_size_in_bytes) {
    // Start off with small blocks each

    _number_of_blocks_in_heap = (unsigned int)(heap_size_in_bytes / (POINTER_SIZE_INT)_block_size_bytes);

    if (_number_of_blocks_in_heap  >= GC_MAX_BLOCKS) {
        orp_cout << "Internal error - attempt to allocate a heap larger than orp build permits, adjust GC_MAX_BLOCKS and rebuild orp or reduce heap size." << std::endl;
        orp_exit (1);
    }

    memset(_blocks_in_block_store, 0, sizeof(block_store_info) * GC_MAX_BLOCKS);

    void *block_address = p_heap_start_address;
	int free_space = GC_BLOCK_INFO_SIZE_BYTES - sizeof(block_info);
	if(free_space < 0) {
		printf("Size of block_info exceeds GC_BLOCK_INFO_SIZE_BYTES.\n");
		assert(0);
	}

    for (unsigned int i = 0; i < _number_of_blocks_in_heap; i++) {
        _blocks_in_block_store[i].block = (block_info *) block_address;

        // Clear out the block info...actually lets clear the entire block since this helps to page
        // in the entire heap to physical memory. For large IPF heaps this makes a difference..
        //memset(_blocks_in_block_store[i].block, 0, GC_BLOCK_SIZE_BYTES);

        _blocks_in_block_store[i].block_is_free = true;
        //_blocks_in_block_store[i].block->is_free_block = true;
        _blocks_in_block_store[i].number_of_blocks = 1;
        _blocks_in_block_store[i].is_compaction_block = false;
		_blocks_in_block_store[i].block->age = 0;

		memset( ((char *)block_address) + sizeof(block_info),0xAA, free_space );

        // per thread remembered sets/live object lists etc.
        _blocks_in_block_store[i].per_thread_slots_into_compaction_block = (Remembered_Set **) malloc(g_num_cpus * sizeof(void *));
        if (!_blocks_in_block_store[i].per_thread_slots_into_compaction_block) {
            orp_cout << " malloc failed for _blocks_in_block_store[i].per_thread_slots_into_compaction_block " << std::endl;
            orp_exit (411);
        }
        memset(_blocks_in_block_store[i].per_thread_slots_into_compaction_block, 0, g_num_cpus * sizeof(void *));

        _blocks_in_block_store[i].total_live_objects_in_this_block = 0;

        // move to next block
        block_address = (void *) ((uintptr_t)block_address + _block_size_bytes);
    }

    assert(block_address == (void *) ((uintptr_t) p_heap_start_address + heap_size_in_bytes));
    _num_free_blocks = _number_of_blocks_in_heap;
    // Initlialize the hint to the start of block list
    _free_block_search_hint = 0;
}

void Block_Store::characterize_blocks_in_heap () {
    POINTER_SIZE_INT free_blocks = 0;
    POINTER_SIZE_INT used_blocks = 0;
    POINTER_SIZE_INT single_blocks = 0;
    POINTER_SIZE_INT multi_blocks = 0;

    POINTER_SIZE_INT info_nurseries = 0;
    POINTER_SIZE_INT info_free_bytes = 0;
    POINTER_SIZE_INT info_used_bytes = 0;
    POINTER_SIZE_INT info_multi_blocks = 0;

    unsigned int i;
    for (i = 0; i < _number_of_blocks_in_heap; i++) {
        if (_blocks_in_block_store[i].block_is_free) {
            free_blocks += _blocks_in_block_store[i].number_of_blocks;
        } else {
            used_blocks += _blocks_in_block_store[i].number_of_blocks;
        }

        block_info *the_info = _blocks_in_block_store[i].block;
        //  xx      assert(the_info->is_free_block == _blocks_in_block_store[i].block_is_free);
        if (the_info->in_nursery_p) {
            info_nurseries++;
            //  xx          assert(the_info->is_free_block == _blocks_in_block_store[i].block_is_free);
        }
        info_free_bytes += the_info->block_free_bytes;
        info_used_bytes += the_info->block_used_bytes;

        if (_blocks_in_block_store[i].number_of_blocks != 1) {
            multi_blocks += _blocks_in_block_store[i].number_of_blocks;
            i += _blocks_in_block_store[i].number_of_blocks - 1;
        } else {
            single_blocks++;
        }

    }

    printf ("Total blocks = %d, single blocks = %d, blocks in multi blocks = %d, free blocks =  %d, used blocks = %d, nurseries = %d info_free_bytes = %d, info_used_bytes = %d, accounted for bytes = %d \n",
        _number_of_blocks_in_heap, single_blocks, multi_blocks, free_blocks, used_blocks, info_nurseries, info_free_bytes, info_used_bytes, info_free_bytes + info_used_bytes);
}

void Block_Store::set_free_status_for_all_sub_blocks_in_block(unsigned int block_number, bool is_free_val) {
    unsigned int i = 0;
    while (i < 	_blocks_in_block_store[block_number].number_of_blocks) {
        // this function changes the sense!!!
        assert(_blocks_in_block_store[block_number + i].block_is_free != is_free_val);
        _blocks_in_block_store[block_number + i].block_is_free = is_free_val;
        i++;
    }
}



bool Block_Store::block_store_can_satisfy_request(unsigned int blocks_needed) {

    get_block_store_lock();
    unsigned int index = 0;

    while (index < _number_of_blocks_in_heap) {
        if (_blocks_in_block_store[index].block_is_free == true) {
            if (_blocks_in_block_store[index].number_of_blocks >= blocks_needed) {
                release_block_store_lock();
                return true;
            }
        }
        index++;
    }
    release_block_store_lock();
    return false;
}

//
// If size > GC_BLOCK_ALLOC_SIZE then on needs to use more than one block and the
// single object block flag needs to be set in the block_store_info struct indexed in the
// _blocks_in_block_store index.
//

block_info * Block_Store::p_get_multi_block(unsigned int size, bool for_chunks_pool, bool do_alloc) {
    // Control the number of blocks that go into block chunks pool if
    // I run below the watermark and that is what the request is for
    // This way we can satisfy LOS and muti-block requests even if block
    // store is dangerously low without doing a collection

#if 0
	static unsigned call_count = 0;
	printf("%d size=%d for_chunks=%d alloc=%d\n",call_count++,size,for_chunks_pool,do_alloc);
	print_block_stats();
#endif

    if (for_chunks_pool && (_num_free_blocks <= get_block_store_low_watermark_free_blocks())) {
        return NULL;
    }

    unsigned int number_of_blocks_needed = 1;

    if (size > GC_BLOCK_ALLOC_SIZE) {
        number_of_blocks_needed = ((size + GC_BLOCK_INFO_SIZE_BYTES) / _block_size_bytes) + 1;
        assert (number_of_blocks_needed > 1);
        assert ((GC_BLOCK_ALLOC_SIZE + GC_BLOCK_INFO_SIZE_BYTES) == GC_BLOCK_SIZE_BYTES);
    }

    get_block_store_lock();

    bool search_from_hint_failed = false;
	bool reclaimed_free = false;
    unsigned int index = 0;

retry_p_get_multi_block_label:
    if (search_from_hint_failed) {
        index = 0;
    } else {
        index = _free_block_search_hint;
    }

    while (index < _number_of_blocks_in_heap) {

        if (_blocks_in_block_store[index].block_is_free == true) {

            unsigned int number_of_blocks_found = _blocks_in_block_store[index].number_of_blocks;
            assert(number_of_blocks_found > 0);
            unsigned int j = index + _blocks_in_block_store[index].number_of_blocks;
            unsigned int k = index;

            while (number_of_blocks_found < number_of_blocks_needed) {
                if (_blocks_in_block_store[j].block_is_free == false) {
                    break;
                }
                unsigned int num_blocks = _blocks_in_block_store[j].number_of_blocks;
                assert(num_blocks > 0);
                number_of_blocks_found += num_blocks;
                k = j;
                j += num_blocks;
            }

            if (number_of_blocks_found >= number_of_blocks_needed) {
                if(do_alloc) {
                    // Found the exact or greater number of blocks (than) needed
                    unsigned int check_debug = _blocks_in_block_store[index].number_of_blocks;

                    assert(_blocks_in_block_store[index].block);
                    // clear out the block_info before returning the block
                    memset(_blocks_in_block_store[index].block, 0, sizeof(block_info));
    //                memset(_blocks_in_block_store[index].block, 0, GC_BLOCK_INFO_SIZE_BYTES);

                    set_free_status_for_all_sub_blocks_in_block(index, false);
                    // Set is_free_block to false
                    //_blocks_in_block_store[index].block->is_free_block = false; // xx Added RLH 8/04

                    unsigned int z = index + _blocks_in_block_store[index].number_of_blocks;
                    while (z <= k) {
                        // This is a multi-block..... Zero out info for ancillary blocks (uptil index k)
                        assert(number_of_blocks_found > 1);
                        assert(_blocks_in_block_store[z].block_is_free == true);

                        // make the entire super-block not-free
                        set_free_status_for_all_sub_blocks_in_block(z, false);
                        // Only set the first block ->is_free_block since the other block headers will be overwritten
                        assert(_blocks_in_block_store[z].block);
                        // clear out the block info before collapsing this block into the bigger one
                        memset(_blocks_in_block_store[z].block, 0, GC_BLOCK_INFO_SIZE_BYTES);

                        _blocks_in_block_store[z].block = NULL;

                        check_debug += _blocks_in_block_store[z].number_of_blocks;

                        // Block at "index" will keep track of the count of this
                        unsigned int save_z = z;
                        z += _blocks_in_block_store[z].number_of_blocks;
                        _blocks_in_block_store[save_z].number_of_blocks = 0;
                    }

                    // now block at "index" has either exactly equal or more blocks than needed
                    assert(check_debug == number_of_blocks_found);

                    if (number_of_blocks_found > number_of_blocks_needed) {
                        // chop off remaining and return exactly what is needed.
                        POINTER_SIZE_INT chopped_free_index = index + number_of_blocks_needed;
                        assert(_blocks_in_block_store[chopped_free_index].block == NULL);

                        void *block_address = (void *) ((uintptr_t)_p_heap_base + (chopped_free_index << GC_BLOCK_SHIFT_COUNT));
                        POINTER_SIZE_INT check_index = (POINTER_SIZE_INT) (((uintptr_t) block_address - (uintptr_t) _p_heap_base) >> GC_BLOCK_SHIFT_COUNT);
                        assert(check_index == chopped_free_index);
                        // Give the chopped portion a new address and set it FREE!!!
                        _blocks_in_block_store[chopped_free_index].block = (block_info *) block_address;
                        _blocks_in_block_store[chopped_free_index].number_of_blocks = number_of_blocks_found - number_of_blocks_needed;
                        set_free_status_for_all_sub_blocks_in_block((unsigned int)chopped_free_index, true);
                        //_blocks_in_block_store[chopped_free_index].block->is_free_block = true; // RLH Aug 04
                        _blocks_in_block_store[index].number_of_blocks = number_of_blocks_needed;
                        _blocks_in_block_store[index].block->number_of_blocks = _blocks_in_block_store[index].number_of_blocks;

                    } else {
                        assert(number_of_blocks_found == number_of_blocks_needed);
                        _blocks_in_block_store[index].number_of_blocks = number_of_blocks_needed;
                        _blocks_in_block_store[index].block->number_of_blocks = _blocks_in_block_store[index].number_of_blocks;
                    }

                    assert(_blocks_in_block_store[index].number_of_blocks == number_of_blocks_needed);
                    assert(_blocks_in_block_store[index].block->number_of_blocks == number_of_blocks_needed);

                    // points back to the block store info
                    _blocks_in_block_store[index].block->block_store_info_index = index;
                    _blocks_in_block_store[index].block->bsi = &_blocks_in_block_store[index];
                    // Decrement free count
                    _num_free_blocks -= number_of_blocks_needed;
                    // Update hint for next search
                    _free_block_search_hint = index;

                    if (number_of_blocks_needed > 1) {
                        // ***SOB LOOKUP*** We need to set the is_single_object_block to true....
                        unsigned int sob_index;
                        for (sob_index = 0; sob_index < number_of_blocks_needed; sob_index++) {
                            assert (_blocks_in_block_store[sob_index + index].is_single_object_block == false);
                            _blocks_in_block_store[sob_index + index].is_single_object_block = true;
                        }
                    }
                    gc_trace_block(_blocks_in_block_store[index].block, "p_get_multi_block returns this block");
                }
                release_block_store_lock();

                // Set is_free_block to false
                //assert (_blocks_in_block_store[index].block->is_free_block == false); // xx Added RLH 8/04
                return _blocks_in_block_store[index].block;
            }
        }
        // Jump ahead of this free chunk of small size or used chunk of any size
        index += _blocks_in_block_store[index].number_of_blocks;

    } // while

    if (search_from_hint_failed == false) {
        // Try again...exactly once more....this time from index 0;
        search_from_hint_failed = true;
        goto retry_p_get_multi_block_label;
    }

	if(!reclaimed_free && !for_chunks_pool) {
		p_global_gc->return_free_blocks_to_block_store_prob(0.5, false, false, true);
		reclaimed_free = true;
        goto retry_p_get_multi_block_label;
	}

    if (stats_gc) {
        orp_cout << "p_get_multi_block() returns NULL -- , _num_free_blocks is " << _num_free_blocks << " requested " << number_of_blocks_needed << std::endl;
    }

    release_block_store_lock();

    return NULL;
}


block_info * Block_Store::p_get_new_block(bool for_chunks_pool) {
    return p_get_multi_block (GC_BLOCK_ALLOC_SIZE, for_chunks_pool);
}



void Block_Store::link_free_blocks (block_info *freed_block, unsigned int num_blocks, bool has_block_store_lock) {
    gc_trace_block(freed_block, " in link_free_block");

    assert(freed_block <  _p_heap_ceiling);
    assert(freed_block >= _p_heap_base);

	if(!has_block_store_lock) {
		get_block_store_lock();
	}

    unsigned int block_store_block_number = (unsigned int) (((uintptr_t) freed_block - (uintptr_t) _p_heap_base) >> GC_BLOCK_SHIFT_COUNT);

    // points back to the block store info
    assert(freed_block->block_store_info_index == block_store_block_number);

    assert(_blocks_in_block_store[block_store_block_number].block == freed_block);
    assert(_blocks_in_block_store[block_store_block_number].number_of_blocks == num_blocks);
    assert(_blocks_in_block_store[block_store_block_number].block_is_free == false);

    for (unsigned int i = block_store_block_number; i < (num_blocks + block_store_block_number) ; i++) {
        _blocks_in_block_store[i].total_live_objects_in_this_block = 0;
        _blocks_in_block_store[i].is_compaction_block = false; // Clear is compaction block field

        _blocks_in_block_store[i].block_for_sliding_compaction_allocation_pointer_computation = NULL;
        _blocks_in_block_store[i].block_for_sliding_compaction_slot_updates = NULL;
        _blocks_in_block_store[i].block_for_sliding_compaction_object_slides = NULL;
        assert(_blocks_in_block_store[i].per_thread_slots_into_compaction_block);
        for (unsigned int cpu = 0; cpu < g_num_cpus; cpu++) {
            assert(_blocks_in_block_store[i].per_thread_slots_into_compaction_block[cpu] == NULL);
            if (_blocks_in_block_store[i].per_thread_slots_into_compaction_block[cpu]) {
                delete _blocks_in_block_store[i].per_thread_slots_into_compaction_block[cpu];
                _blocks_in_block_store[i].per_thread_slots_into_compaction_block[cpu] = NULL;
            }
        }
        if (freed_block->block_free_areas)  {	// These get recreated when they go back to the chunk pool or whatever.
            free(freed_block->block_free_areas);
        }
        freed_block->block_free_areas = NULL;

        // ***SOB LOOKUP*** Clear the is_single_object_block field as we release the blocks.
        _blocks_in_block_store[i].is_single_object_block = false;
    }

    // check integrity of sub-blocks if any
    if (num_blocks > 1) {
        for (unsigned int i = block_store_block_number + 1; i < (num_blocks + block_store_block_number) ; i++) {
            assert(_blocks_in_block_store[i].block == NULL);
            assert(_blocks_in_block_store[i].number_of_blocks == 0);
            assert(_blocks_in_block_store[i].block_is_free == false);
        }
    }

    // clear out the block info of the returned block
    memset(freed_block, 0, sizeof(block_info));

    // Make the entire block free
    set_free_status_for_all_sub_blocks_in_block(block_store_block_number, true);
    //freed_block->is_free_block = true;

    // Increment free count
    _num_free_blocks += num_blocks;

    // block has been returned...nothing more to do.

	if(!has_block_store_lock) {
	    release_block_store_lock();
	}
} // Block_Store::link_free_blocks


void Block_Store::coalesce_free_blocks() {
    get_block_store_lock();

    unsigned int number_of_blocks_coalesced = 0;

    // Dont expect any multiblocks for now....
    unsigned int index = 0;

    while (index < _number_of_blocks_in_heap) {

        if (_blocks_in_block_store[index].block_is_free == true) {

            // Points to valid block
            assert(_blocks_in_block_store[index].block);

            unsigned int j = index + _blocks_in_block_store[index].number_of_blocks;

            while ((j < _number_of_blocks_in_heap) && (_blocks_in_block_store[j].block_is_free == true)) {

                assert(_blocks_in_block_store[j].block);
                // clear out the block info
                memset(_blocks_in_block_store[j].block, 0, GC_BLOCK_INFO_SIZE_BYTES);
                // Coalesced and gone
                _blocks_in_block_store[j].block = NULL;
                // Combine with number of blocks with one at index
                _blocks_in_block_store[index].number_of_blocks += _blocks_in_block_store[j].number_of_blocks;
                number_of_blocks_coalesced += _blocks_in_block_store[j].number_of_blocks;
                // Skip ahead by the size of this region
                unsigned int save_size_at_j = _blocks_in_block_store[j].number_of_blocks;
                // This block loses its identity and count now
                _blocks_in_block_store[j].number_of_blocks = 0;
                j += save_size_at_j;
            }
        }
        // check if _free_block_search_hint was subsumed by this coalesce
        if ((_free_block_search_hint > index) && (_free_block_search_hint < (index + _blocks_in_block_store[index].number_of_blocks))) {
            _free_block_search_hint = index;	// Adjust to first block
        }

        // Jump ahead of the coalesced region and start looking after that ends
        index += _blocks_in_block_store[index].number_of_blocks;
    }
    if (stats_gc) {
        orp_cout << "Block_Store::coalesce_free_blocks() coalesced " << number_of_blocks_coalesced << " blocks." << std::endl;
    }
    release_block_store_lock();
}


#ifdef LINUX
#include <unistd.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////// S L I D I N G   C O M P A C T I O N //////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



inline void Block_Store::get_compaction_limits_based_on_compaction_type_for_this_gc(unsigned int *low, unsigned int *high) {
    if (_compaction_type_for_this_gc == gc_full_heap_sliding_compaction) {
        *low = 0;
        *high = _number_of_blocks_in_heap;

        // Reset so that the next GC after a full sliding compaction starts at the beginning of the heap.
        _compaction_blocks_low_index  = 0;
        _compaction_blocks_high_index = _number_of_blocks_in_heap / _heap_compaction_ratio;
    } else if (_compaction_type_for_this_gc == gc_incremental_sliding_compaction) {
        *low  = _compaction_blocks_low_index;
        *high = _compaction_blocks_high_index;
    } else {
        // Only these two types are supported for now
        assert(0);
    }
}

/*
 * pinned_blocks = set of blocks that cannot be compacted because some pinned root is pointing into that block.
 */
void Block_Store::determine_compaction_areas_for_this_gc(std::set<block_info *> &pinned_blocks) {
    if(is_young_gen_collection()) {
        // young generation collection
        unsigned int low_index  = 0;
        unsigned int high_index = _number_of_blocks_in_heap;
        unsigned max_blocks_to_compact = _number_of_blocks_in_heap / _heap_compaction_ratio;

        for (unsigned int i = low_index; i < high_index; i++) {
            // Set for compaction all single blocks
            if ( (_blocks_in_block_store[i].number_of_blocks == 1) &&	// Only simple blocks
                 (_blocks_in_block_store[i].block_is_free == false) &&	// Only used blocks
                 (_blocks_in_block_store[i].block->in_los_p == false) &&	// If they are LOS, they will not be compacted
                 (_blocks_in_block_store[i].block->is_single_object_block == false) &&	// If they are single object block, they will not be compacted
                 (_blocks_in_block_store[i].block->generation == 0) && // == 0 means young generation block
                 max_blocks_to_compact &&
                 (pinned_blocks.find(_blocks_in_block_store[i].block) == pinned_blocks.end()) // If the block is not in the list of pinned blocks
                ) {

                assert(_blocks_in_block_store[i].block->in_nursery_p);
                assert(_blocks_in_block_store[i].is_compaction_block == false);

                _blocks_in_block_store[i].is_compaction_block = true;
                --max_blocks_to_compact;

                assert(_blocks_in_block_store[i].block);
                _blocks_in_block_store[i].block->is_compaction_block = true;
                gc_trace_block(_blocks_in_block_store[i].block, "This block in compaction area.");

                _blocks_in_block_store[i].total_live_objects_in_this_block = 0;

			    _blocks_in_block_store[i].block->from_block_has_been_evacuated = false;
			    _blocks_in_block_store[i].block->is_to_block = false;
			    gc_trace_block (  _blocks_in_block_store[i].block, "from_block_has_been_evacuated and is_to_block set to false.");

                _blocks_in_block_store[i].block_for_sliding_compaction_allocation_pointer_computation = _blocks_in_block_store[i].block;
                _blocks_in_block_store[i].block_for_sliding_compaction_slot_updates  = _blocks_in_block_store[i].block;
                _blocks_in_block_store[i].block_for_sliding_compaction_object_slides = _blocks_in_block_store[i].block;
            }
        }

        // Setup regions for each GC thread to operate on
        unsigned int min_compact_blocks_per_gc_thread = (high_index - low_index) / g_num_cpus;
        unsigned int extra_tail_for_last_gc_thread    = (high_index - low_index) % g_num_cpus;
        unsigned int gc_thread = 0;
        unsigned int low_index_curr  = low_index;
        unsigned int high_index_curr = low_index + min_compact_blocks_per_gc_thread;
        while (gc_thread < g_num_cpus) {
            _per_gc_thread_compaction_blocks_low_index[gc_thread]  = low_index_curr;
            _per_gc_thread_compaction_blocks_high_index[gc_thread] = high_index_curr;
            low_index_curr   = high_index_curr;
            high_index_curr += min_compact_blocks_per_gc_thread;
            gc_thread++;
        }
        _per_gc_thread_compaction_blocks_high_index[g_num_cpus - 1] += extra_tail_for_last_gc_thread;
        assert(_per_gc_thread_compaction_blocks_low_index[0] == low_index);
        assert(_per_gc_thread_compaction_blocks_high_index[g_num_cpus - 1] == high_index);
        return;
    }

    if (_compaction_type_for_this_gc == gc_no_compaction) {
        // nothing
        return;
    }

    unsigned int low_index  = 0;
    unsigned int high_index = 0;

    get_compaction_limits_based_on_compaction_type_for_this_gc(&low_index, &high_index);

    assert((low_index < high_index) && (high_index <= _number_of_blocks_in_heap));
    if (stats_gc) {
        printf("Compaction limits for this GC (#%d - #%d) \n", low_index, high_index - 1);
    }

    for (unsigned int i = low_index; i < high_index; i++) {

        // Set for compaction all single blocks
        if ( (_blocks_in_block_store[i].number_of_blocks == 1) &&	// Only simple blocks
             (_blocks_in_block_store[i].block_is_free == false) &&	// Only used blocks
             (_blocks_in_block_store[i].block->in_los_p == false) &&	// If they are LOS, they will not be compacted
             (_blocks_in_block_store[i].block->is_single_object_block == false) &&	// If they are single object block, they will not be compacted
             (pinned_blocks.find(_blocks_in_block_store[i].block) == pinned_blocks.end()) // If the block is not in the list of pinned blocks
            ) {

            assert(_blocks_in_block_store[i].block->in_nursery_p);
            assert(_blocks_in_block_store[i].is_compaction_block == false);

            _blocks_in_block_store[i].is_compaction_block = true;
//			++compaction_blocks;

            assert(_blocks_in_block_store[i].block);
            _blocks_in_block_store[i].block->is_compaction_block = true;
            gc_trace_block(_blocks_in_block_store[i].block, "This block in compaction area.");

            _blocks_in_block_store[i].total_live_objects_in_this_block = 0;

			_blocks_in_block_store[i].block->from_block_has_been_evacuated = false;
			_blocks_in_block_store[i].block->is_to_block = false;
			gc_trace_block (_blocks_in_block_store[i].block, "from_block_has_been_evacuated and is_to_block set to false.");

            _blocks_in_block_store[i].block_for_sliding_compaction_allocation_pointer_computation = _blocks_in_block_store[i].block;
            _blocks_in_block_store[i].block_for_sliding_compaction_slot_updates  = _blocks_in_block_store[i].block;
            _blocks_in_block_store[i].block_for_sliding_compaction_object_slides = _blocks_in_block_store[i].block;
        }
    }

    // Setup regions for each GC thread to operate on
    unsigned int min_compact_blocks_per_gc_thread = (high_index - low_index) / g_num_cpus;
    unsigned int extra_tail_for_last_gc_thread = (high_index - low_index) % g_num_cpus;
    unsigned int gc_thread = 0;
    unsigned int low_index_curr = low_index;
    unsigned int high_index_curr = low_index + min_compact_blocks_per_gc_thread;
    while (gc_thread < g_num_cpus) {
        _per_gc_thread_compaction_blocks_low_index[gc_thread]  =  low_index_curr;
        _per_gc_thread_compaction_blocks_high_index[gc_thread] = high_index_curr;
        low_index_curr   = high_index_curr;
        high_index_curr += min_compact_blocks_per_gc_thread;
        gc_thread++;
    }
    _per_gc_thread_compaction_blocks_high_index[g_num_cpus - 1] += extra_tail_for_last_gc_thread;
    assert(_per_gc_thread_compaction_blocks_low_index[0] == low_index);
    assert(_per_gc_thread_compaction_blocks_high_index[g_num_cpus - 1] == high_index);
}

void Block_Store::reset_compaction_areas_after_this_gc() {
    if(is_young_gen_collection()) {
        // young generation collection
        unsigned int low_index  = 0;
        unsigned int high_index = _number_of_blocks_in_heap;

        for (unsigned int i = low_index; i < high_index; i++) {
		    if(_blocks_in_block_store[i].is_compaction_block) {
			    _blocks_in_block_store[i].is_compaction_block = false;
			    _blocks_in_block_store[i].block->is_compaction_block = false;

			    assert(_blocks_in_block_store[i].per_thread_slots_into_compaction_block);
			    for (unsigned int cpu = 0; cpu < g_num_cpus; cpu++) {
				    assert(_blocks_in_block_store[i].per_thread_slots_into_compaction_block[cpu] == NULL);
				    if (_blocks_in_block_store[i].per_thread_slots_into_compaction_block[cpu]) {
					    delete _blocks_in_block_store[i].per_thread_slots_into_compaction_block[cpu];
					    _blocks_in_block_store[i].per_thread_slots_into_compaction_block[cpu] = NULL;
				    }
			    }

                _blocks_in_block_store[i].bit_index_into_all_lives_in_block = 0;
                _blocks_in_block_store[i].total_live_objects_in_this_block = 0;
                assert(_blocks_in_block_store[i].block);

                // compacted blocks get swept automatically
                assert(_blocks_in_block_store[i].block->block_has_been_swept);
		    }
        }
    }

    unsigned int low_index = 0;
    unsigned int high_index = 0;

    if (_compaction_type_for_this_gc == gc_no_compaction) {
        // nothing
        return;
    }

    get_compaction_limits_based_on_compaction_type_for_this_gc(&low_index, &high_index);
    for (unsigned int i = low_index; i < high_index; i++) {
		if(_blocks_in_block_store[i].is_compaction_block == false) {
			continue;
		}

        // Set for compaction all single blocks
        if ((_blocks_in_block_store[i].number_of_blocks == 1) &&
            (_blocks_in_block_store[i].block_is_free == false) &&
            (_blocks_in_block_store[i].block->in_los_p == false)&&
            (_blocks_in_block_store[i].block->is_single_object_block == false)&&
			(is_compaction_block(_blocks_in_block_store[i].block))
            ) {

            gc_trace_block(_blocks_in_block_store[i].block, "This block has compaction area reset.");

			assert(_blocks_in_block_store[i].block->in_nursery_p);
			assert(_blocks_in_block_store[i].is_compaction_block == true);
            // From block had better have been evacuated by now.

            assert(_blocks_in_block_store[i].block->from_block_has_been_evacuated); // This block has not been evacuated yet.


			_blocks_in_block_store[i].is_compaction_block = false;
			_blocks_in_block_store[i].block->is_compaction_block = false;

			assert(_blocks_in_block_store[i].per_thread_slots_into_compaction_block);
			for (unsigned int cpu = 0; cpu < g_num_cpus; cpu++) {
				assert(_blocks_in_block_store[i].per_thread_slots_into_compaction_block[cpu] == NULL);
				if (_blocks_in_block_store[i].per_thread_slots_into_compaction_block[cpu]) {
					delete _blocks_in_block_store[i].per_thread_slots_into_compaction_block[cpu];
					_blocks_in_block_store[i].per_thread_slots_into_compaction_block[cpu] = NULL;
				}
			}

            _blocks_in_block_store[i].bit_index_into_all_lives_in_block = 0;
            _blocks_in_block_store[i].total_live_objects_in_this_block = 0;
            assert(_blocks_in_block_store[i].block);

            // compacted blocks get swept automatically
            assert(_blocks_in_block_store[i].block->block_has_been_swept);
        }
    }

    if (_compaction_type_for_this_gc == gc_incremental_sliding_compaction) {
        if (_compaction_blocks_high_index == _number_of_blocks_in_heap) {
            // need to rotate back to beginning of heap since we just finished with last chunk of heap
            _compaction_blocks_low_index = 0;
            _compaction_blocks_high_index = _number_of_blocks_in_heap / _heap_compaction_ratio;
        } else {
            // Roll forward compaction area range for next GC cycle...this is still inside the heap...
            _compaction_blocks_low_index  = _compaction_blocks_low_index  + (_number_of_blocks_in_heap / _heap_compaction_ratio);
            _compaction_blocks_high_index = _compaction_blocks_high_index + (_number_of_blocks_in_heap / _heap_compaction_ratio);
        }
        if ((_number_of_blocks_in_heap - _compaction_blocks_high_index) < (_number_of_blocks_in_heap / _heap_compaction_ratio)) {
            // final section of heap incremental compaction...take care of the tail...compact till the very end of the heap....
            _compaction_blocks_high_index = _number_of_blocks_in_heap;
        }
    }
    // Just clear the compaction type...this needs to be re-initialized at the beginning of next GC
    _compaction_type_for_this_gc = gc_bogus_compaction;
}

// Use by all threads as hint
static volatile unsigned int alloc_ptr_block_index_hint = 0;

block_info * Block_Store::get_block_for_sliding_compaction_allocation_pointer_computation(unsigned int thread_id, block_info *curr_block) {
    return iter_get_next_compaction_block_for_gc_thread(thread_id, curr_block, NULL);
}

// Use by all threads as hint
static volatile unsigned int fix_slots_block_index_hint = 0;

block_info * Block_Store::get_block_for_fix_slots_to_compaction_live_objects(unsigned int thread_id, block_info *curr_block) {
    return iter_get_next_compaction_block_for_gc_thread(thread_id, curr_block, NULL);
}

// Use by all threads as hint
static volatile unsigned int slide_block_index_hint = 0;

block_info * Block_Store::get_block_for_slide_compact_live_objects(unsigned int thread_id, block_info *curr_block) {
    return iter_get_next_compaction_block_for_gc_thread(thread_id, curr_block, NULL);
}


// Iterator to get next block for compaction for current GC thread. This is idempotent and returns the
// block following cur_block.

block_info * Block_Store::iter_get_next_compaction_block_for_gc_thread(unsigned int gc_thread_id, block_info *curr_block,void *owner, bool search_for_owner) {
    assert(gc_thread_id < g_num_cpus);
    // compute range for thread and find next compaction block and return
    unsigned int low_index  = _per_gc_thread_compaction_blocks_low_index [gc_thread_id];
    unsigned int high_index = _per_gc_thread_compaction_blocks_high_index[gc_thread_id];
    // default is first block in thread's range
    unsigned int index = low_index;
    if (curr_block) {
        assert(curr_block->block_store_info_index >= low_index);
        assert(curr_block->block_store_info_index < high_index);
        // integrity of block_store_info_index
        assert(curr_block->block_store_info_index == (((uintptr_t) curr_block - (uintptr_t) _p_heap_base) >> GC_BLOCK_SHIFT_COUNT));
        assert(_blocks_in_block_store[curr_block->block_store_info_index].is_compaction_block);
        index = curr_block->block_store_info_index + 1;
    }
    while (index < high_index) {
        if (_blocks_in_block_store[index].is_compaction_block) {
            if(!search_for_owner || _blocks_in_block_store[index].block->thread_owner == owner) {
                assert(_blocks_in_block_store[index].block->is_compaction_block && _blocks_in_block_store[index].block->in_nursery_p);
                assert(_blocks_in_block_store[index].number_of_blocks == 1);
                return _blocks_in_block_store[index].block;
            }
        }
        index++;
    }
    assert(index == high_index);
    // Ran through last block for this thread
    return NULL;
}

Remembered_Set * Block_Store::get_slots_into_compaction_block(block_info *block) {
#ifdef _DEBUG
    unsigned int block_no = (unsigned int) (((POINTER_SIZE_INT) block - (POINTER_SIZE_INT) _p_heap_base) >> GC_BLOCK_SHIFT_COUNT);
    assert(block->block_store_info_index == block_no);
#endif // _DEBUG

    unsigned int block_number = block->block_store_info_index;

    for (unsigned int i = 0; i < g_num_cpus; i++) {
        Remembered_Set *thr_slots = _blocks_in_block_store[block_number].per_thread_slots_into_compaction_block[i];
        if (thr_slots) {
            _blocks_in_block_store[block_number].per_thread_slots_into_compaction_block[i] = NULL;
            // The remembered set will be freed by the GC thread that works on these slots.
            return thr_slots;
        }
    }
    return NULL;
}

void Block_Store::init_live_object_iterator_for_block(block_info *block) {
    unsigned int block_store_block_number =
        (unsigned int) (((uintptr_t) block - (uintptr_t) _p_heap_base) >> GC_BLOCK_SHIFT_COUNT);
    _blocks_in_block_store[block_store_block_number].bit_index_into_all_lives_in_block = 0;
}

Partial_Reveal_Object * Block_Store::get_next_live_object_in_block(block_info *block) {

    set_bit_search_info info;

    uint8 *mark_vector_base = &(block->mark_bit_vector[0]);

    // stop searching when we get close the the end of the vector
    info.p_ceil_byte = (uint8 *) ((uintptr_t)mark_vector_base + GC_SIZEOF_MARK_BIT_VECTOR_IN_BYTES);


#ifdef _DEBUG
    unsigned int bs_index = (unsigned int) (((POINTER_SIZE_INT) block - (POINTER_SIZE_INT) _p_heap_base) >> GC_BLOCK_SHIFT_COUNT);
    assert(bs_index == block->block_store_info_index);
#endif // _DEBUG

    unsigned int block_store_block_number = block->block_store_info_index;

    unsigned int bit_index = _blocks_in_block_store[block_store_block_number].bit_index_into_all_lives_in_block;

    info.p_start_byte = (uint8 *)((uintptr_t) mark_vector_base + (bit_index / GC_NUM_BITS_PER_BYTE));
    info.start_bit_index = (bit_index % GC_NUM_BITS_PER_BYTE);	// convert this to a subtract later...XXX

    get_next_set_bit(&info);

    if (verify_gc) {
        verify_get_next_set_bit_code(&info);
    }

    if (info.p_non_zero_byte == NULL) {
        _blocks_in_block_store[block_store_block_number].bit_index_into_all_lives_in_block = 0;
        return NULL;
    }

    // Compute the address of the live object
    unsigned int obj_bit_index = (unsigned int) ((info.p_non_zero_byte - mark_vector_base) * GC_NUM_BITS_PER_BYTE + info.bit_set_index);
    Partial_Reveal_Object *p_live_obj = (Partial_Reveal_Object *) ((uintptr_t)block + GC_BLOCK_INFO_SIZE_BYTES +  (obj_bit_index * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES));

#ifdef _DEBUG
    // this might fail if sliding compaction is turned on
    //	verify_object(p_live_obj, get_object_size_bytes(p_live_obj));
#endif // _DEBUG

    // Next time we need to start searching from the next bit onwards
    _blocks_in_block_store[block_store_block_number].bit_index_into_all_lives_in_block = obj_bit_index + 1;

    return p_live_obj;
}
