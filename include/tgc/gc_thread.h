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

#ifndef _gc_thread_H_
#define _gc_thread_H_

class Garbage_Collector;

#include "tgc/garbage_collector.h"

class GC_Thread {

public:

	GC_Thread(Garbage_Collector *, unsigned int);

    virtual ~GC_Thread();

	// This doesnt need to be a public -- expose using an accessor
	Garbage_Collector *_p_gc;

	void reset(bool);

	void wait_for_work();

	void signal_work_is_done();

	inline gc_thread_action get_task_to_do() {
		return _task_to_do;
	}

	inline void set_task_to_do(gc_thread_action task) {
		_task_to_do = task;
	}

	inline ThreadThreadHandle get_thread_handle() {
		return _thread_handle;
	}

	inline bool is_compaction_turned_on_during_this_gc() {
		return _compaction_turned_on_during_this_gc;
	}

	inline SynchEventHandle get_gc_thread_work_done_event_handle() {
		return _gc_thread_work_done_event;
	}

	inline SynchEventHandle get_gc_thread_start_work_event_handle() {
		return _gc_thread_start_work_event;
	}

	inline MARK_STACK *get_mark_stack() {
		return _mark_stack;
	}

	inline unsigned int get_num_marked_objects() {
		return _num_marked_objects;
	}

	inline unsigned int get_marked_object_size() {
		return _marked_object_size;
	}

	inline unsigned int get_num_bytes_recovered_by_sweep() {
		return _num_bytes_recovered_by_sweep;
	}

	inline void set_num_bytes_recovered_by_sweep(unsigned int bytes) {
		_num_bytes_recovered_by_sweep = bytes;
	}

	inline Partial_Reveal_Object *get_marked_object(unsigned int k) {
#ifdef _DEBUG
		assert(k < _num_marked_objects);
		assert((*_marked_objects)[k]);
		return (*_marked_objects)[k];
#else  // _DEUBG
		dprintf("BAD USE OF get_marked_object()...");
		orp_exit(17055);
        return 0;
#endif // _DEBUG
	}

	inline void add_to_marked_objects(Partial_Reveal_Object *p_obj) {
#ifdef _DEBUG
		assert(p_obj);
		_marked_objects->push_back(p_obj);
		_num_marked_objects++;
#else  // _DEBUG
		_num_marked_objects++;
#endif // _DEBUG
        if(stats_gc) {
            _marked_object_size += get_object_size_bytes_with_vt(p_obj,p_obj->vt());
        }
	}

    inline void add_weak_slot(Partial_Reveal_Object **slot) {
        _weak_slots->push_back(slot);
    }

    inline void clear_weak_slots(void) {
        _weak_slots->clear();
    }

	inline int get_sweep_start_index() {
		return _sweep_start_index;
	}

	inline void set_sweep_start_index(int index) {
		_sweep_start_index = index;
	}

	inline int get_num_chunks_to_sweep() {
		return _num_chunks_to_sweep;
	}

	inline void set_num_chunks_to_sweep(int num) {
		_num_chunks_to_sweep = num;
	}

	inline void add_to_num_chunks_to_sweep(int num) {
		_num_chunks_to_sweep += num;
	}

	inline void set_chunk_average_number_of_free_areas(unsigned int chunk_index, unsigned int num) {
		_sweep_stats[chunk_index].average_number_of_free_areas = num;
	}

	inline void set_chunk_average_size_per_free_area(unsigned int chunk_index, unsigned int sz) {
		_sweep_stats[chunk_index].average_size_per_free_area = sz;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

	inline Work_Packet *get_input_packet() {
		return _input_packet;
	}

	inline void set_input_packet(Work_Packet *wp) {
		_input_packet = wp;
	}

	inline Work_Packet *get_output_packet() {
		return _output_packet;
	}

	inline void set_output_packet(Work_Packet *wp) {
		_output_packet = wp;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////


	inline unsigned int get_id() {
		return _id;
	}

	inline unsigned int get_num_slots_collected_for_later_fixing() {
		return _num_slots_collected_for_later_fixing;
	}

	inline void increment_num_slots_collected_for_later_fixing() {
		_num_slots_collected_for_later_fixing++;
	}


	ExpandInPlaceArray<ForwardedObject> m_forwards;
///////////////////////////////////////////////////////////////////////////////////////////////////////////

private:
	unsigned int _id;
	MARK_STACK *_mark_stack;
	int _sweep_start_index;
	int _num_chunks_to_sweep;
	chunk_sweep_stats _sweep_stats[GC_MAX_CHUNKS];
    SynchEventHandle _thread_handle;
    unsigned int _thread_id;
	SynchEventHandle _gc_thread_start_work_event;
	SynchEventHandle _gc_thread_work_done_event;
	gc_thread_action _task_to_do;
	unsigned int _clear_mark_start_index ;
	unsigned int _num_marks_to_clear;

	std::vector<Partial_Reveal_Object*> *_marked_objects;
	std::vector<Partial_Reveal_Object**> *_weak_slots;

	unsigned int _num_marked_objects;
    unsigned int _marked_object_size;
	unsigned int _num_bytes_recovered_by_sweep;

	/////////////////////////////////////////////////////////////////////////////
	Work_Packet *_input_packet;
	Work_Packet *_output_packet;
	/////////////////////////////////////////////////////////////////////////////

	bool _compaction_turned_on_during_this_gc;
	unsigned int _num_slots_collected_for_later_fixing;
public:
    int started;

    std::set<Partial_Reveal_Object *> m_global_marks;
    sweep_stats sweeping_stats;
};

#endif // _gc_thread_H_
