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

#ifndef _Object_List_H_
#define _Object_List_H_

#ifdef GC_CLI
// CLI retquires that we allow roots to be pinned. When/if support CLI we need to turn this on.
#define GC_ROOT_PINNING
#endif

//
// The default size of object lists. Keep modest for now.
//
const int DEFAULT_OBJECT_SIZE_IN_ENTRIES = (4096*16);

//
// An Object List is used to keep track of lists of objects when
// a hash table-style remembered set or object set is overkill.
//


class Object_List {
public:
    Object_List();

    virtual ~Object_List();

    unsigned add_entry(Partial_Reveal_Object *p_obj);

	bool exists(Partial_Reveal_Object *p_obj);

	Partial_Reveal_Object *next(void);

    void rewind();

    void reset();

    unsigned size();

    void debug_check_list_integrity();

    void debug_dump_list();

private:

    unsigned _current_pointer;

    void _extend();

    unsigned _resident_count;

    unsigned _size_in_entries;

    Partial_Reveal_Object **_store;
};


//
// An Object List is used to keep track of lists of objects when
// a hash table-style remembered set or object set is overkill.
//


class VTable_List {
public:
    VTable_List();

    virtual ~VTable_List();

    unsigned add_entry(Partial_Reveal_VTable *p_obj);

	bool exists(Partial_Reveal_VTable *p_obj);

	Partial_Reveal_VTable *next(void);

    void rewind();

    void reset();

    unsigned size();

    void debug_check_list_integrity();

    void debug_dump_list();

private:

    unsigned _current_pointer;

    void _extend();

    unsigned _resident_count;

    unsigned _size_in_entries;

    Partial_Reveal_VTable **_store;
};

class Root_List {
public:
    Root_List();

    virtual ~Root_List();

#ifdef GC_ROOT_PINNING
    unsigned add_entry(Partial_Reveal_Object **pp_obj, bool root_is_pinned);
	Partial_Reveal_Object **pp_next(bool *);
#else
    unsigned add_entry(Partial_Reveal_Object **pp_obj);
#endif // GC_ROOT_PINNING

	Partial_Reveal_Object **pp_next(void);

    void rewind();

    void reset();

    unsigned size();

    void debug_check_list_integrity();

    void debug_dump_list();

private:

    unsigned _current_pointer;

    void _extend();

    unsigned _resident_count;

    unsigned _size_in_entries;

    Partial_Reveal_Object ***_store;

#ifdef GC_ROOT_PINNING
	bool *_is_root_pinned;
#endif // GC_ROOT_PINNING
};


#endif // _Object_List_H_

