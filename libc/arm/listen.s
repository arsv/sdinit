# auto-generated, remove this line before editing
.equ NR_listen, 284

.text
.global listen

listen:
        stmfd	sp!,{r4,r5,r7,lr}
	ldr	r4, [sp,#16]
	ldr	r5, [sp,#20]
        ldr     r7, =NR_listen
	swi	0
	b	unisys

.type listen,function
.size listen,.-listen
