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
