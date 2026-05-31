	.file	"init.c"
	.option pic
	.option norelax
	.text
	.section	.rodata.str1.8,"aMS",@progbits,1
	.align	3
.LC0:
	.string	"init: starting sh\n"
	.align	3
.LC1:
	.string	"init: fork %s failed\n"
	.align	3
.LC2:
	.string	"init: exec %s failed\n"
	.align	3
.LC3:
	.string	"init: wait returned an error\n"
	.section	.text.startup,"ax",@progbits
	.align	1
	.globl	main
	.type	main, @function
main:
	addi	sp,sp,-80
	li	a2,0
	li	a1,1
	li	a0,2
	sd	ra,72(sp)
	sd	s0,64(sp)
	sd	s1,56(sp)
	sd	s3,40(sp)
	sd	s4,32(sp)
	sd	s5,24(sp)
	sd	s2,48(sp)
	call	dev@plt
	li	a0,0
	call	dup@plt
	li	a0,0
	lla	s0,.LANCHOR0
	call	dup@plt
	li	s1,0
	mv	s3,s0
	lla	s4,.LC0
	lla	s5,.LC1
.L2:
	ld	a5,0(s0)
	bne	a5,zero,.L6
	call	shutdown@plt
	ld	ra,72(sp)
	ld	s0,64(sp)
	ld	s1,56(sp)
	ld	s2,48(sp)
	ld	s3,40(sp)
	ld	s4,32(sp)
	ld	s5,24(sp)
	li	a0,0
	addi	sp,sp,80
	jr	ra
.L6:
	mv	a0,s4
	call	printf@plt
	call	fork@plt
	mv	s2,a0
	bge	a0,zero,.L3
	ld	a1,0(s0)
	mv	a0,s5
	call	printf@plt
.L4:
	addiw	s1,s1,1
	addi	s0,s0,8
	j	.L2
.L3:
	bne	a0,zero,.L5
	slli	s1,s1,3
	add	s1,s3,s1
	ld	a0,0(s1)
	mv	a1,sp
	sd	zero,8(sp)
	sd	a0,0(sp)
	call	exec@plt
	ld	a1,0(s1)
	lla	a0,.LC2
	call	printf@plt
.L10:
	li	a0,1
	call	exit@plt
.L5:
	li	a0,0
	call	wait@plt
	beq	s2,a0,.L4
	bge	a0,zero,.L5
	lla	a0,.LC3
	call	printf@plt
	j	.L10
	.size	main, .-main
	.globl	test
	.section	.rodata.str1.8
	.align	3
.LC4:
	.string	"getcwd"
	.align	3
.LC5:
	.string	"write"
	.align	3
.LC6:
	.string	"getpid"
	.align	3
.LC7:
	.string	"times"
	.align	3
.LC8:
	.string	"uname"
	.align	3
.LC9:
	.string	"getppid"
	.align	3
.LC10:
	.string	"yield"
	.align	3
.LC11:
	.string	"gettimeofday"
	.align	3
.LC12:
	.string	"sleep"
	.align	3
.LC13:
	.string	"fork"
	.align	3
.LC14:
	.string	"clone"
	.align	3
.LC15:
	.string	"wait"
	.align	3
.LC16:
	.string	"waitpid"
	.align	3
.LC17:
	.string	"execve"
	.align	3
.LC18:
	.string	"exit"
	.globl	argv
	.align	3
.LC19:
	.string	"sh"
	.section	.data.rel.local,"aw"
	.align	3
	.set	.LANCHOR0,. + 0
	.type	test, @object
	.size	test, 128
test:
	.dword	.LC4
	.dword	.LC5
	.dword	.LC6
	.dword	.LC7
	.dword	.LC8
	.dword	.LC9
	.dword	.LC10
	.dword	.LC11
	.dword	.LC12
	.dword	.LC13
	.dword	.LC14
	.dword	.LC15
	.dword	.LC16
	.dword	.LC17
	.dword	.LC18
	.dword	0
	.type	argv, @object
	.size	argv, 16
argv:
	.dword	.LC19
	.dword	0
	.ident	"GCC: (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0"
	.section	.note.GNU-stack,"",@progbits
