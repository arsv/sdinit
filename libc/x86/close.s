# auto-generated, remove this line before editing
.equ NR_close, 6

.text
.global close

close:
	mov	$NR_close, %al
	jmp	_syscall

.type close,@function
.size close,.-close
