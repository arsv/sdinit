.data
.align 4
.globl errno

errno:	.word 0

.type errno,object
.size errno,.-errno


.text
.align 2
.set reorder

.globl unisys

unisys:
	beq	$7, $0, ok
	sw	$2, errno
	li	$2, -1
ok:	jr	$31

.type unisys,function
.size unisys,.-unisys
