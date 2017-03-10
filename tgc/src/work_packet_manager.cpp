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
#include <memory.h>
#include <assert.h>
#include <time.h>

// GC header files
#include "tgc/gc_cout.h"
#include "tgc/gc_header.h"
#include "tgc/gc_v4.h"
#include "tgc/work_packet_manager.h"
#include "tgc/gcv4_synch.h"

#ifdef ORP_POSIX
#define InterlockedIncrement(x) (assert(0))
#define InterlockedDecrement(x) (assert(0))
#endif // ORP_POSIX


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern bool verify_gc;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Work_Packet_Manager::Work_Packet_Manager()
{
	_num_total_work_packets = 0;
#ifndef USE_LOCK_PER_QUEUE
	_full_work_packets = NULL;
	_num_full_work_packets = 0;;
	_empty_work_packets = NULL;
	_num_empty_work_packets = 0;
	_almost_full_work_packets = NULL;
	_num_almost_full_work_packets = 0;
	_almost_empty_work_packets = NULL;
	_num_almost_empty_work_packets = 0;
#endif
#ifdef USE_ONE_LOCK
	InitializeCriticalSection(&big_lock);
#endif // USE_ONE_LOCK
}

Work_Packet_Manager::~Work_Packet_Manager()
{
#ifdef USE_ONE_LOCK
#endif // USE_ONE_LOCK
}

Work_Packet *
Work_Packet_Manager::get_full_work_packet()
{
#ifdef USE_ONE_LOCK
#ifdef USE_LOCK_PER_QUEUE
	return m_full_queue.deque();
#else // !USE_LOCK_PER_QUEUE
	LockProtector lp(&big_lock);
    Work_Packet *wp = _full_work_packets;
	if(wp != NULL) {
		_full_work_packets = wp->get_next();
		wp->set_next(NULL);
		_num_full_work_packets--;
	}
#ifdef _DEBUG
	int num_actual_packets = 0;
	Work_Packet *count = (Work_Packet *) _full_work_packets;
	while (count) {
		num_actual_packets++;
		assert(count->is_full());
		count = count->get_next();
	}
	if (num_actual_packets != _num_full_work_packets) {
		printf("Mismatch in full work packets accounting at the end of GC...%d supposed to be there...only %d found\n", num_actual_packets, _num_empty_work_packets);
		assert(0);
	}
#endif // _DEBUG

	return wp;
#endif // !USE_LOCK_PER_QUEUE

#else
	while (_full_work_packets != NULL) {

		POINTER_SIZE_INT old_val = (POINTER_SIZE_INT)_full_work_packets;
		if (old_val) {
			POINTER_SIZE_INT wp_next = (POINTER_SIZE_INT) ((Work_Packet *)old_val)->get_next();
			POINTER_SIZE_INT val =
				LockedCompareExchangePOINTER_SIZE_INT((POINTER_SIZE_INT *)&_full_work_packets,
														wp_next,
														old_val
													 );
			if (val && (old_val == val)) {
				((Work_Packet *)val)->set_next(NULL);
				InterlockedDecrement((LPLONG) (&_num_full_work_packets));
				return (Work_Packet *)val;
			}
		}
	}
	return NULL;
#endif // USE_ONE_LOCK
}


Work_Packet *
Work_Packet_Manager::get_almost_full_work_packet()
{
#ifdef USE_ONE_LOCK
#ifdef USE_LOCK_PER_QUEUE
	return m_almost_full_queue.deque();
#else // !USE_LOCK_PER_QUEUE
	LockProtector lp(&big_lock);
    Work_Packet *wp = _almost_full_work_packets;
	if(wp != NULL) {
		_almost_full_work_packets = wp->get_next();
		wp->set_next(NULL);
		_num_almost_full_work_packets--;
	}

#ifdef _DEBUG
	int num_actual_packets = 0;
	Work_Packet *count = (Work_Packet *) _almost_full_work_packets;
	while (count) {
		num_actual_packets++;
		count = count->get_next();
	}
	if (num_actual_packets != _num_almost_full_work_packets) {
		printf("Mismatch in full work packets accounting at the end of GC...%d supposed to be there...only %d found\n", num_actual_packets, _num_empty_work_packets);
		assert(0);
	}
#endif // _DEBUG

	return wp;
#endif // !USE_LOCK_PER_QUEUE
#else
	while (_almost_full_work_packets != NULL) {

		POINTER_SIZE_INT old_val = (POINTER_SIZE_INT)_almost_full_work_packets;
		if (old_val) {
			POINTER_SIZE_INT wp_next = (POINTER_SIZE_INT) ((Work_Packet *)old_val)->get_next();
			POINTER_SIZE_INT val =
				LockedCompareExchangePOINTER_SIZE_INT((POINTER_SIZE_INT *)&_almost_full_work_packets,
														wp_next,
														old_val
													 );
			if (val && (old_val == val)) {
				((Work_Packet *)val)->set_next(NULL);
				InterlockedDecrement((LPLONG) (&_num_almost_full_work_packets));
				return (Work_Packet *)val;
			}
		}
	}
	return NULL;
#endif // USE_ONE_LOCK
}


