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

#ifndef _work_packet_manager_H_
#define _work_packet_manager_H_

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const int GC_SIZEOF_WORK_PACKET = 200;

enum packet_emptiness {
	packet_full,
	packet_empty,
	packet_almost_full,
	packet_almost_empty
};


class Work_Packet {

public:
	Work_Packet() {
		memset(_work_units, 0, sizeof(void *) * GC_SIZEOF_WORK_PACKET);
		_current_work_unit_index = _num_work_units_in_packet = 0;
		_next = NULL;
	}

	inline bool is_empty() {
		return (_num_work_units_in_packet == 0);
	}

	inline bool is_full() {
		return (_num_work_units_in_packet == GC_SIZEOF_WORK_PACKET);
	}

	inline unsigned int get_num_work_units_in_packet() {
		return _num_work_units_in_packet;
	}

	inline packet_emptiness fullness() {
		if (_num_work_units_in_packet == 0) {
			return packet_empty;
		} else if (_num_work_units_in_packet == GC_SIZEOF_WORK_PACKET) {
			return packet_full;
		} else if (_num_work_units_in_packet <= (GC_SIZEOF_WORK_PACKET / 4)) {
			return packet_almost_empty;
		} else {
			return packet_almost_full;
		}
	}

	/////////////////////////////////////////////////////////
	inline Work_Packet *get_next() {
		return _next;
	}
	
	inline void set_next(Work_Packet *wp) {
		_next = wp;
	}
	/////////////////////////////////////////////////////////

	// iterator to remove units of work from the work packet
	inline void init_work_packet_iterator() {
		assert(_num_work_units_in_packet > 0); 
		if (_num_work_units_in_packet > 0) {
			assert(_num_work_units_in_packet <= GC_SIZEOF_WORK_PACKET);		
			_current_work_unit_index = _num_work_units_in_packet - 1;
		} else {
			assert(_num_work_units_in_packet == 0);
			_current_work_unit_index = 0;
		}
	}

	inline void *remove_next_unit_of_work() {
		if (_num_work_units_in_packet == 0) {
			return NULL;
		}
		assert(_num_work_units_in_packet <= GC_SIZEOF_WORK_PACKET);		
		// assert(_current_work_unit_index >= 0); // pointless comparison : is unsiagned
		assert(_current_work_unit_index < GC_SIZEOF_WORK_PACKET);

		void *ret = _work_units[_current_work_unit_index];
		_work_units[_current_work_unit_index] = NULL;
		if (_current_work_unit_index > 0) {
			_current_work_unit_index--;
		}
		assert(_num_work_units_in_packet > 0);
		_num_work_units_in_packet--; 
		return ret;
	}

	inline void reset_work_packet() {
		assert(_num_work_units_in_packet == 0);
		memset(_work_units, 0, sizeof(void *) * GC_SIZEOF_WORK_PACKET);
		_current_work_unit_index = _num_work_units_in_packet = 0;
		_next = NULL;
	}

	inline bool work_packet_has_space_to_add() {
		return (_num_work_units_in_packet < GC_SIZEOF_WORK_PACKET);
	}

	// add work unit to work packet
	inline void add_unit_of_work(void *work_unit) {
		assert(_num_work_units_in_packet < GC_SIZEOF_WORK_PACKET);
		assert(_work_units[_num_work_units_in_packet] == NULL);
		_work_units[_num_work_units_in_packet] = work_unit;
		_num_work_units_in_packet++;
	}

private:

	void *_work_units[GC_SIZEOF_WORK_PACKET];
	unsigned int _num_work_units_in_packet;
	unsigned int _current_work_unit_index;
	
	Work_Packet *_next;
};

//#define USE_ONE_LOCK
//#define USE_LOCK_PER_QUEUE

#ifdef USE_ONE_LOCK

class LockProtector {
protected:
	CRITICAL_SECTION *the_lock;
public:
	LockProtector(CRITICAL_SECTION* lock) {
		the_lock = lock;
		EnterCriticalSection(the_lock);
	}
	~LockProtector(void) {
		LeaveCriticalSection(the_lock);
	}
};

