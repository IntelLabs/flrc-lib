/*
 * COPYRIGHT_NOTICE_1
 */

// System header files
#include <iostream>

// GC header files
#include "gc_cout.h"
#include "gc_header.h"
#include "gc_plan.h"
#include "gcv4_synch.h"

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

