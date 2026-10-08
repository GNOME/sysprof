Powersort implementation
========================

This directory contains code modified for GTK, based on the timsort
implementation in python, and its Java adaptation by Joshua Bloch.

The merge policy is Powersort, as used by CPython since Python 3.11:

- J. Ian Munro and Sebastian Wild, [Nearly-Optimal Mergesorts: Fast, Practical Sorting Methods That Optimally Adapt to Existing Runs](https://doi.org/10.4230/LIPIcs.ESA.2018.63)
- [CPython's sorting implementation notes](https://github.com/python/cpython/blob/main/Objects/listsort.txt)

The private `gtk_tim_sort` API and filenames are kept for compatibility.

See the source files for copyright and licensing information, and the
`COPYING` file for the full text of the Apache license, version 2.0.
