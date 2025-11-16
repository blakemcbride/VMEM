/*
 * Test program to verify dump/restore functionality
 * Tests both regular restore and fast restore
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vmem.h"

int main()
{
    VMPTR_TYPE h1, h2, h3;
    char *ptr;
    char test_data1[] = "First block data - 12345";
    char test_data2[] = "Second block data - ABCDEF";
    char test_data3[] = "Third block data - xyz789";
    char buffer[100];
    int ret;

    printf("VMEM Dump/Restore Test\n");
    printf("======================\n\n");

    // Phase 1: Create and dump
    printf("Phase 1: Creating initial VM state\n");
    if (VM_init() != 0) {
        printf("Failed to initialize VMEM\n");
        return 1;
    }

    h1 = VM_alloc(100, 1);
    h2 = VM_alloc(200, 1);
    h3 = VM_alloc(300, 1);

    if (!h1 || !h2 || !h3) {
        printf("Failed to allocate memory\n");
        return 1;
    }
    printf("  Allocated 3 blocks: %d, %d, %d\n", h1, h2, h3);

    ptr = (char*)VM_addr(h1, 1, 0);
    strcpy(ptr, test_data1);

    ptr = (char*)VM_addr(h2, 1, 0);
    strcpy(ptr, test_data2);

    ptr = (char*)VM_addr(h3, 1, 0);
    strcpy(ptr, test_data3);

    printf("  Written test data to all blocks\n");

    ret = VM_dump("test.dump");
    printf("  VM_dump returned: %d\n", ret);

    if (ret != 0) {
        printf("  FAILED to dump\n");
        return 1;
    }

    // Phase 2: Test regular restore (VM_rest)
    printf("\nPhase 2: Testing VM_rest\n");
    VM_end();

    ret = VM_rest("test.dump");
    printf("  VM_rest returned: %d\n", ret);

    if (ret != 0) {
        printf("  FAILED to restore\n");
        return 1;
    }

    ptr = (char*)VM_addr(h1, 0, 0);
    strcpy(buffer, ptr);
    if (strcmp(buffer, test_data1) == 0) {
        printf("  Block 1: OK - '%s'\n", buffer);
    } else {
        printf("  Block 1: FAILED - got '%s'\n", buffer);
    }

    ptr = (char*)VM_addr(h2, 0, 0);
    strcpy(buffer, ptr);
    if (strcmp(buffer, test_data2) == 0) {
        printf("  Block 2: OK - '%s'\n", buffer);
    } else {
        printf("  Block 2: FAILED - got '%s'\n", buffer);
    }

    ptr = (char*)VM_addr(h3, 0, 0);
    strcpy(buffer, ptr);
    if (strcmp(buffer, test_data3) == 0) {
        printf("  Block 3: OK - '%s'\n", buffer);
    } else {
        printf("  Block 3: FAILED - got '%s'\n", buffer);
    }

    // Phase 3: Test fast restore (VM_frest)
    printf("\nPhase 3: Testing VM_frest\n");
    VM_end();

    ret = VM_frest("test.dump");
    printf("  VM_frest returned: %d\n", ret);

    if (ret != 0) {
        printf("  FAILED to fast restore\n");
        return 1;
    }

    // After fast restore, data is on disk - accessing will page it in
    ptr = (char*)VM_addr(h1, 0, 0);
    strcpy(buffer, ptr);
    if (strcmp(buffer, test_data1) == 0) {
        printf("  Block 1: OK - '%s'\n", buffer);
    } else {
        printf("  Block 1: FAILED - got '%s'\n", buffer);
    }

    ptr = (char*)VM_addr(h2, 0, 0);
    strcpy(buffer, ptr);
    if (strcmp(buffer, test_data2) == 0) {
        printf("  Block 2: OK - '%s'\n", buffer);
    } else {
        printf("  Block 2: FAILED - got '%s'\n", buffer);
    }

    ptr = (char*)VM_addr(h3, 0, 0);
    strcpy(buffer, ptr);
    if (strcmp(buffer, test_data3) == 0) {
        printf("  Block 3: OK - '%s'\n", buffer);
    } else {
        printf("  Block 3: FAILED - got '%s'\n", buffer);
    }

    // Test that we can still allocate and use more memory
    printf("\nPhase 4: Testing continued operation after fast restore\n");
    VMPTR_TYPE h4 = VM_alloc(400, 1);
    if (h4) {
        ptr = (char*)VM_addr(h4, 1, 0);
        strcpy(ptr, "New block after fast restore");
        ptr = (char*)VM_addr(h4, 0, 0);
        printf("  New allocation works: '%s'\n", ptr);
    } else {
        printf("  Failed to allocate new block\n");
    }

    VM_end();
    printf("\nAll tests completed!\n");
    return 0;
}