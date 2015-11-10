/*
 * COPYRIGHT_NOTICE_1
 */

#ifndef _SLOT_OFFSET_LIST_H_
#define _SLOT_OFFSET_LIST_H_

//
// An Object List is used to keep track of lists of objects when
// a hash table-style remembered set or object set is overkill.
//

#include "tgc/gc_header.h"

template <class T>
class ExpandInPlaceArray {
public:
	ExpandInPlaceArray(unsigned initial_size=100) {
		m_initial_size = initial_size;

	    _size_in_entries = m_initial_size;

	    _store = (store_node*)malloc(sizeof(store_node));

	    if (_store==NULL) {
	        printf ("Error: malloc failed while creating slot offset list.\n");
	        assert(0);
	        orp_exit(17061);
	    }

	    start_node = _store;

	    _store->_store = (T *)malloc(m_initial_size * sizeof(T));
	    _store->next = NULL;

	    if (_store->_store==NULL) {
	        printf ("Error: malloc failed while creating slot offset list.\n");
	        assert(0);
	        orp_exit(17062);
	    }

	    _resident_count  = 0;
	    _total_count     = 0;
	}

	~ExpandInPlaceArray(void) {
	    store_node *temp = start_node;
	    while(temp) {
	        start_node = temp->next;
	        free(temp->_store);
	        free(temp);
	        temp = start_node;
	    }
	}

	bool add_entry(const T &t, bool can_extend=true) {
	    if (_resident_count >= (m_initial_size)) {
			if(can_extend) {
				_extend();
			} else {
				return false;
			}
	    }

	    _store->_store[_resident_count] = t;
	    _resident_count++;
	    _total_count++;
		return true;
	}

	void push_back(const T &t, bool can_extend=true) {
		add_entry(t,can_extend);
	}

	T *get_last_addr(void) {
	    return &(_store->_store[_resident_count - 1]);
	}

#if 0
	T *get_addr(void) {
	    return &(_cur_scan_node->_store[_current_pointer % m_initial_size]);
	}

	T get_current(void) {
	    return _cur_scan_node->_store[_current_pointer % m_initial_size];
	}

	void next(void)	{
	    _current_pointer++;
	    if(_current_pointer % m_initial_size == 0) {
	        _cur_scan_node = _cur_scan_node->next;
	    }
	}
#endif

	// Clear the table.
	void reset(void) {
	    _resident_count = 0;
	    _total_count = 0;
	    _store = start_node;
	}

	void clear(void) {
		reset();
	}

	unsigned size(void) {
	    return _total_count;
	}

	void sort(void) {
		if(_total_count < 2) return; // must be sorted already

		store_node *temp_node;

		// Compute the number of store_nodes that need to be sorted.
		unsigned num_nodes = ((_total_count - 1) / m_initial_size) + 1;

		store_node ** indices = new store_node *[num_nodes];

		unsigned i;

		temp_node = start_node;

		// Copy the store_node pointers into this array for fast access.
		for(i=0;i<num_nodes;++i) {
			indices[i] = temp_node;
			temp_node  = temp_node->next;
		}

		quicksort(indices,0,_total_count-1);

		delete indices;
	}

protected:
	void _extend(void) {
	    if(_store->next) {
	        _store = _store->next;
	        _resident_count = 0;
	    } else {
	        _store->next = (store_node*)malloc(sizeof(store_node));

	        if (_store->next==NULL) {
	            printf ("Error: malloc failed while creating slot offset list.\n");
	            assert(0);
	            orp_exit(17063);
	        }

	        _store = _store->next;
	        _resident_count = 0;

	        _store->_store = (T *)malloc(m_initial_size * sizeof(T));
	        _store->next = NULL;

	        if (_store->_store==NULL) {
	            printf ("Error: malloc failed while creating slot offset list.\n");
				if(_store->_store == NULL) {
		            assert(0);
				}
	            orp_exit(17064);
	        }

	        _size_in_entries += m_initial_size;
	    }
	}

	unsigned m_initial_size;

    unsigned _resident_count;
    unsigned _total_count;

    unsigned _size_in_entries;

    typedef struct store_node {
        T *_store;
        struct store_node *next;
    } store_node;

    store_node *_store;
    store_node *start_node;


	inline T * indices_get(store_node *indices[], unsigned index) {
		unsigned node		= index / m_initial_size;
		unsigned node_index = index % m_initial_size;

		return &(indices[node]->_store[node_index]);
	}

	unsigned partition(store_node *indices[], unsigned start, unsigned end) {
		T *pivot = indices_get(indices, start);
		unsigned i = start - 1;
		unsigned j = end   + 1;

		while(1) {
			do {
				j--;
			} while (*pivot < *indices_get(indices, j));
			do {
				i++;
			} while  (*indices_get(indices, i) < *pivot);
			if(i < j) {
				T temp = *indices_get(indices, i);
				*indices_get(indices, i) = *indices_get(indices, j);
				*indices_get(indices, j) = temp;
			} else {
				return j;
			}
		}
	}

	void quicksort(store_node *indices[], unsigned start, unsigned end) {
		if(start < end) {
			unsigned q = partition(indices, start, end);
			quicksort(indices, start, q);
			quicksort(indices, q+1, end);
		}
	}

public:

	class iterator;
	friend class iterator;

	class iterator {
	protected:
		unsigned _current_pointer;
		store_node *_cur_scan_node;
		unsigned node_size;

	public:
		bool operator!=(const iterator &rhs) {
			return _current_pointer != rhs._current_pointer;
		}

		iterator& operator++(void) {
			_current_pointer++;
			if(_current_pointer % node_size == 0) {
				_cur_scan_node = _cur_scan_node->next;
			}
			return *this;
		}

		T *get_addr(void) {
			return &(_cur_scan_node->_store[_current_pointer % node_size]);
		}

		T get_current(void) {
			return _cur_scan_node->_store[_current_pointer % node_size];
		}

		friend class ExpandInPlaceArray<T>;
	};

	iterator begin(void) {
		iterator ret;
		ret._current_pointer = 0;
		ret._cur_scan_node   = start_node;
		ret.node_size        = m_initial_size;
		return ret;
	}

	iterator end(void) {
		iterator ret;
		ret._current_pointer = _total_count;
		ret._cur_scan_node   = NULL;
		ret.node_size        = m_initial_size;
		return ret;
	}
};

//==================================================================================
class slot_offset_entry {
public:
    void **slot;
    Partial_Reveal_Object *base;
    POINTER_SIZE_INT offset;

    slot_offset_entry(void **s,Partial_Reveal_Object *b,POINTER_SIZE_INT o) :
    	slot(s),
    	base(b),
    	offset(o) {}
};

#endif // _SLOT_OFFSET_LIST_H_
