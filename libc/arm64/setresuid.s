# auto-generated, remove this line before editing
.equ NR_setresuid, 147

.text
.global setresuid

setresuid:
	mov	x8, NR_setresuid
	b	_syscall

.type setresuid,function
.size setresuid,.-setresuid
