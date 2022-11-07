> :warning: **DISCONTINUATION OF PROJECT** - 
> *This project will no longer be maintained by Intel.
> Intel has ceased development and contributions including, but not limited to, maintenance, bug fixes, new releases, or updates, to this project.*
> **Intel no longer accepts patches to this project.**
> *If you have an ongoing need to use this project, are interested in independently developing it, or would like to maintain patches for the open source software community, please create your own fork of this project.*


# Libraries for the Functional Language Research Compiler [![Build Status](https://travis-ci.org/IntelLabs/flrc-lib.svg)](https://travis-ci.org/IntelLabs/flrc-lib)

This repository contains the following libraries and tools that are 
part of the overall [FLRC] tool-chain. Strictly speaking, they 
are a pre-requisite to using FLRC, but not for compiling FLRC. But
due to installation path dependencies, `flrc-lib` should be installed
before `flrc`.

1. A translator from [Pillar] to C++ (`pilla2c`).

2. Pillar Runtime (`prt`). 

3. Pillar Toolkit (`ptkfuture`).

4. Garbage collector (`tgc`), and its pillar interface (`pgc`).

5. Bash scripts (`pilicl` and `pilink`) to help compile and link Pillar programs.

## Installation

All released code has only been tested to work on x86 64-bit 
Linux distros, although they were originally written for x86 32-bit 
Windows.

The installation uses the typical autoconf/automake facilities on
Linux. It also requires a C/C++ compiler (gcc or icc), and an 
assembler (nasm) to compile.

To install:

```
sh bootstrap.sh
./configure --prefix=${PREFIX}
make && make install
```

If successful, all related libraries and scripts will be installed
under the given path `${PREFIX}`. 

## Usage and FAQ

These libraries and tools are mostly intended to be used by [FLRC].
Please use the issue track if you have questions.

## Related Publication

Todd Anderson, Neal Glew, Peng Guo, Brian T. Lewis, Wei Liu, Zhanglin Liu, Leaf Petersen, Mohan Rajagopalan, James M. Stichnoth, Gansha Wu, and Dan Zhang. 2007. [Pillar: A Parallel Implementation Language][Pillar]. In Languages and Compilers for Parallel Computing, Vikram Adve, María Jesús Garzarán, and Paul Petersen (Eds.). Lecture Notes In Computer Science, Vol. 5234. Springer-Verlag, Berlin, Heidelberg 141-155. [(PDF)](doc/pillar-lcpc.pdf).

Todd A. Anderson.  2010. [Optimizations in a Private Nursery-based Garbage Collector][TGC]. In International Symposium on Memory Management 2010.  [(PDF)](doc/optimizations-private-nursery.pdf).

## License

This software carries a BSD style license. See [LICENSE_INFO](LICENSE_INFO.txt) for more information.


[Pillar]: http://dl.acm.org/citation.cfm?id=1433050.1433063
[FLRC]: https://github.com/IntelLabs/flrc
[TGC]: http://dl.acm.org/citation.cfm?id=1806655

