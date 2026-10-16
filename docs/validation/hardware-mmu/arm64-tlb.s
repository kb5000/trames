__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj16_ECsl4LB0XcnsVR_11trames_core:
	.cfi_startproc
	sub	sp, sp, #112
	.cfi_def_cfa_offset 112
	stp	x29, x30, [sp, #96]
	add	x29, sp, #96
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	.cfi_remember_state
	str	x20, [sp, #32]
	ldr	x8, [x21, #40]
	str	x8, [sp, #48]
	ldr	x26, [x21, #24]
	ldp	x24, x8, [x21]
	stur	x8, [x29, #-40]
	ldr	x8, [x26]
	ldr	x27, [x24, #952]
LBB75_1:
	mov	w9, #40
	madd	x28, x22, x9, x8
	ldrb	w22, [x28, #9]
	cmp	w22, #8
	ldrb	w19, [x28, #10]
	ldrb	w20, [x28, #11]
	eor	w8, w20, #0x1
	csel	w8, wzr, w8, ne
	cmp	w19, #2
	csel	w8, wzr, w8, ne
	tbnz	w8, #0, LBB75_37
	ldrb	w25, [x28, #37]
	ldr	x8, [x28, #24]
	ldrb	w9, [x28, #16]
	add	x10, x24, #696
	add	x11, x10, x25, lsl #3
	cmp	w25, #31
	add	x10, x24, #944
	str	x11, [sp, #16]
	csel	x10, x10, x11, eq
	ldr	x2, [x10]
	cmp	w9, #1
	str	x23, [sp, #40]
	b.gt	LBB75_7
	add	x8, x2, x8
	cmp	w9, #0
	mov	x2, x8
	cset	w9, ne
	str	w9, [sp, #28]
	csel	x8, x8, x8, eq
	str	x8, [sp, #8]
	ldrb	w23, [x28, #36]
	cmp	w19, #0
	cset	w8, ne
	cmp	w20, #0
	csel	w8, w19, w8, ne
	tbz	w8, #0, LBB75_25
LBB75_4:
	sub	x0, x29, #32
	ldur	x1, [x29, #-40]
	mov	x3, x22
	bl	__RINvNtCs7Hne36L0lGo_10trames_cpu10load_store12read_operandNtNtCslITGYdeqHog_13trames_memory8parallel4ViewECsl4LB0XcnsVR_11trames_core
	ldur	w8, [x29, #-32]
	tbnz	w8, #0, LBB75_84
	ldur	x8, [x29, #-16]
	tbz	w20, #0, LBB75_13
	ldur	x9, [x29, #-8]
	add	x10, x24, #160
	add	x10, x10, x23, lsl #4
	stp	x8, x9, [x10]
	b	LBB75_32
LBB75_7:
	cmp	w9, #2
	b.ne	LBB75_9
	add	x8, x2, x8
	str	x8, [sp, #8]
	mov	w8, #1
	str	w8, [sp, #28]
	ldrb	w23, [x28, #36]
	cmp	w19, #0
	cset	w8, ne
	cmp	w20, #0
	csel	w8, w19, w8, ne
	tbnz	w8, #0, LBB75_4
	b	LBB75_25
LBB75_9:
	ldrb	w8, [x28, #38]
	ldrb	w9, [x28, #17]
	cmp	w9, #2
	b.eq	LBB75_19
	cmp	w9, #6
	b.ne	LBB75_21
	cmp	w8, #31
	b.eq	LBB75_22
	lsl	x8, x8, #3
	add	x9, x24, #696
	ldrsw	x8, [x9, x8]
	b	LBB75_24
LBB75_13:
	cmp	w19, #1
	b.ls	LBB75_17
	cmp	w22, #8
	b.hi	LBB75_90
	mov	w9, #64
	sub	w9, w9, w22, lsl #3
	cmp	w9, #64
	b.hs	LBB75_89
	lsl	x8, x8, x9
	asr	x8, x8, x9
LBB75_17:
	cmp	w23, #31
	b.eq	LBB75_32
	mov	w9, w8
	cmp	w19, #3
	csel	x8, x9, x8, eq
	add	x9, x24, #696
	str	x8, [x9, x23, lsl #3]
	b	LBB75_32
LBB75_19:
	cmp	w8, #31
	b.eq	LBB75_22
	lsl	x8, x8, #3
	add	x9, x24, #696
	ldr	w8, [x9, x8]
	b	LBB75_24
LBB75_21:
	cmp	w8, #31
	b.ne	LBB75_23
LBB75_22:
	mov	x8, #0
	b	LBB75_24
LBB75_23:
	add	x9, x24, #696
	ldr	x8, [x9, x8, lsl #3]
LBB75_24:
	str	wzr, [sp, #28]
	ldrb	w9, [x28, #18]
	lsl	x8, x8, x9
	add	x2, x8, x2
	ldrb	w23, [x28, #36]
	cmp	w19, #0
	cset	w8, ne
	cmp	w20, #0
	csel	w8, w19, w8, ne
	tbnz	w8, #0, LBB75_4
LBB75_25:
	tbz	w20, #0, LBB75_27
	add	x8, x24, #160
	add	x8, x8, x23, lsl #4
	ldp	x4, x5, [x8]
	ldr	x19, [sp, #40]
	b	LBB75_31
LBB75_27:
	cmp	w23, #31
	b.ne	LBB75_29
	mov	x4, #0
	b	LBB75_30
LBB75_29:
	add	x8, x24, #696
	ldr	x4, [x8, x23, lsl #3]
LBB75_30:
	ldr	x19, [sp, #40]
	mov	x5, #0
LBB75_31:
	sub	x0, x29, #32
	ldur	x1, [x29, #-40]
	mov	x3, x22
	bl	__RINvNtCs7Hne36L0lGo_10trames_cpu10load_store13write_operandNtNtCslITGYdeqHog_13trames_memory8parallel4ViewECsl4LB0XcnsVR_11trames_core
	ldurb	w8, [x29, #-32]
	cmp	w8, #255
	b.ne	LBB75_85
LBB75_32:
	ldr	x23, [sp, #40]
	ldr	w8, [sp, #28]
	tbz	w8, #0, LBB75_37
	cmp	w25, #31
	b.ne	LBB75_35
	add	x8, x24, #944
	b	LBB75_36
LBB75_35:
	ldr	x8, [sp, #16]
LBB75_36:
	ldr	x9, [sp, #8]
	str	x9, [x8]
LBB75_37:
	add	x27, x27, #4
	str	x27, [x24, #952]
	ldrb	w8, [x28, #8]
	cmp	w8, #23
	b.gt	LBB75_41
	cmp	w8, #22
	b.eq	LBB75_43
	cmp	w8, #23
	b.ne	LBB75_51
	ldrb	w8, [x28, #11]
	cbz	w8, LBB75_47
	b	LBB75_51
LBB75_41:
	cmp	w8, #24
	b.eq	LBB75_46
	cmp	w8, #25
	b.eq	LBB75_47
	b	LBB75_51
LBB75_43:
	ldrb	w8, [x28, #10]
	cbz	w8, LBB75_47
	cmp	w8, #2
	b.ne	LBB75_51
	ldrb	w8, [x28, #11]
	tbnz	w8, #0, LBB75_47
	b	LBB75_51
LBB75_46:
	ldrb	w8, [x28, #9]
	tbz	w8, #0, LBB75_51
LBB75_47:
	sub	x8, x29, #32
	ldur	x0, [x29, #-40]
	bl	__RNvXs1_NtCslITGYdeqHog_13trames_memory8parallelNtB5_4ViewNtNtB7_10cpu_memory9CpuMemory20instruction_identity
	ldur	x8, [x29, #-32]
	ldr	x9, [x26, #40]
	cmp	x8, x9
	b.ne	LBB75_57
	ldur	x8, [x29, #-24]
	ldr	x9, [x26, #48]
	cmp	x8, x9
	b.ne	LBB75_57
	ldur	x8, [x29, #-16]
	ldr	x9, [x26, #56]
	cmp	x8, x9
	b.ne	LBB75_57
	ldr	x27, [x24, #952]
LBB75_51:
	add	x9, x23, #1
	ldr	x8, [sp, #48]
	cmp	x9, x8
	b.hs	LBB75_58
	and	x8, x27, #0xfffffffffffff000
	ldr	x10, [x26, #8]
	cmp	x8, x10
	b.ne	LBB75_57
	tst	x27, #0x3
	b.ne	LBB75_59
	ubfx	x22, x27, #2, #10
	ldr	x8, [x26]
	mov	w10, #40
	umaddl	x10, w22, w10, x8
	ldr	x11, [x10]
	ldr	x12, [x26, #64]
	cmp	x11, x12
	b.ne	LBB75_88
	mov	x23, x9
	ldrb	w9, [x10, #8]
Lloh854:
	adrp	x12, LJTI75_0@PAGE
Lloh855:
	add	x12, x12, LJTI75_0@PAGEOFF
	adr	x10, LBB75_1
	ldrh	w11, [x12, x9, lsl #1]
	add	x10, x10, x11, lsl #2
	br	x10
LBB75_56:
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj0_ECsl4LB0XcnsVR_11trames_core
LBB75_57:
	.cfi_restore_state
	.cfi_remember_state
	add	x8, x23, #1
	mov	x9, #-1
	ldr	x10, [sp, #32]
	stp	x9, x8, [x10]
	b	LBB75_87
LBB75_58:
	mov	w8, #2
	add	x9, x23, #1
	ldr	x10, [sp, #32]
	stp	x8, x9, [x10]
	b	LBB75_87
LBB75_59:
	add	x8, x23, #1
	str	x8, [x24, #976]
	mov	w8, #3
	ldr	x9, [sp, #32]
	str	x8, [x9]
	str	wzr, [x9, #8]
	str	x27, [x9, #16]
	mov	w8, #2
	strb	w8, [x9, #24]
	str	x27, [x9, #32]
	b	LBB75_87
LBB75_60:
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj10_ECsl4LB0XcnsVR_11trames_core
LBB75_61:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKje_ECsl4LB0XcnsVR_11trames_core
LBB75_62:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKjb_ECsl4LB0XcnsVR_11trames_core
LBB75_63:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj4_ECsl4LB0XcnsVR_11trames_core
LBB75_64:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKjc_ECsl4LB0XcnsVR_11trames_core
LBB75_65:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj9_ECsl4LB0XcnsVR_11trames_core
LBB75_66:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj19_ECsl4LB0XcnsVR_11trames_core
LBB75_67:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj2_ECsl4LB0XcnsVR_11trames_core
LBB75_68:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKjf_ECsl4LB0XcnsVR_11trames_core
LBB75_69:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj3_ECsl4LB0XcnsVR_11trames_core
LBB75_70:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj7_ECsl4LB0XcnsVR_11trames_core
LBB75_71:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj1_ECsl4LB0XcnsVR_11trames_core
LBB75_72:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj11_ECsl4LB0XcnsVR_11trames_core
LBB75_73:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj14_ECsl4LB0XcnsVR_11trames_core
LBB75_74:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj5_ECsl4LB0XcnsVR_11trames_core
LBB75_75:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj15_ECsl4LB0XcnsVR_11trames_core
LBB75_76:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKjd_ECsl4LB0XcnsVR_11trames_core
LBB75_77:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj6_ECsl4LB0XcnsVR_11trames_core
LBB75_78:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj12_ECsl4LB0XcnsVR_11trames_core
LBB75_79:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKja_ECsl4LB0XcnsVR_11trames_core
LBB75_80:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj8_ECsl4LB0XcnsVR_11trames_core
LBB75_81:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj17_ECsl4LB0XcnsVR_11trames_core
LBB75_82:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj13_ECsl4LB0XcnsVR_11trames_core
LBB75_83:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj18_ECsl4LB0XcnsVR_11trames_core
LBB75_84:
	.cfi_restore_state
	.cfi_remember_state
	ldp	x9, x8, [x29, #-24]
	ldr	x19, [sp, #40]
	b	LBB75_86
LBB75_85:
	ldp	x9, x8, [x29, #-32]
LBB75_86:
	str	x19, [x24, #976]
	mov	w10, #3
	ldr	x11, [sp, #32]
	stp	x10, xzr, [x11]
	stp	x27, x9, [x11, #16]
	str	x8, [x11, #32]
LBB75_87:
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	ret
LBB75_88:
	.cfi_restore_state
	.cfi_remember_state
	add	x23, x23, #1
	ldr	x20, [sp, #32]
	.cfi_def_cfa wsp, 112
	ldp	x29, x30, [sp, #96]
	add	sp, sp, #112
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached11tail_decodeNtNtCslITGYdeqHog_13trames_memory8parallel4ViewECsl4LB0XcnsVR_11trames_core
LBB75_89:
	.cfi_restore_state
Lloh856:
	adrp	x0, l_anon.731397bb9a610793db00e25b847a0223.358@PAGE
Lloh857:
	add	x0, x0, l_anon.731397bb9a610793db00e25b847a0223.358@PAGEOFF
	bl	__RNvNtNtCsce4t4En13qn_4core9panicking11panic_const24panic_const_shl_overflow
LBB75_90:
Lloh858:
	adrp	x0, l_anon.731397bb9a610793db00e25b847a0223.357@PAGE
Lloh859:
	add	x0, x0, l_anon.731397bb9a610793db00e25b847a0223.357@PAGEOFF
	bl	__RNvNtNtCsce4t4En13qn_4core9panicking11panic_const24panic_const_sub_overflow
	.loh AdrpAdd	Lloh854, Lloh855
	.loh AdrpAdd	Lloh856, Lloh857
	.loh AdrpAdd	Lloh858, Lloh859
	.cfi_endproc

__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj4_ECsl4LB0XcnsVR_11trames_core:
	.cfi_startproc
	ldr	x8, [x21, #40]
	ldr	x10, [x21, #24]
	ldr	x9, [x21]
	add	x11, x9, #944
	add	x12, x9, #696
	ldr	x17, [x10]
	ldr	x15, [x9, #952]
	mov	w13, #40
Lloh890:
	adrp	x14, LJTI82_0@PAGE
Lloh891:
	add	x14, x14, LJTI82_0@PAGEOFF
LBB82_1:
	mov	x16, x15
	madd	x17, x22, x13, x17
	ldrb	w15, [x17, #36]
	ldrb	w19, [x17, #37]
	ldrb	w22, [x17, #39]
	ldr	x17, [x17, #16]
	add	x24, x12, x19, lsl #3
	cmp	w19, #31
	csel	x19, x11, x24, eq
	ldr	x19, [x19]
	add	x17, x19, x17
	mov	w19, w17
	cmp	w22, #0
	csel	x17, x17, x19, ne
	cmp	w15, #31
	b.ne	LBB82_3
	str	x17, [x11]
	add	x15, x16, #4
	str	x15, [x9, #952]
	add	x19, x23, #1
	cmp	x19, x8
	b.lo	LBB82_4
	b	LBB82_9
LBB82_3:
	str	x17, [x12, x15, lsl #3]
	add	x15, x16, #4
	str	x15, [x9, #952]
	add	x19, x23, #1
	cmp	x19, x8
	b.hs	LBB82_9
LBB82_4:
	and	x17, x15, #0xfffffffffffff000
	ldr	x22, [x10, #8]
	cmp	x17, x22
	b.ne	LBB82_10
	tst	x16, #0x3
	b.ne	LBB82_11
	ubfx	x22, x15, #2, #10
	ldr	x17, [x10]
	umaddl	x16, w22, w13, x17
	ldr	x24, [x16]
	ldr	x25, [x10, #64]
	cmp	x24, x25
	b.ne	LBB82_36
	ldrb	w16, [x16, #8]
	mov	x23, x19
	adr	x19, LBB82_1
	ldrb	w24, [x14, x16]
	add	x19, x19, x24, lsl #2
	br	x19
LBB82_8:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj0_ECsl4LB0XcnsVR_11trames_core
LBB82_9:
	mov	w8, #2
	stp	x8, x19, [x20]
	ret
LBB82_10:
	add	x8, x23, #1
	mov	x9, #-1
	stp	x9, x8, [x20]
	ret
LBB82_11:
	add	x8, x23, #1
	str	x8, [x9, #976]
	mov	w8, #3
	str	x8, [x20]
	str	wzr, [x20, #8]
	add	x8, x16, #4
	str	x8, [x20, #16]
	mov	w9, #2
	strb	w9, [x20, #24]
	str	x8, [x20, #32]
	ret
LBB82_12:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj11_ECsl4LB0XcnsVR_11trames_core
LBB82_13:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKjc_ECsl4LB0XcnsVR_11trames_core
LBB82_14:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKjf_ECsl4LB0XcnsVR_11trames_core
LBB82_15:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj5_ECsl4LB0XcnsVR_11trames_core
LBB82_16:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKja_ECsl4LB0XcnsVR_11trames_core
LBB82_17:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKjd_ECsl4LB0XcnsVR_11trames_core
LBB82_18:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj19_ECsl4LB0XcnsVR_11trames_core
LBB82_19:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj12_ECsl4LB0XcnsVR_11trames_core
LBB82_20:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj15_ECsl4LB0XcnsVR_11trames_core
LBB82_21:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj16_ECsl4LB0XcnsVR_11trames_core
LBB82_22:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj13_ECsl4LB0XcnsVR_11trames_core
LBB82_23:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKje_ECsl4LB0XcnsVR_11trames_core
LBB82_24:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj17_ECsl4LB0XcnsVR_11trames_core
LBB82_25:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj8_ECsl4LB0XcnsVR_11trames_core
LBB82_26:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKjb_ECsl4LB0XcnsVR_11trames_core
LBB82_27:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj14_ECsl4LB0XcnsVR_11trames_core
LBB82_28:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj18_ECsl4LB0XcnsVR_11trames_core
LBB82_29:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj7_ECsl4LB0XcnsVR_11trames_core
LBB82_30:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj9_ECsl4LB0XcnsVR_11trames_core
LBB82_31:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj2_ECsl4LB0XcnsVR_11trames_core
LBB82_32:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj3_ECsl4LB0XcnsVR_11trames_core
LBB82_33:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj1_ECsl4LB0XcnsVR_11trames_core
LBB82_34:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj6_ECsl4LB0XcnsVR_11trames_core
LBB82_35:
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj10_ECsl4LB0XcnsVR_11trames_core
LBB82_36:
	add	x23, x23, #1
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached11tail_decodeNtNtCslITGYdeqHog_13trames_memory8parallel4ViewECsl4LB0XcnsVR_11trames_core
	.loh AdrpAdd	Lloh890, Lloh891
	.cfi_endproc
