# auto-generated, remove this line before editing
.equ NR_getdents64, 4219

.text
.set reorder
.global getdents64
.ent getdents64

getdents64:
	li	$2, NR_getdents64
	syscall
	la	$25, _syscall
	jr	$25

.end getdents64
