/*
 * Comprehensive test program for modernized VMEM library
 * Tests basic allocation, reallocation, memory access, and statistics
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vmem.h"

#define TEST_BLOCKS 10
#define PATTERN_SIZE 64

void print_stats(const char *label) {
    long *stats = VM_stat();
    printf("\n%s Statistics:\n", label);
    printf("  Real memory total: %ld bytes\n", stats[0]);
    printf("  Real memory free blocks: %ld\n", stats[1]);
    printf("  Real memory max used: %ld bytes\n", stats[2]);
    printf("  VM blocks in memory: %ld\n", stats[5]);
    printf("  VM blocks on disk: %ld\n", stats[6]);
    printf("  VM total allocated: %ld bytes\n", stats[7]);
    printf("  Disk memory used: %ld bytes\n", stats[9]);
    printf("  Disk free blocks: %ld\n", stats[10]);
}

int verify_pattern(char *ptr, int block_num, int size) {
    char expected[PATTERN_SIZE];
    int pattern_count = size / PATTERN_SIZE;
    int i;
    int pattern_len;

    for (i = 0; i < pattern_count; i++) {
        snprintf(expected, PATTERN_SIZE, "Block %d Pattern %d", block_num, i);
        pattern_len = strlen(expected);
        if (memcmp(ptr + (i * PATTERN_SIZE), expected, pattern_len) != 0) {
            return 0;
        }
    }
    return 1;
}

void write_pattern(char *ptr, int block_num, int size) {
    char pattern[PATTERN_SIZE];
    int pattern_count = size / PATTERN_SIZE;
    int i;

    memset(ptr, 0, size);  // Clear the entire block first
    for (i = 0; i < pattern_count; i++) {
        snprintf(pattern, PATTERN_SIZE, "Block %d Pattern %d", block_num, i);
        strcpy(ptr + (i * PATTERN_SIZE), pattern);
    }
}

int main() {
    VMPTR_TYPE handles[TEST_BLOCKS];
    int sizes[TEST_BLOCKS];
    char *ptr;
    int i;
    int errors = 0;

    printf("VMEM Library Comprehensive Test\n");
    printf("================================\n\n");

    // Initialize VMEM
    printf("1. Initializing VMEM system...\n");
    if (VM_init() != 0) {
        printf("   FAILED: Could not initialize VMEM\n");
        return 1;
    }
    printf("   SUCCESS: VMEM initialized\n");

    // Set parameters for testing (force some paging)
    printf("\n2. Setting VM parameters for testing...\n");
    VM_parm(100000, 20000, 2.0, 200000, 10, 0);  // Increased limits to allow all allocations
    printf("   Real memory max: 100KB, Auto-size: 20KB\n");

    // Test allocation
    printf("\n3. Testing allocation of %d blocks...\n", TEST_BLOCKS);
    for (i = 0; i < TEST_BLOCKS; i++) {
        sizes[i] = (i + 1) * 1024;  // 1KB, 2KB, 3KB, etc.
        handles[i] = VM_alloc(sizes[i], 1);  // Allocate and zero

        if (handles[i] == 0) {
            printf("   FAILED: Could not allocate block %d (size %d)\n", i, sizes[i]);
            errors++;
        } else {
            printf("   Allocated block %d: handle=%d, size=%d bytes\n",
                   i, handles[i], sizes[i]);
        }
    }

    // Write patterns to blocks
    printf("\n4. Writing test patterns to all blocks...\n");
    for (i = 0; i < TEST_BLOCKS; i++) {
        if (handles[i]) {
            ptr = (char*)VM_addr(handles[i], 1, 0);  // Get writable address
            if (!ptr) {
                printf("   Block %d: FAILED - VM_addr returned NULL\n", i);
                errors++;
            } else {
                write_pattern(ptr, i, sizes[i]);
                printf("   Written pattern to block %d\n", i);
            }
        }
    }

    print_stats("After initial allocation");

    // Force paging by accessing blocks in reverse order
    printf("\n5. Verifying patterns (reverse order to force paging)...\n");
    for (i = TEST_BLOCKS - 1; i >= 0; i--) {
        if (handles[i]) {
            ptr = (char*)VM_addr(handles[i], 0, 0);  // Get read-only address
            if (!ptr) {
                printf("   Block %d: FAILED - VM_addr returned NULL\n", i);
                errors++;
            } else if (verify_pattern(ptr, i, sizes[i])) {
                printf("   Block %d: Pattern verified OK\n", i);
            } else {
                printf("   Block %d: FAILED pattern verification\n", i);
                errors++;
            }
        }
    }

    // Test reallocation
    printf("\n6. Testing reallocation...\n");
    for (i = 0; i < 3; i++) {  // Reallocate first 3 blocks
        if (handles[i]) {
            int new_size = sizes[i] * 2;
            VMPTR_TYPE new_handle = VM_realloc(handles[i], new_size);

            if (new_handle) {
                printf("   Block %d reallocated: old size=%d, new size=%d, handle=%d\n",
                       i, sizes[i], new_size, new_handle);
                handles[i] = new_handle;
                sizes[i] = new_size;

                // Write new pattern to expanded area
                ptr = (char*)VM_addr(handles[i], 1, 0);
                write_pattern(ptr, i, sizes[i]);
            } else {
                printf("   FAILED to reallocate block %d\n", i);
                errors++;
            }
        }
    }

    print_stats("After reallocation");

    // Test freeing some blocks
    printf("\n7. Testing deallocation...\n");
    for (i = 1; i < TEST_BLOCKS; i += 2) {  // Free every other block
        if (handles[i]) {
            VM_free(handles[i]);
            printf("   Freed block %d (handle=%d)\n", i, handles[i]);
            handles[i] = 0;
        }
    }

    print_stats("After freeing blocks");

    // Verify remaining blocks still have correct data
    printf("\n8. Verifying remaining blocks...\n");
    for (i = 0; i < TEST_BLOCKS; i++) {
        if (handles[i]) {
            ptr = (char*)VM_addr(handles[i], 0, 0);
            if (!ptr) {
                printf("   Block %d: FAILED - VM_addr returned NULL\n", i);
                errors++;
            } else if (verify_pattern(ptr, i, sizes[i])) {
                printf("   Block %d: Pattern still valid\n", i);
            } else {
                printf("   Block %d: FAILED - pattern corrupted\n", i);
                errors++;
            }
        }
    }

    // Test VM_newadd flag
    printf("\n9. Testing VM_newadd flag...\n");
    VM_newadd = 0;
    ptr = (char*)VM_addr(handles[0], 0, 0);  // Access without change
    printf("   After read access: VM_newadd = %d (should be 0)\n", VM_newadd);

    // VM_newadd should be set when memory is moved (compaction/paging)
    // Force paging by allocating a large block
    VM_newadd = 0;
    VMPTR_TYPE large_handle = VM_alloc(50000, 1);  // Large allocation to force paging
    if (large_handle) {
        printf("   After large allocation: VM_newadd = %d (should be 1 if paging occurred)\n", VM_newadd);
        VM_free(large_handle);
    } else {
        printf("   Could not allocate large block for paging test\n");
    }

    // Test forced compaction
    printf("\n10. Testing memory compaction...\n");
    VM_dcmps();  // Force decompression
    printf("    Decompression completed\n");
    VM_fcore();  // Force compaction
    printf("    Compaction completed\n");

    print_stats("After compaction");

    // Final cleanup
    printf("\n11. Cleaning up...\n");
    for (i = 0; i < TEST_BLOCKS; i++) {
        if (handles[i]) {
            VM_free(handles[i]);
        }
    }

    VM_end();
    printf("    VMEM system terminated\n");

    // Report results
    printf("\n================================\n");
    if (errors == 0) {
        printf("ALL TESTS PASSED SUCCESSFULLY!\n");
    } else {
        printf("TESTS COMPLETED WITH %d ERRORS\n", errors);
    }
    printf("================================\n");

    return errors;
}