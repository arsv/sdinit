# auto-generated, remove this line before editing
.equ NR_geteuid, 107

.text
.global geteuid

geteuid:
	mov	$NR_geteuid, %al
	jmp	unisys

.type geteuid,@function
.size geteuid,.-geteuid
