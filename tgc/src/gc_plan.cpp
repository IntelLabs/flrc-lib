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

// GC header files
#include "tgc/gc_cout.h"
#include "tgc/gc_header.h"
#include "tgc/gc_plan.h"
#include "tgc/gcv4_synch.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef LINUX
#include <unistd.h>
#endif // LINUX

int get_page_size ()
{
#ifdef _WINDOWS
    SYSTEM_INFO sSysInfo;         // useful information about the system
    GetSystemInfo(&sSysInfo);     // populate the system information structure
    return (int)sSysInfo.dwPageSize;
#elif defined LINUX
    return getpagesize();
#else
    assert(0);
    exit(-1);
#endif
}


void
Gc_Plan::_set_defaults()
{
     // Set some defaults.
     // Initial Heap size set to 128M
     plan_initial_heap_size_bytes = 0x8000000;

	// Final heap size set to be same as initial heap size
     plan_final_heap_size_bytes = plan_initial_heap_size_bytes;

	 // 64K block ??
	 plan_sub_block_size_bytes = GC_BLOCK_SIZE_BYTES;
}
