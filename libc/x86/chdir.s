# auto-generated, remove this line before editing
.equ NR_chdir, 12

.text
.global chdir

chdir:
	mov	$NR_chdir, %al
	jmp	unisys

.type chdir,@function
.size chdir,.-chdir
