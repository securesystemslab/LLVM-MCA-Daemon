#!/usr/bin/env python3

import random
import sys
from textwrap import dedent

it=10
assert it < 2**64
n_randbits=2048 #4096
assert n_randbits % 2 == 0  # Required assumption for efficient divisions/shifts
do_random=sys.argv[1]=="1"
random.seed(99837212)
if do_random:
    randbits = random.getrandbits(n_randbits)
else:
    randbits = 0

print(f"""
.globl	main
	.p2align	4, 0x90
main:
    xorq    %r8, %r8
    xorq    %r9, %r9
	xorq	%rax, %rax  # outer loop induction variable

loop_head:
	cmpq	${it}, %rax
	jge     loop_end

    # The following 8 instructions load the next random bit from the randomness
    # byte array below, by first loading the correct byte offset 
    # (loop_iteration/8) from randomness, then shifting (loop_iteration%8) to
    # the right to get the right bit out of the byte.
    movq    %rax, %rcx   # %rcx will be used to hold byte offset into array
    andq    ${n_randbits-1}, %rcx   # %rcx mod size of randomness array
    shrq    $3, %rcx     # %rcx / 8 to get byte offset
    mov     randomness(%rcx), %dl  # load randomness into %dl
    movq    %rax, %rcx   # %rcx will be used to hold bit offset into byte now
    andq    $7, %rcx     # loop index modulo 7 into rcs (bit offset into byte)
    shr     %rcx, %dl    # shift randomness right by %rcs bits
    and     $1, %dl      # take lowest bit of randomness

    # Actual comparison and branch; this is where we'll get our mispredicts
    cmpb    $0, %dl
    je      branch_true
branch_false:
    addq    $1, %r8   # keep track of not-taken branches in %r8
    jmp     loop_foot
branch_true:
    addq    $1, %r9  # keep track of taken branches in %r9

loop_foot:
	addq    $1, %rax
	jmp	    loop_head
loop_end:
	xorl	%eax, %eax
	retq

.data
randomness:
    .align 8
    .byte {", ".join("0x{0:x}".format((randbits>>(8*i))&0xFF) for i in range((n_randbits+7)//8))}

""")