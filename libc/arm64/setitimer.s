# auto-generated, remove this line before editing
.equ NR_setitimer, 103

.text
.global setitimer

setitimer:
	mov	x8, NR_setitimer
	b	unisys

.type setitimer,function
.size setitimer,.-setitimer
