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
#include "tgc/mark.h"
#include "tgc/gcv4_synch.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern bool get_num_consecutive_similar_bits(uint8 *, unsigned int, unsigned int *, uint8 *);
// used to debug/verify "get_num_consecutive_similar_bits" above....
extern bool verify_consec_bits_using_asm(uint8 *, unsigned int , unsigned int *, uint8 *);


//////////////////////////////////  S W E E P  /////////////////////////////////////////////////////////////////////////////////////

void Garbage_Collector::prepare_to_sweep_heap() {
	// Divide all chunks up for the sweep.
	// Also, give each thread some marks to clear

	assert(_free_chunks_end_index > 0);
	int num_chunks_per_thread = (_free_chunks_end_index + 1) / g_num_cpus;
	int num_extra_chunks_for_last_thread = (_free_chunks_end_index + 1) % g_num_cpus;
	assert(num_chunks_per_thread > 0);

	for (unsigned int i = 0, chunks_sweep_offset = 0; i < g_num_cpus; i++) {
		assert(_gc_threads[i]->get_sweep_start_index() == -1);
		assert(_gc_threads[i]->get_num_chunks_to_sweep() == -1);

		_gc_threads[i]->set_sweep_start_index(chunks_sweep_offset);
		_gc_threads[i]->set_num_chunks_to_sweep(num_chunks_per_thread);

		chunks_sweep_offset += num_chunks_per_thread;
	}

	if (num_extra_chunks_for_last_thread) {
		_gc_threads[g_num_cpus -1]->add_to_num_chunks_to_sweep(num_extra_chunks_for_last_thread);
	}
}


unsigned int Garbage_Collector::sweep_heap(GC_Thread *gc_thread, sweep_stats &stats) {
	unsigned int num_blocks_swept = 0;
	unsigned int num_bytes_recovered_for_allocation = 0;

	unsigned int num_active_chunks = 0;

	int start_chunk_index = gc_thread->get_sweep_start_index();
	int end_chunk_index   = start_chunk_index + gc_thread->get_num_chunks_to_sweep() - 1;

	// Process each chunk separately
	for (int chunk_index = start_chunk_index; chunk_index <= end_chunk_index; chunk_index++) {

		// continue if empty chunk
		if (_gc_chunks[chunk_index].chunk == NULL) {
			continue;
		}

		unsigned int num_blocks_in_chunk = 0;
		unsigned int num_free_bytes_in_chunk = 0;
		unsigned int num_free_areas_in_chunk = 0;

		// Sweep all blocks in this chunk
		block_info *block = _gc_chunks[chunk_index].chunk;

		bool active_chunk = (block->get_nursery_status() == active_nursery);
		if (active_chunk) {
			assert(_gc_chunks[chunk_index].free_chunk == NULL);
			num_active_chunks++;
		}

		bool free_chunk = (block->get_nursery_status() == free_nursery);
		if (free_chunk) {
			assert(_gc_chunks[chunk_index].chunk == _gc_chunks[chunk_index].free_chunk);
		}

		// I dont need to sweep a free chunk, DO I??! --- TO DO
		while (block) {
			num_blocks_in_chunk++;

            if(g_gen && g_gen_all == false && block->generation) {
    			block = block->next_free_block;
                continue;
            }

			// Sweep only non-compacted blocks
			if (gc_thread->is_compaction_turned_on_during_this_gc() && is_compaction_block(block)) {
				// this automatically gets swept....nothing to do...
				assert(block->block_has_been_swept == true);
			} else {
				num_bytes_recovered_for_allocation += sweep_one_block(block, stats);
			}

			num_free_areas_in_chunk += block->num_free_areas_in_block;
			num_free_bytes_in_chunk += block->block_free_bytes;

			if (active_chunk) {
				// each block in an active chunk better be active; since they are recycled together
				assert(block->get_nursery_status() == active_nursery);

			} else if (free_chunk) {
				assert(block->get_nursery_status() == free_nursery);

			} else {
				// It better be spent in this case. We will convert it to FREE now that it has been swept.
				assert(block->get_nursery_status() == spent_nursery);
				block->set_nursery_status(spent_nursery,free_nursery);
			}

			num_blocks_swept++;

			// Move to next block in current chunk
			block = block->next_free_block;
		}

		if (num_blocks_in_chunk && num_free_areas_in_chunk) {
			gc_thread->set_chunk_average_number_of_free_areas(chunk_index - start_chunk_index, num_free_areas_in_chunk / num_blocks_in_chunk );
			gc_thread->set_chunk_average_size_per_free_area(chunk_index - start_chunk_index, num_free_bytes_in_chunk / num_free_areas_in_chunk );
		}

		// Have swept the whole chunk....Now...restore the processed chunk IF it is not held by some thread
		if (active_chunk == false) {
			_gc_chunks[chunk_index].free_chunk = _gc_chunks[chunk_index].chunk;
		}

	} // for


	return num_bytes_recovered_for_allocation;
} // sweep_heap
