# auto-generated, remove this line before editing
.equ NR_ioctl, 16

.text
.global ioctl

ioctl:
	mov	$NR_ioctl, %al
	jmp	_syscall

.type ioctl,@function
.size ioctl,.-ioctl