Work_Packet *
Work_Packet_Manager::get_empty_work_packet(bool dont_fail_me)
{
#ifdef USE_ONE_LOCK
#ifdef USE_LOCK_PER_QUEUE
	Work_Packet *wp = m_empty_queue.deque();
	if(!wp && dont_fail_me) {
		InterlockedIncrement((LPLONG) (&_num_total_work_packets));
//		_num_total_work_packets++;
		wp = new Work_Packet();
	}
	return wp;
#else // !USE_LOCK_PER_QUEUE
	LockProtector lp(&big_lock);
    Work_Packet *wp = _empty_work_packets;
	if(wp != NULL) {
		_empty_work_packets = wp->get_next();
		wp->set_next(NULL);
		_num_empty_work_packets--;
	}
	if(!wp && dont_fail_me) {
		_num_total_work_packets++;
		wp = new Work_Packet();
	}

#ifdef _DEBUG
	int num_actual_packets = 0;
	Work_Packet *count = (Work_Packet *) _empty_work_packets;
	while (count) {
		num_actual_packets++;
		assert(count->is_empty());
		count = count->get_next();
	}
	if (num_actual_packets != _num_empty_work_packets) {
		printf("Mismatch in full work packets accounting at the end of GC...%d supposed to be there...only %d found\n", num_actual_packets, _num_empty_work_packets);
		assert(0);
	}
#endif

	return wp;
#endif // !USE_LOCK_PER_QUEUE
#else
	while (_empty_work_packets != NULL) {

		POINTER_SIZE_INT old_val = (POINTER_SIZE_INT)_empty_work_packets;
		if (old_val) {
			POINTER_SIZE_INT wp_next = (POINTER_SIZE_INT) ((Work_Packet *)old_val)->get_next();
			POINTER_SIZE_INT val =
				LockedCompareExchangePOINTER_SIZE_INT((POINTER_SIZE_INT *)&_empty_work_packets,
														wp_next,
														old_val
													 );
			if (val && (old_val == val)) {
				((Work_Packet *)val)->set_next(NULL);
				InterlockedDecrement((LPLONG) (&_num_empty_work_packets));
				return (Work_Packet *)val;
			}
		}
	}

	if (dont_fail_me) {
		// Create and return a new packet....
		InterlockedIncrement((LPLONG) (&_num_total_work_packets));
		return new Work_Packet();
	} else {
		return NULL;
	}
#endif // USE_ONE_LOCK
}


Work_Packet *
Work_Packet_Manager::get_almost_empty_work_packet()
{
#ifdef USE_ONE_LOCK
#ifdef USE_LOCK_PER_QUEUE
	return m_almost_empty_queue.deque();
#else // !USE_LOCK_PER_QUEUE
	LockProtector lp(&big_lock);
    Work_Packet *wp = _almost_empty_work_packets;
	if(wp != NULL) {
		_almost_empty_work_packets = wp->get_next();
		wp->set_next(NULL);
		_num_almost_empty_work_packets--;
	}

#ifdef _DEBUG
	int num_actual_packets = 0;
	Work_Packet *count = (Work_Packet *) _almost_empty_work_packets;
	while (count) {
		num_actual_packets++;
		count = count->get_next();
	}
	if (num_actual_packets != _num_almost_empty_work_packets) {
		printf("Mismatch in full work packets accounting at the end of GC...%d supposed to be there...only %d found\n", num_actual_packets, _num_empty_work_packets);
		assert(0);
	}
#endif // _DEBUG

	return wp;
#endif // !USE_LOCK_PER_QUEUE
#else
	while (_almost_empty_work_packets != NULL) {

		POINTER_SIZE_INT old_val = (POINTER_SIZE_INT)_almost_empty_work_packets;
		if (old_val) {
			POINTER_SIZE_INT wp_next = (POINTER_SIZE_INT) ((Work_Packet *)old_val)->get_next();
			POINTER_SIZE_INT val =
				LockedCompareExchangePOINTER_SIZE_INT((POINTER_SIZE_INT *)&_almost_empty_work_packets,
														wp_next,
														old_val
													 );
			if (val && (old_val == val)) {
				((Work_Packet *)val)->set_next(NULL);
				InterlockedDecrement((LPLONG) (&_num_almost_empty_work_packets));
				return (Work_Packet *)val;
			}
		}
	}
	return NULL;
#endif // USE_ONE_LOCK
}


