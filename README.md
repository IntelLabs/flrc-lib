# Libraries for the Functional Language Research Compiler 

This repository contains the following libraries and tools that are 
part of the overall [FLRC](flrc) toolchain. Strictly speaking, they 
are a pre-requsite to using FLRC, but not for compiling FLRC. But
due to installation path dependencies, `flrc-lib` should be installed
before `flrc`.

1. A translator from [Pillar](pillar) to C++ (`pilla2c`).

2. Pillar Runtime (`prt`). 

3. Pillar Toolkit (`ptkfuture`).

4. Garbage collector (`tgc`), and its pillar interface (`pgc`).

5. Bash scripts (`pilicl` and `pilink`) to help compile and link Pillar programs.

## Installation

All released code has only been tested to work on x86 64-bit 
Linux distros, although they were originally written for x86 32-bit 
Windows.

The installation uses the typical autoconf/automake facilites on
Linux. It also requires a C/C++ compiler (gcc or icc), and an 
assember (nasm) to compile. The steps are:

To install:

```
sh bootstrap.sh
./configure --prefix=${PREFIX}
make && make install
```

If successful, all related libraries and scripts will be installed
under the given path `${PREFIX}`. 

## Usage and FAQ

These libraries and tools are mostly intended to be used by [FLRC](flrc).
Please use the issue track if you have questions.

## Related Publication

Todd Anderson, Neal Glew, Peng Guo, Brian T. Lewis, Wei Liu, Zhanglin Liu, Leaf Petersen, Mohan Rajagopalan, James M. Stichnoth, Gansha Wu, and Dan Zhang. 2007. [Pillar: A Parallel Implementation Language](pillar). In Languages and Compilers for Parallel Computing, Vikram Adve, María Jesús Garzarán, and Paul Petersen (Eds.). Lecture Notes In Computer Science, Vol. 5234. Springer-Verlag, Berlin, Heidelberg 141-155. [(PDF)](doc/pillar-lcpc.pdf).

## License

This software carries a BSD style license. See [LICENSE_INFO](LICENSE_INFO.txt) for more information.


[pillar]: http://dl.acm.org/citation.cfm?id=1433050.1433063
[flrc]: https://github.com/IntelLabs/flrc

