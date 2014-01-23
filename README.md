
VMEM - Virtual Memory for C programs
===============================

Vmem is a C library that allows your C program to allocate and use more memory than is physically available.  Memory blocks are accessed through handles.  When there is not enough memory, the system will automatically page out the least recently used blocks to a disk file.  They are automatically reloaded when accessed again.

The system also has some nice dump and restore facilities.


Please see the accompanying readme file for more details.
