# auto-generated, remove this line before editing
.equ NR_setpriority, 141

.text
.global setpriority

setpriority:
	mov	$NR_setpriority, %al
	jmp	_syscall

.type setpriority,@function
.size setpriority,.-setpriority