void
Work_Packet_Manager::return_work_packet(Work_Packet *wp)
{
#ifdef USE_ONE_LOCK
#ifdef USE_LOCK_PER_QUEUE
	if (wp->fullness() == packet_full) {
		m_full_queue.enqueue(wp);
	} else if (wp->fullness() == packet_empty) {
		m_empty_queue.enqueue(wp);
	} else if (wp->fullness() == packet_almost_full) {
		m_almost_full_queue.enqueue(wp);
	} else {
		assert(wp->fullness() == packet_almost_empty);
		m_almost_empty_queue.enqueue(wp);
	}
#else // !USE_LOCK_PER_QUEUE
	LockProtector lp(&big_lock);

	// this packet is part of no list (no context)
	if (wp->get_next() != NULL) {
		assert(0);
		printf("Work packet returned was still connected to some list...\n");
		orp_exit(17048);
	}

	Work_Packet **ptr = NULL;

	if (wp->fullness() == packet_full) {
		ptr = &_full_work_packets;
		_num_full_work_packets++;
	} else if (wp->fullness() == packet_empty) {
		ptr = &_empty_work_packets;
		_num_empty_work_packets++;
	} else if (wp->fullness() == packet_almost_full) {
		ptr = &_almost_full_work_packets;
		_num_almost_full_work_packets++;
	} else {
		assert(wp->fullness() == packet_almost_empty);
		ptr = &_almost_empty_work_packets;
		_num_almost_empty_work_packets++;
	}

	assert(ptr);

	Work_Packet *old_val = (Work_Packet *) *ptr;
	wp->set_next((Work_Packet *)old_val);

	*ptr = wp;
#endif // !USE_LOCK_PER_QUEUE
#else
	// this packet is part of no list (no context)
	if (wp->get_next() != NULL) {
		assert(0);
		printf("Work packet returned was still connected to some list...\n");
		orp_exit(17049);
	}

	volatile Work_Packet **ptr = NULL;

	if (wp->fullness() == packet_full) {
		ptr = &_full_work_packets;
		InterlockedIncrement((LPLONG) (&_num_full_work_packets));
	} else if (wp->fullness() == packet_empty) {
		ptr = &_empty_work_packets;
		InterlockedIncrement((LPLONG) (&_num_empty_work_packets));
	} else if (wp->fullness() == packet_almost_full) {
		ptr = &_almost_full_work_packets;
		InterlockedIncrement((LPLONG) (&_num_almost_full_work_packets));
	} else {
		assert(wp->fullness() == packet_almost_empty);
		ptr = &_almost_empty_work_packets;
		InterlockedIncrement((LPLONG) (&_num_almost_empty_work_packets));
	}

	assert(ptr);

	while (true) {
		Work_Packet *old_val = (Work_Packet *) *ptr;
		wp->set_next((Work_Packet *)old_val);

		POINTER_SIZE_INT val =
			LockedCompareExchangePOINTER_SIZE_INT(	(POINTER_SIZE_INT *)ptr,
													(POINTER_SIZE_INT) wp,
													(POINTER_SIZE_INT) old_val
												);
		if (val == (POINTER_SIZE_INT) old_val) {
			return ;
		}
	}
#endif // USE_ONE_LOCK
}



Work_Packet *
Work_Packet_Manager::get_output_work_packet() {

	Work_Packet *wp = get_empty_work_packet(false);
	if (wp == NULL) {
		wp = get_almost_empty_work_packet();
	}
	if (wp == NULL) {
		// dont fail me this time
		wp = get_empty_work_packet(true);
	}
	assert(wp);

	if (wp) {
		// this packet loses its context
		wp->set_next(NULL);
	}
	return wp;
}


Work_Packet *
Work_Packet_Manager::get_input_work_packet() {

	Work_Packet *wp = get_full_work_packet();
	if (wp == NULL) {
		wp = get_almost_full_work_packet();
	}
	if (wp == NULL) {
		wp = get_almost_empty_work_packet();
	}
	if (wp) {
		// this packet loses its context
		wp->set_next(NULL);
	}
	return wp;
}



void
Work_Packet_Manager::_dump_state()
{
	printf("==========================================================================\n");
	printf("_num_total_work_packets = %d\n", _num_total_work_packets);
#ifdef USE_LOCK_PER_QUEUE
	printf("_num_empty_work_packets = %d\n", m_empty_queue.get_packet_count_unsafe());
	printf("_num_full_work_packets = %d\n", m_full_queue.get_packet_count_unsafe());
	printf("_num_almost_full_work_packets = %d\n", m_almost_full_queue.get_packet_count_unsafe());
	printf("_num_almost_empty_work_packets = %d\n", m_almost_empty_queue.get_packet_count_unsafe());
#else // USE_LOCK_PER_QUEUE
	printf("_num_empty_work_packets = %d\n", _num_empty_work_packets);
	printf("_num_full_work_packets = %d\n", _num_full_work_packets);
	printf("_num_almost_full_work_packets = %d\n", _num_almost_full_work_packets);
	printf("_num_almost_empty_work_packets = %d\n", _num_almost_empty_work_packets);
	printf("_empty_work_packets = %p\n", _empty_work_packets);
	printf("_full_work_packets = %p\n", _full_work_packets);
	printf("_almost_full_work_packets = %p\n", _almost_full_work_packets);
	printf("_almost_empty_work_packets = %p\n", _almost_empty_work_packets);
#endif // !USE_LOCK_PER_QUEUE
}


