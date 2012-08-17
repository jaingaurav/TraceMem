TraceMem
========

TraceMem is a compiler pass built on LLVM that instruments program loads and stores in a rotating log.

Once you've downloaded the project you need to do the following in the project directory:

1. cd TraceMem/llvm/tools
2. ln -s ../../clang .
3. cd ../projects
4. ln -s ../../compiler-rt .
5. cd ../../..
6. mkdir TraceMem-build
7. cd TraceMem-build
8. ../TraceMem/llvm/configure
9. make
10. export PATH=$PWD/Release+Asserts/bin:$PATH

Now we need to build the TraceMem library

1. cd ..
2. mkdir libtracemem-build
3. cd libtracemem-build
4. cmake ../TraceMem/libtracemem/
5. make

At this point you have everything to compile programs with TraceMem instrumentation. The samples directory contains some example programs that can be compiled as follows:

clang++ series.cpp -S -emit-llvm -o series.ll

opt -mem2reg -load ../../TraceMem-build/Release+Asserts/lib/LLVMTraceMem.dylib -stats -TraceMem series.ll -o series.bc

clang++ series.bc -L../../libtracemem-build/ -lTraceMem -o series

Upon program terminatation the memory traces are written to a file.
