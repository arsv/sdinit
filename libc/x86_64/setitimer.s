# auto-generated, remove this line before editing
.equ NR_setitimer, 38

.text
.global setitimer

setitimer:
	mov	$NR_setitimer, %al
	jmp	_syscall

.type setitimer,@function
.size setitimer,.-setitimer
