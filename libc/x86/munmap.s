# auto-generated, remove this line before editing
.equ NR_munmap, 91

.text
.global munmap

munmap:
	mov	$NR_munmap, %al
	jmp	_syscall

.type munmap,@function
.size munmap,.-munmap
