# auto-generated, remove this line before editing
.equ NR_umask, 95

.text
.global umask

umask:
	mov	$NR_umask, %al
	jmp	unisys

.type umask,@function
.size umask,.-umask
