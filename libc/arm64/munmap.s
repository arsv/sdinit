# auto-generated, remove this line before editing
.equ NR_munmap, 215

.text
.global munmap

munmap:
	mov	x8, NR_munmap
	b	_syscall

.type munmap,function
.size munmap,.-munmap
