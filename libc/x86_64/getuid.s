# auto-generated, remove this line before editing
.equ NR_getuid, 102

.text
.global getuid

getuid:
	mov	$NR_getuid, %al
	jmp	unisys

.type getuid,@function
.size getuid,.-getuid