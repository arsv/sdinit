# auto-generated, remove this line before editing
.equ NR_open, 5

.text
.global open

open:
	mov	$NR_open, %al
	jmp	unisys

.type open,@function
.size open,.-open
