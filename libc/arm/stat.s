# auto-generated, remove this line before editing
.equ NR_stat, 106

.text
.global stat

stat:
        stmfd	sp!,{r4,r5,r7,lr}
	ldr	r4, [sp,#16]
	ldr	r5, [sp,#20]
        ldr     r7, =NR_stat
	swi	0
	b	unisys

.type stat,function
.size stat,.-stat
