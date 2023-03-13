# Dynamic Memory Management Systems

For safety-critical systems dynamic memory management is not an option.
This is not because of the challenges in *using* dynamic memory management
correctly, but because dynamic memory management as such is hard or even
impossible to analyse. There is no method to predict whether there is always
enough memory available in view of fragmentation for a specific application
to work properly. In practice that is rarely a real concern. Systems like
the first fit method and the buddy system do not run out of memory
before at least 95% of the available resources are allocated.
For safety-critical systems, however, this is not enough. A lower limit
needs to be established by analysis and *exhaustive* testing.

Most embedded real-time systems, however, are not safety-critical
in the proper sense. Furthermore, many applications need a dynamic heap
and many systems today even use high-level languages that rely on
this feature.

This library provides three different dynamic memory managers:
  * one based on the first fit method (ffit)
  * one based on the buddy system (buddy)
  * one mainly based on the buddy system using ffit as a
    secondary "emergency" heap (ebuddy).

The implementation follows Knuth, The Art of Computer Programming,
Vol. 1, Sec. 2.5.

The managers are implemented in two files (and their header files):
  * ffit.c
  * buddy.c

The second implements both: the pure buddy system and
the buddy system plus emergency heap. Whether an emergency heap
is used or not is controlled by an initialisation flag
(for details, please refer directly to buddy.h).

There are also three files implementing tests and experiments:
  * Hello-world-style smoke tests (ffitsmoke, buddysmoke and ebuddysmoke)
  * Basic testcases (testffit1, testbuddy and testebuddy)
  * A monte carlo simulation inspired by Knuth
    (monteffit, montebuddy, monteebuddy).

All tests are derived from the two input files with different
compilation settings (please refer directly to the Makefile for details).

The buddy and ffit libraries provide three main services
(besides initialisation and debugging), namely:
  * a _get block_ service to be used with malloc and calloc
  * a _free block_ service to be used with free
  * an _expand block_ service to be use with realloc.

Internally, these services shall use locks to protect the
internal structures. Note that the code indicates where
locking should take place but does not yet actually implement
locking. The concrete mechanism to be used for locking
depends on the libc in which the managers
shall be integrated (e.g. musl).

The buddy and ffit components do not use global variables.
Instead explicit descriptors must be passed to the library services.
This feature makes it possible to use more than one heap per process.
The obvious use case is to separate the heap according to processing
cores to reduce lock contention. The implementation, say malloc.c,
would select the respective heap according to the core on which
the invoking thread or process is running.

Concerning the  origin and history of the library,
the buddy system was implemented some years ago as an exercise
and many experiments were performed with it, but it never used
in a real project. In 2020, I reviewed and improved the code
and added the ffit implementation. This code was exercised only
in basic tests and it was used only in the monte carlo simulation.
In other words:
The code still needs more and, in particular, systematic testing
and it is advisable to design a non-trivial use case to observe
its behaviour in praxis.

I declare that all code in this library was written by me
without the use of any other code and, in particular, without
reusing copyrighted material.

I donate this code to the Public Domain.

Tobias Schoofs
