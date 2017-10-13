/*
 * Filename.: lzvn_decode.s
 * Author...: Pike R. Alpha
 */

.text

.globl lzvn_decode

#	rdi: void *dst
#	rsi: size_t dst_size
#	rdx: const void *src
#	rcx: size_t src_size

#	rax: return size_t

lzvn_decode:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	pushq	%r12
	leaq	Lzvn_decode.opcode_table(%rip), %rbx
#	leaq	0x2f2(%rip), %rbx
	xorq	%rax, %rax
	xorq	%r12, %r12
	# Has to be at least 8 bytes output (?)
	subq	$8, %rsi
	jb	L_return_0
	# Last 8 bytes are 06 00 00 00 00 00 00 00
	leaq	-8(%rdx,%rcx), %rcx  # src + src_size - 8
	cmpq	%rcx, %rdx
	ja	L_return_0
	# r9 = next byte
	movzbq	(%rdx), %r9
	# r8 = next 8 bytes
	movq	(%rdx), %r8
	# Execute operation
	jmpq	*(%rbx,%r9,8)

L_op_0E:
	addq	$1, %rdx # src++
	cmpq	%rcx, %rdx # src >= end?
	ja	L_return_0

	# Get next op
	movzbq	(%rdx), %r9
	movq	(%rdx), %r8
	jmpq	*(%rbx,%r9,8)

	# Alignment stuff
	nopw	%cs:(%rax,%rax)
	nop

L_op_00:
	shrq	$6, %r9 # byte >>= 6
	leaq	2(%rdx,%r9), %rdx # src += (2 + (byte >> 6))
	cmpq	%rcx, %rdx
	ja	L_return_0

	movq	%r8, %r12 # qword
	bswapq	%r12
	movq	%r12, %r10
	shlq	$5, %r12
	shlq	$2, %r10
	shrq	$53, %r12 # r12 = (qword.swp << 5) >> 53 = (qword.swp >> 48) & 0x07FF
	shrq	$61, %r10 # r10 = (qword.swp << 2) >> 61 = (qword.swp >> 59) & 0x1F
	shrq	$16, %r8 # qword >>= 16
	addq	$3, %r10 # r10 += 3

L_0x089:
	leaq	(%rax,%r9), %r11 # r11 = dst_written + (byte >> 6) ...
	addq	%r10, %r11 # + r10

	cmpq	%rsi, %r11 # r11 > dst_size?
	jae	L_0x0d2

	movq	%r8, (%rdi,%rax) # dst[dst_written] = r8
	addq	%r9, %rax
	movq	%rax, %r8
	subq	%r12, %r8
	jb	L_return_0
	cmpq	$8, %r12
	jb	L_0x102

L_0x0ae:
	movq	(%rdi,%r8), %r9
	addq	$8, %r8
	movq	%r9, (%rdi,%rax)
	addq	$8, %rax
	subq	$8, %r10
	ja	L_0x0ae
	addq	%r10, %rax

	# Get op
	movzbq	(%rdx), %r9
	movq	(%rdx), %r8
	jmpq	*(%rbx,%r9,8)

L_0x0d2:
	testq	%r9, %r9
	je	L_0x0f6
	leaq	8(%rsi), %r11

L_0x0db:
	movb	%r8b, (%rdi,%rax)
	addq	$1, %rax
	cmpq	%rax, %r11
	je	L_return_size
	shrq	$8, %r8
	subq	$1, %r9
	jne	L_0x0db

L_0x0f6:
	movq	%rax, %r8
	subq	%r12, %r8
	jb	L_return_0

L_0x102:
	leaq	8(%rsi), %r11

L_0x106:
	movzbq	(%rdi,%r8), %r9
	addq	$1, %r8
	movb	%r9b, (%rdi,%rax)
	addq	$1, %rax
	cmpq	%rax, %r11
	je	L_return_size
	subq	$1, %r10
	jne	L_0x106
	movzbq	(%rdx), %r9
	movq	(%rdx), %r8
	jmpq	*(%rbx,%r9,8)

L_op_46:
	shrq	$6, %r9
	leaq	1(%rdx,%r9), %rdx
	cmpq	%rcx, %rdx
	ja	L_return_0
	movq	$56, %r10
	andq	%r8, %r10
	shrq	$8, %r8
	shrq	$3, %r10
	addq	$3, %r10
	jmp	L_0x089

