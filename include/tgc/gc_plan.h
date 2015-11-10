/*
 * COPYRIGHT_NOTICE_1
 */

//
// A "plan" sets various parameters in the moving collector,
// such as the number of steps, the sizes of nurseries, etc.
// There is a default plan that is hardwired into the source
// code (this file.) In addition, if the user places a ".plan"
// file in the same directory as the ORP executable, that file
// is read, and the values replace the defaults.
//


#ifndef _gc_plan_h_
#define _gc_plan_h_

//
// Define how many bits we shift an address to get its card index. So for
// a 256 byte card we shift by 8 bits. This number should be between 
// 8 and 11 to correspond with the 256 to 1024 byte card size.
//
// This needs to be a compile time constant for speed.
//
 

class Gc_Plan {
public:
    Gc_Plan(char *p_plan_file_name) {
		_set_defaults();
	}

	Gc_Plan() {
		_set_defaults();
	}


    virtual ~Gc_Plan() {

    }

	/*unsigned long*/ POINTER_SIZE_INT default_initial_heap_size_bytes()
	{
		return plan_initial_heap_size_bytes;
	}

	/*unsigned long*/ POINTER_SIZE_INT default_final_heap_size_bytes()
	{
		return plan_final_heap_size_bytes;
	}

	unsigned long sub_block_size_bytes()
	{
		return plan_sub_block_size_bytes;
	}

private:

 
	POINTER_SIZE_INT plan_initial_heap_size_bytes;

	POINTER_SIZE_INT plan_final_heap_size_bytes;

	unsigned int plan_sub_block_size_bytes;

	void _set_defaults();
};

#endif // _gc_plan_h_
