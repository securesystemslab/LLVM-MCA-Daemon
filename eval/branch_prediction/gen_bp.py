#!/usr/bin/env python3

import random
import sys
from textwrap import dedent

it=10_000
inlined_iters_p2 = 3  # 2**3 = 8
assert it % (2**inlined_iters_p2) == 0
assert it < 2**64
# If we have less randomness below than total iterations, the branch predictor
# will learn form the global history: The first branch in the iteration of 8
# can then be used to predict the behavior of the next 7, because they will all
# use the same randomness as on an earlier iteration. It might be possible to
# estimate the size of the global history by adjusting the amount of randomness.
n_randbits_p2=15  # 2**12 = 4096

n_random=int(sys.argv[1])  # number out of 8 that will be made random
assert 0 <= n_random <= 8

random.seed(99837212)
randbits = random.getrandbits(2**n_randbits_p2)
for i in range(2**n_randbits_p2//(2**inlined_iters_p2)):
    for j in range(2**inlined_iters_p2-n_random):
        randbits &= ~(1<<(i*2**inlined_iters_p2+j))

print(f"""
.globl	_start
	.p2align	4, 0x90
_start:
    xorq    %r8, %r8
    xorq    %r9, %r9
	xorq	%rax, %rax  # outer loop induction variable

loop_head:
	cmpq	${it//inlined_iters_p2}, %rax
	jge     loop_end
    """);

for i in range(2**inlined_iters_p2):
    print(f"""
        # The following 8 instructions load the next random bit from the randomness
        # byte array below, by first loading the correct byte offset 
        # (loop_iteration/8) from randomness, then shifting (loop_iteration%8) to
        # the right to get the right bit out of the byte.
        movq    %rax, %rcx   # %rcx will be used to hold byte offset into array
        andq    ${2**n_randbits_p2//2**inlined_iters_p2-1}, %rcx   # %rcx mod size of the slice of randomness array for this inlined iter (in bits)
        addq    ${(2**n_randbits_p2//2**inlined_iters_p2)*i}, %rcx
        shrq    $3, %rcx     # %rcx / 8 to get byte offset
        mov     randomness(%rcx), %dl  # load randomness into %dl
        mov     %rax, %rcx
        and     $7, %rcx
        shr     %rcx, %dl    
        and     $1, %dl      # take lowest bit of randomness

        # Actual comparison and branch; this is where we'll get our mispredicts
        cmpb    $0, %dl
        je      branch_{i}_true
    branch_{i}_false:
        addq    $1, %r8   # keep track of not-taken branches in %r8
    branch_{i}_true:
    """)

print(f"""
loop_foot:
	addq    $1, %rax
	jmp	    loop_head
loop_end:
	xorl	%eax, %eax

    // the following performs the exit system call with a 0 exit code
    movl    $60, %eax
    xorl    %ebx, %ebx
    syscall

.data
randomness:
    .align 8
    .byte {", ".join("0x{0:x}".format((randbits>>(8*i))&0xFF) for i in range(2**n_randbits_p2//8))}

""")