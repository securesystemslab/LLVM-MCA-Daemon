#!/usr/bin/env python3

# blackforest (i9-9900K) stats:
#  L1d:                   256 KiB (8 instances)  -> 32 KB per core  -- 8-way
#  L1i:                   256 KiB (8 instances)  -> 32 KB per core  -- 8-way
#  L2:                    2 MiB (8 instances)                       -- 4-way 
#  L3:                    16 MiB (1 instance)                       -- 16-way

# source: https://en.wikichip.org/wiki/intel/microarchitectures/skylake_(client)
#
# perf stat -e "cache-misses,L1-dcache-load-miss,l2_rqsts.miss,offcore_requests.all_requests"
# Try:
# - active_set_size > 600, stride == 1: 
#   this is a larger active set than L1 cache set, should see ~100,000 L1 cache misses
# - active_set_size > 64 (e.g. 74), stride == 8: 
#   since L1 cache is 8-way, using a stride == 8 means each access will map to a
#   separate set, i.e. we are only utilizing 1 way in each set, effectively
#   dividing L1 cache size by 8; should se ~100,000 L1 cache misses

import sys

n_to_read = 1<<30-1 # 1 GB

it=10_000

cache_line_size =    64  # in bytes
active_set_size =   128  # number of cache lines that we use; once this increases past cache size, we should see a spike in cache misses

# stride in units of `cache_line_size`; how many cache lines to stride between accesses; 1 means contiguous cache lines are accessed
# contiguous accesses mean that the tag bits of the address are likely different between accesses;
# meaning that there will be little contention / few conflict misses
# making the stride large enough that only the bits outside of the tag change means that back-to-back accesses map to the
# same cache set, leading to conflict misses once the active set size exceeds the number of ways of the cache
stride = 24

print(f"""
.globl	_start
	.p2align	4, 0x90
_start:
    xorq    %r8, %r8
    xorq    %r9, %r9
	xor 	%ebx, %ebx  # outer loop induction variable
    xor     %edx, %edx  # upper half of divison; we don't use it

loop_head:
	cmp 	${it}, %ebx
	jge     loop_end

    mov     %ebx, %eax  # div operates on eax
    mov     ${active_set_size}, %ecx
    div     %ecx  # div puts the remainder in edx

    mov     %edx, %eax  # mul operates on eax
    mov     ${stride*cache_line_size}, %ecx
    mul     %ecx

    # this is the read we are interested in benchmarking
    mov     to_read(%eax), %edx

    # write back to memory; I believe this stops the prefetcher
    # without this line, we do not get L1 cache hits as expected when the
    # active set size is larger than L1 cache
    mov     %edx, to_read(%eax)

loop_foot:
	add     $1, %ebx
	jmp	    loop_head
loop_end:
	xor 	%eax, %eax

    // the following performs the exit system call with a 0 exit code
    movl    $60, %eax
    xorl    %ebx, %ebx
    syscall

.lcomm to_read, {n_to_read}  // 1 GB

""")