# auto-generated, remove this line before editing
.equ NR_close, 6

.text
.global close

close:
        stmfd	sp!,{r4,r5,r7,lr}
	ldr	r4, [sp,#16]
	ldr	r5, [sp,#20]
        ldr     r7, =NR_close
	swi	0
	b	unisys

.type close,function
.size close,.-close
