# auto-generated, remove this line before editing
.equ NR_prctl, 172

.text
.global prctl

prctl:
	stmfd	sp!,{r4,r5,r7,lr}
	ldr	r4, [sp,#16]
	ldr	r5, [sp,#20]
	ldr	r7, =NR_prctl
	b	unisys

.type prctl,function
.size prctl,.-prctl
