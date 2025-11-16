/*
 * Simple example demonstrating VMEM virtual memory library
 * Shows handle-based memory allocation and dump/restore features
 */

#include <stdio.h>
#include <string.h>
#include "vmem.h"

int main()
{
    VMPTR_TYPE handle1, handle2, handle3;
    char *ptr;
    int i;

    printf("VMEM Example Program\n");
    printf("====================\n\n");

    // Initialize the virtual memory system
    if (VM_init() != 0) {
        printf("Failed to initialize VMEM\n");
        return 1;
    }

    // Set reasonable parameters: 10MB max real memory, 1MB chunks
    VM_parm(10000000L, 1000000L, -1.0, -1L, -1, -1);

    printf("1. Allocating virtual memory blocks...\n");

    // Allocate first block - 1000 bytes
    handle1 = VM_alloc(1000, 1);  // 1 = zero the memory
    if (!handle1) {
        printf("Failed to allocate first block\n");
        return 1;
    }
    printf("   Allocated 1000 bytes with handle %d\n", handle1);

    // Allocate second block - 5000 bytes
    handle2 = VM_alloc(5000, 1);
    if (!handle2) {
        printf("Failed to allocate second block\n");
        return 1;
    }
    printf("   Allocated 5000 bytes with handle %d\n", handle2);

    // Allocate third block - 10000 bytes
    handle3 = VM_alloc(10000, 1);
    if (!handle3) {
        printf("Failed to allocate third block\n");
        return 1;
    }
    printf("   Allocated 10000 bytes with handle %d\n", handle3);

    printf("\n2. Writing data to virtual memory...\n");

    // Write to first block
    ptr = (char*)VM_addr(handle1, 1, 0);  // 1=dirty, 0=don't freeze
    strcpy(ptr, "Hello from VMEM block 1!");
    printf("   Wrote to block 1: %s\n", ptr);

    // Write to second block
    ptr = (char*)VM_addr(handle2, 1, 0);
    strcpy(ptr, "This is virtual memory block 2 with more data.");
    printf("   Wrote to block 2: %s\n", ptr);

    // Write pattern to third block
    ptr = (char*)VM_addr(handle3, 1, 0);
    for (i = 0; i < 100; i++) {
        ptr[i] = 'A' + (i % 26);
    }
    ptr[100] = '\0';
    printf("   Wrote pattern to block 3: %.50s...\n", ptr);

    printf("\n3. Dumping virtual memory to disk...\n");
    if (VM_dump("vmem.dump") == 0) {
        printf("   Successfully dumped VM to vmem.dump\n");
    } else {
        printf("   Failed to dump VM\n");
    }

    printf("\n4. Freeing all memory...\n");
    VM_end();
    printf("   All virtual memory freed\n");

    printf("\n5. Restoring from dump file...\n");
    if (VM_rest("vmem.dump") == 0) {
        printf("   Successfully restored VM from vmem.dump\n");
    } else {
        printf("   Failed to restore VM\n");
        return 1;
    }

    printf("\n6. Verifying restored data...\n");

    // Verify first block
    ptr = (char*)VM_addr(handle1, 0, 0);  // 0=not dirty, 0=don't freeze
    printf("   Block 1 contains: %s\n", ptr);

    // Verify second block
    ptr = (char*)VM_addr(handle2, 0, 0);
    printf("   Block 2 contains: %s\n", ptr);

    // Verify third block
    ptr = (char*)VM_addr(handle3, 0, 0);
    printf("   Block 3 contains: %.50s...\n", ptr);

    printf("\n7. Getting VM statistics...\n");
    long *stats = VM_stat();
    printf("   Total VM allocated: %ld bytes\n", stats[0]);
    printf("   Real memory used: %ld bytes\n", stats[1]);
    printf("   Disk memory used: %ld bytes\n", stats[2]);
    printf("   Objects in real memory: %ld\n", stats[3]);
    printf("   Objects on disk: %ld\n", stats[4]);

    printf("\n8. Cleaning up...\n");
    VM_end();
    printf("   All virtual memory freed\n");

    printf("\nExample completed successfully!\n");
    return 0;
}