L_op_07:
	shrq	$6, %r9
	leaq	3(%rdx,%r9), %rdx
	cmpq	%rcx, %rdx
	ja	L_return_0
	movq	$56, %r10
	movq	$65535, %r12
	andq	%r8, %r10
	shrq	$8, %r8
	shrq	$3, %r10
	andq	%r8, %r12
	shrq	$16, %r8
	addq	$3, %r10
	jmp	L_0x089

L_op_A0:
	shrq	$3, %r9
	andq	$3, %r9
	leaq	3(%rdx,%r9), %rdx
	cmpq	%rcx, %rdx
	ja	L_return_0
	movq	%r8, %r10
	andq	$775, %r10
	shrq	$10, %r8
	movzbq	%r10b, %r12
	shrq	$8, %r10
	shlq	$2, %r12
	orq	%r12, %r10
	movq	$16383, %r12
	addq	$3, %r10
	andq	%r8, %r12
	shrq	$14, %r8
	jmp	L_0x089

L_op_Fx:
	addq	$1, %rdx
	cmpq	%rcx, %rdx
	ja	L_return_0
	movq	%r8, %r10
	andq	$15, %r10
	jmp	L_0x218

L_op_F0:
	addq	$2, %rdx
	cmpq	%rcx, %rdx
	ja	L_return_0
	movq	%r8, %r10
	shrq	$8, %r10
	andq	$255, %r10
	addq	$16, %r10

L_0x218:
	movq	%rax, %r8
	subq	%r12, %r8
	leaq	(%rax,%r10), %r11
	cmpq	%rsi, %r11
	jae	L_0x102
	cmpq	$8, %r12
	jae	L_0x0ae
	jmp	L_0x102

L_op_Ex:
	# Ex [LIT] : copy x bytes
	andq	$15, %r8
	leaq	1(%rdx,%r8), %rdx
	jmp	L_copy_literals

L_op_E0:
	# E0 (cnt-16) [LIT] : copy cnt bytes
	shrq	$8, %r8 # r8 = next byte + 16
	andq	$255, %r8
	addq	$16, %r8
	leaq	2(%rdx,%r8), %rdx # src += 2 + r8

L_copy_literals:
	cmpq	%rcx, %rdx
	ja	L_return_0

	leaq	(%rax,%r8), %r11 # r11 = r8 + dst_written
	negq	%r8 # r8 = -r8
	cmpq	%rsi, %r11 # r11 > dst_size?
	ja	L_copy_literals_remainder
	leaq	(%rdi,%r11), %r11 # r11 = dst + r11

L_copy_literals_qword:
	# Copy literals ...
	movq	(%rdx,%r8), %r9
	movq	%r9, (%r11,%r8)
	addq	$8, %r8
	jae	L_copy_literals_qword

	# Update dst_written
	movq	%r11, %rax
	subq	%rdi, %rax

	# Get op
	movzbq	(%rdx), %r9
	movq	(%rdx), %r8
	jmpq	*(%rbx,%r9,8)

L_copy_literals_remainder:
	leaq	8(%rsi), %r11

L_copy_literals_bytes_loop:
	movzbq	(%rdx,%r8), %r9
	movb	%r9b, (%rdi,%rax)
	addq	$1, %rax
	cmpq	%rax, %r11
	je	L_return_size
	addq	$1, %r8
	jne	L_copy_literals_bytes_loop

	# Get op
	movzbq	(%rdx), %r9
	movq	(%rdx), %r8
	jmpq	*(%rbx,%r9,8)

L_return_0:
	xorq	%rax, %rax

L_return_size:
	popq	%r12
	popq	%rbx
	popq	%rbp
	ret

.data
.align 8
Lzvn_decode.opcode_table:
# 00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_return_size
.quad L_op_07
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_0E
.quad L_op_07
# 10
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_0E
.quad L_op_07
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_return_0
.quad L_op_07
# 20
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_return_0
.quad L_op_07
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_return_0
.quad L_op_07
# 30
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_return_0
.quad L_op_07
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_return_0
.quad L_op_07
# 40
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_46
.quad L_op_07
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_46
.quad L_op_07
# 50
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_46
.quad L_op_07
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_46
.quad L_op_07
# 60
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_46
.quad L_op_07
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_46
.quad L_op_07
# 70
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
# 80
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_46
.quad L_op_07
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_46
.quad L_op_07
# 90
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_46
.quad L_op_07
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_46
.quad L_op_07
# A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
# B0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
.quad L_op_A0
# C0
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_46
.quad L_op_07
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_00
.quad L_op_46
.quad L_op_07
# D0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
.quad L_return_0
# E0
.quad L_op_E0
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
.quad L_op_Ex
# F0
.quad L_op_F0
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
.quad L_op_Fx
