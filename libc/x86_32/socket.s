# auto-generated, remove this line before editing
.equ NR_socket, 41

.text
.global socket

socket:
	mov	$NR_socket, %al
	jmp	unisys

.type socket,@function
.size socket,.-socket
