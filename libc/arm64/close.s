# auto-generated, remove this line before editing
.equ NR_close, 57

.text
.global close

close:
	mov	x8, NR_close
	b	_syscall

.type close,function
.size close,.-close
