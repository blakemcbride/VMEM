# Makefile for VMEM Virtual Memory Library
# For 64-bit Linux systems

CC = gcc
CFLAGS = -Wall -O2 -g
LDFLAGS =
AR = ar
ARFLAGS = rcs

# Library name
LIB = libvmem.a

# Object files
OBJS = vmem.o

# Test programs
TESTS = test1 test2 example test_dump

# Default target
all: $(LIB) $(TESTS)

# Build the static library
$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^
	@echo "Library $(LIB) created successfully"

# Compile vmem.c
vmem.o: vmem.c vmem.h
	$(CC) $(CFLAGS) -c vmem.c -o vmem.o

# Build test programs
test1: test1.c $(LIB)
	$(CC) $(CFLAGS) test1.c -L. -lvmem -o test1

test2: test2.c $(LIB)
	$(CC) $(CFLAGS) test2.c -L. -lvmem -o test2

example: example.c $(LIB)
	$(CC) $(CFLAGS) example.c -L. -lvmem -o example

test_dump: test_dump.c $(LIB)
	$(CC) $(CFLAGS) test_dump.c -L. -lvmem -o test_dump

# Clean build artifacts
clean:
	rm -f *.o $(LIB) $(TESTS) vmem*.tmp vm*.dump core

# Install library and header (optional)
install: $(LIB)
	mkdir -p /usr/local/lib /usr/local/include
	cp $(LIB) /usr/local/lib/
	cp vmem.h /usr/local/include/
	@echo "Installed $(LIB) to /usr/local/lib and vmem.h to /usr/local/include"

# Run tests
test: test1 test2 test_dump
	@echo "Running test1 (comprehensive test)..."
	./test1
	@echo ""
	@echo "Running test2 (stress test)..."
	./test2
	@echo ""
	@echo "Running test_dump (dump/restore test)..."
	./test_dump
	@echo ""
	@echo "All tests completed"

.PHONY: all clean install test