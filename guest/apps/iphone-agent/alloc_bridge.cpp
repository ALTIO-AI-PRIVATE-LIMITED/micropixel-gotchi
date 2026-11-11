// SPDX-License-Identifier: Apache-2.0
//
// Lets the C code (libc_shim.c) allocate from the Guest runtime's heap.
#include <stddef.h>

#include <new>

extern "C" void* agent_allocate(size_t size) { return ::operator new(size, std::nothrow); }

extern "C" void agent_release(void* pointer) { ::operator delete(pointer); }