bool
Work_Packet_Manager::wait_till_there_is_work_or_no_work()
{
	clock_t start, finish;
	start = clock();

	while (true) {
#ifdef USE_LOCK_PER_QUEUE
		if (!m_full_queue.is_empty() ||
			!m_almost_full_queue.is_empty() ||
			!m_almost_empty_queue.is_empty()) {
			// there is some work to be done....
			return true;
		}

		if (m_empty_queue.get_packet_count_unsafe() == _num_total_work_packets) {
			return false;
		}
#else // !USE_LOCK_PER_QUEUE
		if ((_full_work_packets != NULL) ||
			(_almost_empty_work_packets != NULL) ||
			(_almost_full_work_packets != NULL)) {
			// there is some work to be done....
			return true;
		}

		if (_num_empty_work_packets == _num_total_work_packets) {
			return false;
		}
#endif // !USE_LOCK_PER_QUEUE

#if 0
		if ((_full_work_packets == NULL) &&
			(_almost_empty_work_packets == NULL) &&
			(_almost_full_work_packets == NULL)) {

			if ((_num_full_work_packets == 0) &&
				(_num_almost_full_work_packets == 0) &&
				(_num_almost_empty_work_packets == 0)) {

					return false;
			}
		}
#endif

		orp_thread_sleep(0);

#ifndef _IA64_
#ifndef ORP_POSIX
		__asm {
			pause
		}
#endif // ORP_POSIX
#endif // _IA64_


		finish = clock();
		if (((finish - start) / CLOCKS_PER_SEC) > 5) {
#ifndef USE_LOCK_PER_QUEUE
			int num_actual_packets = 0;
			Work_Packet *wp = (Work_Packet *) _empty_work_packets;
			while (wp) {
				num_actual_packets++;
				assert(wp->is_empty());
				wp = wp->get_next();
			}
			if (num_actual_packets != _num_empty_work_packets) {
				printf("Mismatch in empty work packets accounting in wait until work GC...%d supposed to be there...%d found\n", num_actual_packets, _num_empty_work_packets);
			}
#endif // !USE_LOCK_PER_QUEUE

			// if I have waited for over 5 seconds....terminate.
			printf("WAITED TOO LONG FOR WORK THAT APPARENTLY IS AVAILABLE!!\n");
			_dump_state();
			orp_exit(17050);
		}

	}
}





void
Work_Packet_Manager::verify_after_gc()
{
#ifdef USE_LOCK_PER_QUEUE
	if (!m_full_queue.is_empty() || !m_almost_full_queue.is_empty() || !m_almost_empty_queue.is_empty() ||
		m_empty_queue.get_packet_count_unsafe() != _num_total_work_packets) {
		// Bad termination of mark/scan phase...
		printf("BAD values in _mark_scan_pool...unfinished phase...\n");
		_dump_state();
		orp_exit(17051);
	}
#else // !USE_LOCK_PER_QUEUE
	if (_num_almost_empty_work_packets || _num_full_work_packets || _num_almost_full_work_packets ||
		_almost_empty_work_packets || _full_work_packets || _almost_full_work_packets ||
		(_num_almost_empty_work_packets + _num_full_work_packets + _num_almost_full_work_packets + _num_empty_work_packets != _num_total_work_packets)
		) {
		// Bad termination of mark/scan phase...
		printf("BAD values in _mark_scan_pool...unfinished phase...\n");
		_dump_state();
		orp_exit(17052);
	}
	assert(_empty_work_packets);
	assert(_num_empty_work_packets > 0);


#ifndef _DEBUG
	if (verify_gc) {
#endif // _DEBUG
		int num_actual_packets = 0;
		Work_Packet *wp = (Work_Packet *) _empty_work_packets;
		while (wp) {
			num_actual_packets++;
			assert(wp->is_empty());
			wp = wp->get_next();
		}
		assert(num_actual_packets == _num_empty_work_packets);
		if (num_actual_packets != _num_empty_work_packets) {
			printf("Mismatch in empty work packets accounting at the end of GC...%d supposed to be there...only %d found\n", num_actual_packets, _num_empty_work_packets);
			orp_exit(17053);
		}
#ifndef _DEBUG
	}
#endif // _DEBUG
#endif // !USE_LOCK_PER_QUEUE
}
