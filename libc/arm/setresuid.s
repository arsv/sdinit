# auto-generated, remove this line before editing
.equ NR_setresuid, 164

.text
.global setresuid

setresuid:
	stmfd	sp!,{r4,r5,r7,lr}
	ldr	r7, =NR_setresuid
	b	_syscall

.type setresuid,function
.size setresuid,.-setresuid