class Work_Queue {
protected:
	Work_Packet *m_packets;
	int m_packet_count;
	CRITICAL_SECTION big_lock;
public:
	Work_Queue(void) : m_packets(NULL), m_packet_count(0) {
		InitializeCriticalSection(&big_lock);
	}

	bool is_empty(void) const {
		return m_packets==NULL;
	}

	void enqueue(Work_Packet *wp) {
		LockProtector lp(&big_lock);
		if (wp->get_next() != NULL) {
			assert(0);
			printf("Work packet returned was still connected to some list...\n");
			orp_exit(17065);
			// next usable exit() code is 17071
		}
	
		wp->set_next(m_packets);
		m_packets = wp;
		m_packet_count++;
	}

    Work_Packet * deque(void) {
		LockProtector lp(&big_lock);
		Work_Packet *wp = m_packets;
		if(wp != NULL) {
			m_packets = wp->get_next();
			wp->set_next(NULL);
			m_packet_count--;
		}

#ifdef _DEBUG
		int num_actual_packets = 0;
		Work_Packet *count = (Work_Packet *) m_packets;
		while (count) {
			num_actual_packets++;
			assert(count->is_full());
			count = count->get_next();
		}
		if (num_actual_packets != m_packet_count) {
			printf("Mismatch in work packets accounting at the end of GC...%d supposed to be there...only %d found\n", m_packet_count, num_actual_packets);
			assert(0);
		}
#endif

		return wp;
	}

	int get_packet_count_unsafe(void) const {
		return m_packet_count;
	}
};

#endif // USE_ONE_LOCK

class Work_Packet_Manager {
public:
	Work_Packet_Manager();
	~Work_Packet_Manager();

	Work_Packet *get_full_work_packet();
	Work_Packet *get_almost_full_work_packet();
	Work_Packet *get_empty_work_packet(bool);
	Work_Packet *get_almost_empty_work_packet();

	void return_work_packet(Work_Packet *);

	//...some more...
	Work_Packet *get_input_work_packet();
	Work_Packet *get_output_work_packet();

	// termination for any thread...
	bool wait_till_there_is_work_or_no_work();

	void verify_after_gc();

private:
	void _dump_state();

#ifdef USE_ONE_LOCK
	CRITICAL_SECTION big_lock;
	int _num_total_work_packets;

#ifdef USE_LOCK_PER_QUEUE
	Work_Queue m_full_queue;
	Work_Queue m_empty_queue;
	Work_Queue m_almost_full_queue;
	Work_Queue m_almost_empty_queue;
#else
	Work_Packet *_full_work_packets;
	int _num_full_work_packets;

	Work_Packet *_empty_work_packets;
	int _num_empty_work_packets;

	Work_Packet *_almost_full_work_packets;
	int _num_almost_full_work_packets;

	Work_Packet *_almost_empty_work_packets;
	int _num_almost_empty_work_packets;
#endif
#if 0
	Work_Packet *_full_work_packets;
	int _num_full_work_packets;

	Work_Packet *_empty_work_packets;
	int _num_empty_work_packets;

	Work_Packet *_almost_full_work_packets;
	int _num_almost_full_work_packets;

	Work_Packet *_almost_empty_work_packets;
	int _num_almost_empty_work_packets;
#endif
#else
	volatile int _num_total_work_packets;

	volatile Work_Packet *_full_work_packets;
	volatile int _num_full_work_packets;

	volatile Work_Packet *_empty_work_packets;
	volatile int _num_empty_work_packets;

	volatile Work_Packet *_almost_full_work_packets;
	volatile int _num_almost_full_work_packets;

	volatile Work_Packet *_almost_empty_work_packets;
	volatile int _num_almost_empty_work_packets;
#endif // USE_ONE_LOCK
};


Work_Packet *get_output_work_packet(Work_Packet_Manager *wpm);
Work_Packet *get_input_work_packet(Work_Packet_Manager *wpm);

#endif // _work_packet_manager_H_


