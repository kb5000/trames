__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj16_ECsl4LB0XcnsVR_11trames_core:
Lfunc_begin39:
	.cfi_startproc
	.cfi_personality 155, __RNvCsfmtydf4BlqR_7___rustc19rust_eh_personality
	.cfi_lsda 16, Lexception39
	sub	sp, sp, #256
	.cfi_def_cfa_offset 256
	stp	x29, x30, [sp, #240]
	add	x29, sp, #240
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	.cfi_remember_state
	stur	x26, [x29, #-96]
	stur	x24, [x29, #-80]
	ldr	x8, [x21, #40]
	stur	x8, [x29, #-112]
	ldr	x28, [x21, #24]
	str	x21, [sp, #96]
	ldp	x21, x16, [x21]
	add	x9, x21, #944
	add	x8, x21, #696
	stp	x9, x8, [sp, #112]
	add	x9, x21, #160
	mov	w8, #4104
	add	x8, x16, x8
	stp	x8, x9, [sp, #80]
	sub	x9, x29, #24
	mov	w8, #5
	orr	x8, x9, x8
	str	x8, [sp, #48]
	orr	x10, x9, #0x3
	orr	x8, x9, #0x2
	stp	x8, x10, [sp, #32]
	ldr	x8, [x28]
	ldr	x19, [x21, #952]
	mov	w26, #40
	stur	x16, [x29, #-104]
	stur	x21, [x29, #-88]
LBB75_1:
	madd	x22, x22, x26, x8
	ldrb	w26, [x22, #9]
	cmp	w26, #8
	ldrb	w24, [x22, #10]
	ldrb	w21, [x22, #11]
	eor	w8, w21, #0x1
	csel	w8, wzr, w8, ne
	cmp	w24, #2
	csel	w8, wzr, w8, ne
	tbz	w8, #0, LBB75_3
	ldur	x21, [x29, #-88]
	mov	w26, #40
	b	LBB75_349
LBB75_3:
	ldrb	w12, [x22, #37]
	ldr	x8, [x22, #24]
	ldrb	w9, [x22, #16]
	ldr	x10, [sp, #120]
	add	x11, x10, x12, lsl #3
	stp	x11, x12, [sp, #64]
	cmp	w12, #31
	ldr	x10, [sp, #112]
	csel	x10, x10, x11, eq
	ldr	x27, [x10]
	cmp	w9, #1
	b.gt	LBB75_18
	add	x8, x27, x8
	cmp	w9, #0
	mov	x27, x8
	cset	w9, ne
	str	w9, [sp, #108]
	csel	x8, x8, x8, eq
	str	x8, [sp, #56]
	ldrb	w10, [x22, #36]
	cmp	w24, #0
	cset	w8, ne
	cmp	w21, #0
	csel	w8, w24, w8, ne
	tbz	w8, #0, LBB75_67
LBB75_5:
	sub	x8, x26, #1
	eor	x9, x26, x8
	cmp	x9, x8
	b.ls	LBB75_442
	rbit	x8, x26
	clz	x8, x8
	cmp	x8, #1
	str	x10, [sp, #16]
	b.le	LBB75_24
	cmp	x8, #2
	b.eq	LBB75_32
	cmp	x8, #3
	b.ne	LBB75_41
	and	x8, x27, #0xfff
	cmp	x8, #4088
	b.hi	LBB75_369
	and	x12, x27, #0xffffffffffffff
	ldr	x13, [x16]
	ldr	x8, [x13, #120]
	cbz	x25, LBB75_13
	ldur	x9, [x29, #-80]
	cmp	x9, x8
	b.ne	LBB75_13
	ldr	x10, [x25, #56]
	sub	x9, x12, x10
	lsr	x11, x9, #24
	cbz	x11, LBB75_173
LBB75_13:
	and	x10, x27, #0xffffffff000000
	lsr	x9, x27, #32
	eor	w9, w9, w27, lsr #28
	eor	w9, w9, w27, lsr #24
	lsr	x11, x27, #40
	eor	w9, w9, w11
	and	x9, x9, #0xf
	ldr	x11, [sp, #80]
	add	x9, x11, x9, lsl #4
	ldr	x11, [x9]
	orr	x10, x10, #0x1
	cmp	x11, x10
	b.ne	LBB75_303
	ldr	x25, [x9, #8]
	cbz	x25, LBB75_369
	str	x25, [x16, #4360]
	stur	x8, [x29, #-80]
	mov	x0, x25
LBB75_16:
	ldr	x8, [x25]
	ldr	x8, [x8, #16]
	ldr	x9, [x25, #56]
	sub	x8, x8, x9
	stur	x8, [x29, #-96]
LBB75_17:
	ldr	x10, [x0, #56]
	sub	x9, x12, x10
	b	LBB75_174
LBB75_18:
	cmp	w9, #2
	b.ne	LBB75_20
	add	x8, x27, x8
	str	x8, [sp, #56]
	mov	w8, #1
	str	w8, [sp, #108]
	ldrb	w10, [x22, #36]
	cmp	w24, #0
	cset	w8, ne
	cmp	w21, #0
	csel	w8, w24, w8, ne
	tbnz	w8, #0, LBB75_5
	b	LBB75_67
LBB75_20:
	ldrb	w8, [x22, #38]
	ldrb	w9, [x22, #17]
	cmp	w9, #2
	b.eq	LBB75_61
	cmp	w9, #6
	b.ne	LBB75_63
	cmp	w8, #31
	b.eq	LBB75_64
	lsl	x8, x8, #3
	ldr	x9, [sp, #120]
	ldrsw	x8, [x9, x8]
	b	LBB75_66
LBB75_24:
	cbnz	x8, LBB75_51
	stp	x20, x23, [sp]
	and	x23, x27, #0xffffffffffffff
	ldr	x20, [x16]
	ldr	x8, [x20, #120]
	cbz	x25, LBB75_28
	ldur	x9, [x29, #-80]
	cmp	x9, x8
	b.ne	LBB75_28
	ldr	x10, [x25, #56]
	sub	x9, x23, x10
	lsr	x11, x9, #24
	cbz	x11, LBB75_123
LBB75_28:
	and	x10, x27, #0xffffffff000000
	lsr	x9, x27, #32
	eor	w9, w9, w27, lsr #28
	eor	w9, w9, w27, lsr #24
	lsr	x11, x27, #40
	eor	w9, w9, w11
	and	x9, x9, #0xf
	ldr	x11, [sp, #80]
	add	x9, x11, x9, lsl #4
	ldr	x11, [x9]
	orr	x10, x10, #0x1
	cmp	x11, x10
	b.ne	LBB75_269
	ldr	x25, [x9, #8]
	cbz	x25, LBB75_279
	str	x25, [x16, #4360]
	stur	x8, [x29, #-80]
	mov	x0, x25
LBB75_31:
	ldr	x8, [x25]
	ldr	x8, [x8, #16]
	ldr	x9, [x25, #56]
	sub	x8, x8, x9
	stur	x8, [x29, #-96]
	ldr	x10, [x0, #56]
	sub	x9, x23, x10
	cmp	x23, x10
	b.hs	LBB75_124
	b	LBB75_433
LBB75_32:
	and	x8, x27, #0xfff
	cmp	x8, #4092
	b.hi	LBB75_326
	and	x12, x27, #0xffffffffffffff
	ldr	x13, [x16]
	ldr	x8, [x13, #120]
	cbz	x25, LBB75_36
	ldur	x9, [x29, #-80]
	cmp	x9, x8
	b.ne	LBB75_36
	ldr	x10, [x25, #56]
	sub	x9, x12, x10
	lsr	x11, x9, #24
	cbz	x11, LBB75_147
LBB75_36:
	and	x10, x27, #0xffffffff000000
	lsr	x9, x27, #32
	eor	w9, w9, w27, lsr #28
	eor	w9, w9, w27, lsr #24
	lsr	x11, x27, #40
	eor	w9, w9, w11
	and	x9, x9, #0xf
	ldr	x11, [sp, #80]
	add	x9, x11, x9, lsl #4
	ldr	x11, [x9]
	orr	x10, x10, #0x1
	cmp	x11, x10
	b.ne	LBB75_297
	ldr	x25, [x9, #8]
	cbz	x25, LBB75_326
	str	x25, [x16, #4360]
	stur	x8, [x29, #-80]
	mov	x0, x25
LBB75_39:
	ldr	x8, [x25]
	ldr	x8, [x8, #16]
	ldr	x9, [x25, #56]
	sub	x8, x8, x9
	stur	x8, [x29, #-96]
LBB75_40:
	ldr	x10, [x0, #56]
	sub	x9, x12, x10
	b	LBB75_148
LBB75_41:
	cmp	x8, #4
	b.ne	LBB75_442
	and	x8, x27, #0xfff
	cmp	x8, #4080
	b.hi	LBB75_291
	and	x12, x27, #0xffffffffffffff
	ldr	x13, [x16]
	ldr	x8, [x13, #120]
	cbz	x25, LBB75_46
	ldur	x9, [x29, #-80]
	cmp	x9, x8
	b.ne	LBB75_46
	ldr	x10, [x25, #56]
	sub	x9, x12, x10
	lsr	x11, x9, #24
	cbz	x11, LBB75_198
LBB75_46:
	and	x10, x27, #0xffffffff000000
	lsr	x9, x27, #32
	eor	w9, w9, w27, lsr #28
	eor	w9, w9, w27, lsr #24
	lsr	x11, x27, #40
	eor	w9, w9, w11
	and	x9, x9, #0xf
	ldr	x11, [sp, #80]
	add	x9, x11, x9, lsl #4
	ldr	x11, [x9]
	orr	x10, x10, #0x1
	cmp	x11, x10
	b.ne	LBB75_309
	ldr	x25, [x9, #8]
	cbz	x25, LBB75_291
	str	x25, [x16, #4360]
	stur	x8, [x29, #-80]
	mov	x0, x25
LBB75_49:
	ldr	x8, [x25]
	ldr	x8, [x8, #16]
	ldr	x9, [x25, #56]
	sub	x8, x8, x9
	stur	x8, [x29, #-96]
LBB75_50:
	ldr	x10, [x0, #56]
	sub	x9, x12, x10
	b	LBB75_199
LBB75_51:
	cmp	x8, #1
	b.ne	LBB75_442
	and	x8, x27, #0xfff
	cmp	x8, #4095
	b.eq	LBB75_382
	and	x12, x27, #0xffffffffffffff
	ldr	x13, [x16]
	ldr	x8, [x13, #120]
	cbz	x25, LBB75_56
	ldur	x9, [x29, #-80]
	cmp	x9, x8
	b.ne	LBB75_56
	ldr	x10, [x25, #56]
	sub	x9, x12, x10
	lsr	x11, x9, #24
	cbz	x11, LBB75_223
LBB75_56:
	and	x10, x27, #0xffffffff000000
	lsr	x9, x27, #32
	eor	w9, w9, w27, lsr #28
	eor	w9, w9, w27, lsr #24
	lsr	x11, x27, #40
	eor	w9, w9, w11
	and	x9, x9, #0xf
	ldr	x11, [sp, #80]
	add	x9, x11, x9, lsl #4
	ldr	x11, [x9]
	orr	x10, x10, #0x1
	cmp	x11, x10
	b.ne	LBB75_315
	ldr	x25, [x9, #8]
	cbz	x25, LBB75_382
	str	x25, [x16, #4360]
	stur	x8, [x29, #-80]
	mov	x0, x25
LBB75_59:
	ldr	x8, [x25]
	ldr	x8, [x8, #16]
	ldr	x9, [x25, #56]
	sub	x8, x8, x9
	stur	x8, [x29, #-96]
LBB75_60:
	ldr	x10, [x0, #56]
	sub	x9, x12, x10
	b	LBB75_224
LBB75_61:
	cmp	w8, #31
	b.eq	LBB75_64
	lsl	x8, x8, #3
	ldr	x9, [sp, #120]
	ldr	w8, [x9, x8]
	b	LBB75_66
LBB75_63:
	cmp	w8, #31
	b.ne	LBB75_65
LBB75_64:
	mov	x8, #0
	b	LBB75_66
LBB75_65:
	ldr	x9, [sp, #120]
	ldr	x8, [x9, x8, lsl #3]
LBB75_66:
	str	wzr, [sp, #108]
	ldrb	w9, [x22, #18]
	lsl	x8, x8, x9
	add	x27, x8, x27
	ldrb	w10, [x22, #36]
	cmp	w24, #0
	cset	w8, ne
	cmp	w21, #0
	csel	w8, w24, w8, ne
	tbnz	w8, #0, LBB75_5
LBB75_67:
	tbz	w21, #0, LBB75_69
	ldr	x8, [sp, #88]
	add	x8, x8, x10, lsl #4
	ldp	x21, x24, [x8]
	b	LBB75_73
LBB75_69:
	cmp	w10, #31
	b.ne	LBB75_71
	mov	x21, #0
	b	LBB75_72
LBB75_71:
	ldr	x8, [sp, #120]
	ldr	x21, [x8, x10, lsl #3]
LBB75_72:
	mov	x24, #0
LBB75_73:
	sub	x8, x26, #1
	eor	x9, x26, x8
	cmp	x9, x8
	b.ls	LBB75_441
	rbit	x8, x26
	clz	x8, x8
	cmp	x8, #1
	b.le	LBB75_86
	cmp	x8, #2
	b.eq	LBB75_94
	cmp	x8, #3
	b.ne	LBB75_103
	stur	x21, [x29, #-24]
	and	x8, x27, #0xfff
	cmp	x8, #4088
	b.hi	LBB75_340
	and	x24, x27, #0xffffffffffffff
	ldr	x26, [x16]
	cbz	x25, LBB75_81
	ldr	x8, [x26, #120]
	ldur	x9, [x29, #-80]
	cmp	x9, x8
	b.ne	LBB75_81
	ldr	x8, [x25, #56]
	sub	x9, x24, x8
	lsr	x10, x9, #24
	cbz	x10, LBB75_184
LBB75_81:
	and	x9, x27, #0xffffffff000000
	lsr	x8, x27, #32
	eor	w8, w8, w27, lsr #28
	eor	w8, w8, w27, lsr #24
	lsr	x10, x27, #40
	eor	w8, w8, w10
	and	x8, x8, #0xf
	ldr	x10, [sp, #80]
	add	x8, x10, x8, lsl #4
	ldr	x10, [x8]
	orr	x9, x9, #0x1
	cmp	x10, x9
	b.ne	LBB75_306
	ldr	x25, [x8, #8]
	cbz	x25, LBB75_340
	str	x25, [x16, #4360]
	mov	x0, x25
LBB75_84:
	ldr	x8, [x26, #120]
	stur	x8, [x29, #-80]
	ldr	x8, [x25]
	ldr	x8, [x8, #16]
	ldr	x9, [x25, #56]
	sub	x8, x8, x9
	stur	x8, [x29, #-96]
LBB75_85:
	ldr	x8, [x0, #56]
	sub	x9, x24, x8
	b	LBB75_185
LBB75_86:
	cbnz	x8, LBB75_113
	sturb	w21, [x29, #-24]
	and	x24, x27, #0xffffffffffffff
	ldr	x26, [x16]
	cbz	x25, LBB75_90
	ldr	x8, [x26, #120]
	ldur	x9, [x29, #-80]
	cmp	x9, x8
	b.ne	LBB75_90
	ldr	x8, [x25, #56]
	sub	x9, x24, x8
	lsr	x10, x9, #24
	cbz	x10, LBB75_134
LBB75_90:
	and	x9, x27, #0xffffffff000000
	lsr	x8, x27, #32
	eor	w8, w8, w27, lsr #28
	eor	w8, w8, w27, lsr #24
	lsr	x10, x27, #40
	eor	w8, w8, w10
	and	x8, x8, #0xf
	ldr	x10, [sp, #80]
	add	x8, x10, x8, lsl #4
	ldr	x10, [x8]
	orr	x9, x9, #0x1
	cmp	x10, x9
	b.ne	LBB75_272
	ldr	x25, [x8, #8]
	cbz	x25, LBB75_277
	str	x25, [x16, #4360]
	mov	x0, x25
LBB75_93:
	ldr	x8, [x26, #120]
	stur	x8, [x29, #-80]
	ldr	x8, [x25]
	ldr	x8, [x8, #16]
	ldr	x9, [x25, #56]
	sub	x8, x8, x9
	stur	x8, [x29, #-96]
	ldr	x8, [x0, #56]
	sub	x9, x24, x8
	cmp	x24, x8
	b.hs	LBB75_135
	b	LBB75_433
LBB75_94:
	stur	w21, [x29, #-24]
	and	x8, x27, #0xfff
	cmp	x8, #4092
	b.hi	LBB75_323
	and	x24, x27, #0xffffffffffffff
	ldr	x26, [x16]
	cbz	x25, LBB75_98
	ldr	x8, [x26, #120]
	ldur	x9, [x29, #-80]
	cmp	x9, x8
	b.ne	LBB75_98
	ldr	x8, [x25, #56]
	sub	x9, x24, x8
	lsr	x10, x9, #24
	cbz	x10, LBB75_159
LBB75_98:
	and	x9, x27, #0xffffffff000000
	lsr	x8, x27, #32
	eor	w8, w8, w27, lsr #28
	eor	w8, w8, w27, lsr #24
	lsr	x10, x27, #40
	eor	w8, w8, w10
	and	x8, x8, #0xf
	ldr	x10, [sp, #80]
	add	x8, x10, x8, lsl #4
	ldr	x10, [x8]
	orr	x9, x9, #0x1
	cmp	x10, x9
	b.ne	LBB75_300
	ldr	x25, [x8, #8]
	cbz	x25, LBB75_323
	str	x25, [x16, #4360]
	mov	x0, x25
LBB75_101:
	ldr	x8, [x26, #120]
	stur	x8, [x29, #-80]
	ldr	x8, [x25]
	ldr	x8, [x8, #16]
	ldr	x9, [x25, #56]
	sub	x8, x8, x9
	stur	x8, [x29, #-96]
LBB75_102:
	ldr	x8, [x0, #56]
	sub	x9, x24, x8
	b	LBB75_160
LBB75_103:
	cmp	x8, #4
	b.ne	LBB75_441
	stp	x21, x24, [x29, #-24]
	and	x8, x27, #0xfff
	cmp	x8, #4080
	b.hi	LBB75_377
	and	x11, x27, #0xffffffffffffff
	ldr	x26, [x16]
	cbz	x25, LBB75_108
	ldr	x8, [x26, #120]
	ldur	x9, [x29, #-80]
	cmp	x9, x8
	b.ne	LBB75_108
	ldr	x8, [x25, #56]
	sub	x9, x11, x8
	lsr	x10, x9, #24
	cbz	x10, LBB75_209
LBB75_108:
	and	x9, x27, #0xffffffff000000
	lsr	x8, x27, #32
	eor	w8, w8, w27, lsr #28
	eor	w8, w8, w27, lsr #24
	lsr	x10, x27, #40
	eor	w8, w8, w10
	and	x8, x8, #0xf
	ldr	x10, [sp, #80]
	add	x8, x10, x8, lsl #4
	ldr	x10, [x8]
	orr	x9, x9, #0x1
	cmp	x10, x9
	b.ne	LBB75_312
	ldr	x25, [x8, #8]
	cbz	x25, LBB75_377
	str	x25, [x16, #4360]
	mov	x0, x25
LBB75_111:
	ldr	x8, [x26, #120]
	stur	x8, [x29, #-80]
	ldr	x8, [x25]
	ldr	x8, [x8, #16]
	ldr	x9, [x25, #56]
	sub	x8, x8, x9
	stur	x8, [x29, #-96]
LBB75_112:
	ldr	x8, [x0, #56]
	sub	x9, x11, x8
	b	LBB75_210
LBB75_113:
	cmp	x8, #1
	b.ne	LBB75_441
	sturh	w21, [x29, #-24]
	and	x8, x27, #0xfff
	cmp	x8, #4095
	b.eq	LBB75_380
	and	x24, x27, #0xffffffffffffff
	ldr	x26, [x16]
	cbz	x25, LBB75_118
	ldr	x8, [x26, #120]
	ldur	x9, [x29, #-80]
	cmp	x9, x8
	b.ne	LBB75_118
	ldr	x8, [x25, #56]
	sub	x9, x24, x8
	lsr	x10, x9, #24
	cbz	x10, LBB75_235
LBB75_118:
	and	x9, x27, #0xffffffff000000
	lsr	x8, x27, #32
	eor	w8, w8, w27, lsr #28
	eor	w8, w8, w27, lsr #24
	lsr	x10, x27, #40
	eor	w8, w8, w10
	and	x8, x8, #0xf
	ldr	x10, [sp, #80]
	add	x8, x10, x8, lsl #4
	ldr	x10, [x8]
	orr	x9, x9, #0x1
	cmp	x10, x9
	b.ne	LBB75_318
	ldr	x25, [x8, #8]
	cbz	x25, LBB75_380
	str	x25, [x16, #4360]
	mov	x0, x25
LBB75_121:
	ldr	x8, [x26, #120]
	stur	x8, [x29, #-80]
	ldr	x8, [x25]
	ldr	x8, [x8, #16]
	ldr	x9, [x25, #56]
	sub	x8, x8, x9
	stur	x8, [x29, #-96]
LBB75_122:
	ldr	x8, [x0, #56]
	sub	x9, x24, x8
	b	LBB75_236
LBB75_123:
	mov	x0, x25
	cmp	x23, x10
	b.lo	LBB75_433
LBB75_124:
	ldr	x8, [x0, #64]
	cbz	x8, LBB75_432
	udiv	x8, x9, x8
	ldr	x1, [x0, #16]
	cmp	x8, x1
	b.hs	LBB75_431
	ldr	x10, [x0, #8]
	ldrb	w8, [x10, x8]
	tbz	w8, #2, LBB75_279
	lsr	x8, x9, #12
	ldr	x1, [x0, #32]
	cmp	x8, x1
	b.hs	LBB75_434
	ldr	x9, [x0, #24]
	ldr	x0, [x9, x8, lsl #3]
	ldr	x8, [x16]
	ldr	x1, [x8, #32]
	cmp	x0, x1
	b.hs	LBB75_438
	ldr	x9, [x8, #24]
	add	x9, x9, x0, lsl #4
	ldr	w10, [x9]
	tbz	w10, #0, LBB75_279
	ldr	x0, [x9, #8]
	ldr	x1, [x8, #56]
	cmp	x0, x1
	b.hs	LBB75_439
	ldur	x9, [x29, #-96]
	add	x20, x9, x23
	ldr	x8, [x8, #48]
	mov	w9, #72
	madd	x8, x0, x9, x8
	ldr	x0, [x8, #56]
	ldr	x8, [x0, #16]!
	cmp	x8, #2
	b.ne	LBB75_250
	; InlineAsm Start
Ltmp4480:
	ldrb	w8, [x20]
	mov	w9, #1
	b	Ltmp4481
Ltmp4482:
	mov	x8, #0
	mov	w9, #0
Ltmp4481:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4480
	.quad	Ltmp4482
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	w9, LBB75_279
	b	LBB75_284
LBB75_134:
	mov	x0, x25
	cmp	x24, x8
	b.lo	LBB75_433
LBB75_135:
	ldr	x8, [x0, #64]
	cbz	x8, LBB75_432
	udiv	x8, x9, x8
	ldr	x1, [x0, #16]
	cmp	x8, x1
	b.hs	LBB75_431
	ldr	x10, [x0, #8]
	ldrb	w8, [x10, x8]
	and	w8, w8, #0xc
	cmp	w8, #4
	b.ne	LBB75_277
	lsr	x8, x9, #12
	ldr	x1, [x0, #32]
	cmp	x8, x1
	b.hs	LBB75_434
	ldr	x9, [x0, #24]
	ldr	x0, [x9, x8, lsl #3]
	ldr	x8, [x16]
	ldr	x1, [x8, #32]
	cmp	x0, x1
	b.hs	LBB75_437
	ldr	x9, [x8, #24]
	add	x9, x9, x0, lsl #4
	ldr	w10, [x9]
	tbz	w10, #0, LBB75_277
	ldr	x0, [x9, #8]
	ldr	x1, [x8, #56]
	cmp	x0, x1
	b.hs	LBB75_440
	ldr	x8, [x8, #48]
	mov	w9, #72
	madd	x26, x0, x9, x8
	ldr	x0, [x26, #56]
	ldr	x8, [x0, #16]!
	cmp	x8, #2
	b.eq	LBB75_277
	ldur	x8, [x29, #-96]
	add	x24, x8, x24
	and	x1, x27, #0xfff
	mov	w2, #1
	bl	__RNvMs0_NtNtCslITGYdeqHog_13trames_memory8parallel4pageNtB5_11MutablePage4lock
	stp	x0, x1, [x29, #-72]
	and	x8, x21, #0xff
	; InlineAsm Start
Ltmp4483:
	strb	w8, [x24]
	mov	w8, #1
	b	Ltmp4484
Ltmp4485:
	mov	w8, #0
Ltmp4484:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4483
	.quad	Ltmp4485
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	w8, LBB75_275
Ltmp268:
	sub	x2, x29, #72
	and	x3, x27, #0xfff
	ldur	x0, [x29, #-104]
	mov	x1, x26
	mov	w4, #1
	bl	__RNvMs0_NtCslITGYdeqHog_13trames_memory8parallelNtB5_4View7changed
Ltmp269:
	ldp	x9, x8, [x29, #-72]
	add	x9, x9, #16
	ldur	x21, [x29, #-88]
	mov	w26, #40
LBB75_146:
	ldxr	x10, [x9]
	bic	x10, x10, x8
	stlxr	w11, x10, [x9]
	cbnz	w11, LBB75_146
	b	LBB75_249
LBB75_147:
	mov	x0, x25
LBB75_148:
	cmp	x12, x10
	b.lo	LBB75_433
	ldr	x8, [x0, #64]
	cbz	x8, LBB75_432
	udiv	x8, x9, x8
	ldr	x1, [x0, #16]
	cmp	x8, x1
	b.hs	LBB75_431
	ldr	x10, [x0, #8]
	ldrb	w8, [x10, x8]
	tbz	w8, #2, LBB75_326
	lsr	x8, x9, #12
	ldr	x1, [x0, #32]
	cmp	x8, x1
	b.hs	LBB75_434
	ldr	x9, [x0, #24]
	ldr	x0, [x9, x8, lsl #3]
	ldr	x8, [x16]
	ldr	x1, [x8, #32]
	cmp	x0, x1
	b.hs	LBB75_438
	ldr	x9, [x8, #24]
	add	x9, x9, x0, lsl #4
	ldr	w10, [x9]
	tbz	w10, #0, LBB75_326
	ldr	x0, [x9, #8]
	ldr	x1, [x8, #56]
	cmp	x0, x1
	b.hs	LBB75_439
	ldur	x9, [x29, #-96]
	add	x10, x9, x12
	ldr	x8, [x8, #48]
	mov	w9, #72
	madd	x8, x0, x9, x8
	ldr	x0, [x8, #56]
	ldr	x8, [x0, #16]!
	cmp	x8, #2
	b.ne	LBB75_254
	; InlineAsm Start
Ltmp4486:
	ldr	w8, [x10]
	mov	w9, #1
	b	Ltmp4487
Ltmp4488:
	mov	x8, #0
	mov	w9, #0
Ltmp4487:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4486
	.quad	Ltmp4488
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	w9, LBB75_326
	b	LBB75_331
LBB75_159:
	mov	x0, x25
LBB75_160:
	cmp	x24, x8
	b.lo	LBB75_433
	ldr	x8, [x0, #64]
	cbz	x8, LBB75_432
	udiv	x8, x9, x8
	ldr	x1, [x0, #16]
	cmp	x8, x1
	b.hs	LBB75_431
	ldr	x10, [x0, #8]
	ldrb	w8, [x10, x8]
	and	w8, w8, #0xc
	cmp	w8, #4
	b.ne	LBB75_323
	lsr	x8, x9, #12
	ldr	x1, [x0, #32]
	cmp	x8, x1
	b.hs	LBB75_434
	ldr	x9, [x0, #24]
	ldr	x0, [x9, x8, lsl #3]
	ldr	x8, [x16]
	ldr	x1, [x8, #32]
	cmp	x0, x1
	b.hs	LBB75_437
	ldr	x9, [x8, #24]
	add	x9, x9, x0, lsl #4
	ldr	w10, [x9]
	tbz	w10, #0, LBB75_323
	ldr	x0, [x9, #8]
	ldr	x1, [x8, #56]
	cmp	x0, x1
	b.hs	LBB75_440
	ldr	x8, [x8, #48]
	mov	w9, #72
	madd	x26, x0, x9, x8
	ldr	x0, [x26, #56]
	ldr	x8, [x0, #16]!
	cmp	x8, #2
	b.eq	LBB75_323
	and	x8, x27, #0xfff
	str	x8, [sp, #16]
	ldur	x8, [x29, #-96]
	add	x24, x8, x24
	and	x1, x27, #0xfff
	mov	w2, #4
	bl	__RNvMs0_NtNtCslITGYdeqHog_13trames_memory8parallel4pageNtB5_11MutablePage4lock
	stp	x0, x1, [x29, #-72]
	mov	w8, w21
	; InlineAsm Start
Ltmp4489:
	str	w8, [x24]
	mov	w8, #1
	b	Ltmp4490
Ltmp4491:
	mov	w8, #0
Ltmp4490:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4489
	.quad	Ltmp4491
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	w8, LBB75_321
Ltmp262:
	sub	x2, x29, #72
	ldur	x0, [x29, #-104]
	mov	x1, x26
	ldr	x3, [sp, #16]
	mov	w4, #4
	bl	__RNvMs0_NtCslITGYdeqHog_13trames_memory8parallelNtB5_4View7changed
Ltmp263:
	ldp	x9, x8, [x29, #-72]
	add	x9, x9, #16
	ldur	x21, [x29, #-88]
	mov	w26, #40
LBB75_172:
	ldxr	x10, [x9]
	bic	x10, x10, x8
	stlxr	w11, x10, [x9]
	cbnz	w11, LBB75_172
	b	LBB75_249
LBB75_173:
	mov	x0, x25
LBB75_174:
	cmp	x12, x10
	b.lo	LBB75_433
	ldr	x8, [x0, #64]
	cbz	x8, LBB75_432
	udiv	x8, x9, x8
	ldr	x1, [x0, #16]
	cmp	x8, x1
	b.hs	LBB75_431
	ldr	x10, [x0, #8]
	ldrb	w8, [x10, x8]
	tbz	w8, #2, LBB75_369
	lsr	x8, x9, #12
	ldr	x1, [x0, #32]
	cmp	x8, x1
	b.hs	LBB75_434
	ldr	x9, [x0, #24]
	ldr	x0, [x9, x8, lsl #3]
	ldr	x8, [x16]
	ldr	x1, [x8, #32]
	cmp	x0, x1
	b.hs	LBB75_438
	ldr	x9, [x8, #24]
	add	x9, x9, x0, lsl #4
	ldr	w10, [x9]
	tbz	w10, #0, LBB75_369
	ldr	x0, [x9, #8]
	ldr	x1, [x8, #56]
	cmp	x0, x1
	b.hs	LBB75_439
	ldur	x9, [x29, #-96]
	add	x10, x9, x12
	ldr	x8, [x8, #48]
	mov	w9, #72
	madd	x8, x0, x9, x8
	ldr	x0, [x8, #56]
	ldr	x8, [x0, #16]!
	cmp	x8, #2
	b.ne	LBB75_258
	; InlineAsm Start
Ltmp4492:
	ldr	x8, [x10]
	mov	w9, #1
	b	Ltmp4493
Ltmp4494:
	mov	x8, #0
	mov	w9, #0
Ltmp4493:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4492
	.quad	Ltmp4494
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbnz	w9, LBB75_374
	b	LBB75_369
LBB75_184:
	mov	x0, x25
LBB75_185:
	cmp	x24, x8
	b.lo	LBB75_433
	ldr	x8, [x0, #64]
	cbz	x8, LBB75_432
	udiv	x8, x9, x8
	ldr	x1, [x0, #16]
	cmp	x8, x1
	b.hs	LBB75_431
	ldr	x10, [x0, #8]
	ldrb	w8, [x10, x8]
	and	w8, w8, #0xc
	cmp	w8, #4
	b.ne	LBB75_340
	lsr	x8, x9, #12
	ldr	x1, [x0, #32]
	cmp	x8, x1
	b.hs	LBB75_434
	ldr	x9, [x0, #24]
	ldr	x0, [x9, x8, lsl #3]
	ldr	x8, [x16]
	ldr	x1, [x8, #32]
	cmp	x0, x1
	b.hs	LBB75_437
	ldr	x9, [x8, #24]
	add	x9, x9, x0, lsl #4
	ldr	w10, [x9]
	tbz	w10, #0, LBB75_340
	ldr	x0, [x9, #8]
	ldr	x1, [x8, #56]
	cmp	x0, x1
	b.hs	LBB75_440
	ldr	x8, [x8, #48]
	mov	w9, #72
	madd	x26, x0, x9, x8
	ldr	x0, [x26, #56]
	ldr	x8, [x0, #16]!
	cmp	x8, #2
	b.eq	LBB75_340
	and	x8, x27, #0xfff
	str	x8, [sp, #16]
	ldur	x8, [x29, #-96]
	add	x24, x8, x24
	and	x1, x27, #0xfff
	mov	w2, #8
	bl	__RNvMs0_NtNtCslITGYdeqHog_13trames_memory8parallel4pageNtB5_11MutablePage4lock
	stp	x0, x1, [x29, #-72]
	; InlineAsm Start
Ltmp4495:
	str	x21, [x24]
	mov	w8, #1
	b	Ltmp4496
Ltmp4497:
	mov	w8, #0
Ltmp4496:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4495
	.quad	Ltmp4497
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	w8, LBB75_338
Ltmp259:
	sub	x2, x29, #72
	ldur	x0, [x29, #-104]
	mov	x1, x26
	ldr	x3, [sp, #16]
	mov	w4, #8
	bl	__RNvMs0_NtCslITGYdeqHog_13trames_memory8parallelNtB5_4View7changed
Ltmp260:
	ldp	x9, x8, [x29, #-72]
	add	x9, x9, #16
	ldur	x21, [x29, #-88]
	mov	w26, #40
LBB75_197:
	ldxr	x10, [x9]
	bic	x10, x10, x8
	stlxr	w11, x10, [x9]
	cbnz	w11, LBB75_197
	b	LBB75_249
LBB75_198:
	mov	x0, x25
LBB75_199:
	cmp	x12, x10
	b.lo	LBB75_433
	ldr	x8, [x0, #64]
	cbz	x8, LBB75_432
	udiv	x8, x9, x8
	ldr	x1, [x0, #16]
	cmp	x8, x1
	b.hs	LBB75_431
	ldr	x10, [x0, #8]
	ldrb	w8, [x10, x8]
	tbz	w8, #2, LBB75_291
	lsr	x8, x9, #12
	ldr	x1, [x0, #32]
	cmp	x8, x1
	b.hs	LBB75_434
	ldr	x9, [x0, #24]
	ldr	x0, [x9, x8, lsl #3]
	ldr	x8, [x16]
	ldr	x1, [x8, #32]
	cmp	x0, x1
	b.hs	LBB75_438
	ldr	x9, [x8, #24]
	add	x9, x9, x0, lsl #4
	ldr	w10, [x9]
	tbz	w10, #0, LBB75_291
	ldr	x0, [x9, #8]
	ldr	x1, [x8, #56]
	cmp	x0, x1
	b.hs	LBB75_439
	ldr	x8, [x8, #48]
	mov	w9, #72
	madd	x8, x0, x9, x8
	ldr	x0, [x8, #56]
	ldr	x8, [x0, #16]!
	cmp	x8, #2
	b.ne	LBB75_262
	mov	x0, #0
	b	LBB75_263
LBB75_209:
	mov	x0, x25
LBB75_210:
	cmp	x11, x8
	b.lo	LBB75_433
	ldr	x8, [x0, #64]
	cbz	x8, LBB75_432
	udiv	x8, x9, x8
	ldr	x1, [x0, #16]
	cmp	x8, x1
	b.hs	LBB75_431
	ldr	x10, [x0, #8]
	ldrb	w8, [x10, x8]
	and	w8, w8, #0xc
	cmp	w8, #4
	b.ne	LBB75_377
	lsr	x8, x9, #12
	ldr	x1, [x0, #32]
	cmp	x8, x1
	b.hs	LBB75_434
	ldr	x9, [x0, #24]
	ldr	x0, [x9, x8, lsl #3]
	ldr	x8, [x16]
	ldr	x1, [x8, #32]
	cmp	x0, x1
	b.hs	LBB75_437
	ldr	x9, [x8, #24]
	add	x9, x9, x0, lsl #4
	ldr	w10, [x9]
	tbz	w10, #0, LBB75_377
	ldr	x0, [x9, #8]
	ldr	x1, [x8, #56]
	cmp	x0, x1
	b.hs	LBB75_440
	ldr	x8, [x8, #48]
	mov	w9, #72
	madd	x26, x0, x9, x8
	ldr	x0, [x26, #56]
	ldr	x8, [x0, #16]!
	cmp	x8, #2
	b.eq	LBB75_377
	and	x8, x27, #0xfff
	str	x8, [sp, #8]
	fmov	d0, x21
	mov.d	v0[1], x24
	str	q0, [sp, #16]
	ldur	x8, [x29, #-96]
	add	x21, x8, x11
	and	x1, x27, #0xfff
	mov	w2, #16
	bl	__RNvMs0_NtNtCslITGYdeqHog_13trames_memory8parallel4pageNtB5_11MutablePage4lock
	stp	x0, x1, [x29, #-72]
	ldr	q0, [sp, #16]
	; InlineAsm Start
Ltmp4498:
	str	q0, [x21]
	mov	w8, #1
	b	Ltmp4499
Ltmp4500:
	mov	w8, #0
Ltmp4499:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4498
	.quad	Ltmp4500
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	w8, LBB75_375
Ltmp256:
	sub	x2, x29, #72
	ldur	x0, [x29, #-104]
	mov	x1, x26
	ldr	x3, [sp, #8]
	mov	w4, #16
	bl	__RNvMs0_NtCslITGYdeqHog_13trames_memory8parallelNtB5_4View7changed
Ltmp257:
	ldp	x9, x8, [x29, #-72]
	add	x9, x9, #16
	ldur	x21, [x29, #-88]
	mov	w26, #40
LBB75_222:
	ldxr	x10, [x9]
	bic	x10, x10, x8
	stlxr	w11, x10, [x9]
	cbnz	w11, LBB75_222
	b	LBB75_249
LBB75_223:
	mov	x0, x25
LBB75_224:
	cmp	x12, x10
	b.lo	LBB75_433
	ldr	x8, [x0, #64]
	cbz	x8, LBB75_432
	udiv	x8, x9, x8
	ldr	x1, [x0, #16]
	cmp	x8, x1
	b.hs	LBB75_431
	ldr	x10, [x0, #8]
	ldrb	w8, [x10, x8]
	tbz	w8, #2, LBB75_382
	lsr	x8, x9, #12
	ldr	x1, [x0, #32]
	cmp	x8, x1
	b.hs	LBB75_434
	ldr	x9, [x0, #24]
	ldr	x0, [x9, x8, lsl #3]
	ldr	x8, [x16]
	ldr	x1, [x8, #32]
	cmp	x0, x1
	b.hs	LBB75_438
	ldr	x9, [x8, #24]
	add	x9, x9, x0, lsl #4
	ldr	w10, [x9]
	tbz	w10, #0, LBB75_382
	ldr	x0, [x9, #8]
	ldr	x1, [x8, #56]
	cmp	x0, x1
	b.hs	LBB75_439
	ldur	x9, [x29, #-96]
	add	x10, x9, x12
	ldr	x8, [x8, #48]
	mov	w9, #72
	madd	x8, x0, x9, x8
	ldr	x0, [x8, #56]
	ldr	x8, [x0, #16]!
	cmp	x8, #2
	b.ne	LBB75_265
	; InlineAsm Start
Ltmp4501:
	ldrh	w8, [x10]
	mov	w9, #1
	b	Ltmp4502
Ltmp4503:
	mov	x8, #0
	mov	w9, #0
Ltmp4502:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4501
	.quad	Ltmp4503
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	w9, LBB75_382
	b	LBB75_387
LBB75_235:
	mov	x0, x25
LBB75_236:
	cmp	x24, x8
	b.lo	LBB75_433
	ldr	x8, [x0, #64]
	cbz	x8, LBB75_432
	udiv	x8, x9, x8
	ldr	x1, [x0, #16]
	cmp	x8, x1
	b.hs	LBB75_431
	ldr	x10, [x0, #8]
	ldrb	w8, [x10, x8]
	and	w8, w8, #0xc
	cmp	w8, #4
	b.ne	LBB75_380
	lsr	x8, x9, #12
	ldr	x1, [x0, #32]
	cmp	x8, x1
	b.hs	LBB75_434
	ldr	x9, [x0, #24]
	ldr	x0, [x9, x8, lsl #3]
	ldr	x8, [x16]
	ldr	x1, [x8, #32]
	cmp	x0, x1
	b.hs	LBB75_437
	ldr	x9, [x8, #24]
	add	x9, x9, x0, lsl #4
	ldr	w10, [x9]
	tbz	w10, #0, LBB75_380
	ldr	x0, [x9, #8]
	ldr	x1, [x8, #56]
	cmp	x0, x1
	b.hs	LBB75_440
	ldr	x8, [x8, #48]
	mov	w9, #72
	madd	x26, x0, x9, x8
	ldr	x0, [x26, #56]
	ldr	x8, [x0, #16]!
	cmp	x8, #2
	b.eq	LBB75_380
	and	x8, x27, #0xfff
	str	x8, [sp, #16]
	ldur	x8, [x29, #-96]
	add	x24, x8, x24
	and	x1, x27, #0xfff
	mov	w2, #2
	bl	__RNvMs0_NtNtCslITGYdeqHog_13trames_memory8parallel4pageNtB5_11MutablePage4lock
	stp	x0, x1, [x29, #-72]
	and	x8, x21, #0xffff
	; InlineAsm Start
Ltmp4504:
	strh	w8, [x24]
	mov	w8, #1
	b	Ltmp4505
Ltmp4506:
	mov	w8, #0
Ltmp4505:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4504
	.quad	Ltmp4506
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	w8, LBB75_378
Ltmp265:
	sub	x2, x29, #72
	ldur	x0, [x29, #-104]
	mov	x1, x26
	ldr	x3, [sp, #16]
	mov	w4, #2
	bl	__RNvMs0_NtCslITGYdeqHog_13trames_memory8parallelNtB5_4View7changed
Ltmp266:
	ldp	x9, x8, [x29, #-72]
	add	x9, x9, #16
	ldur	x21, [x29, #-88]
	mov	w26, #40
LBB75_248:
	ldxr	x10, [x9]
	bic	x10, x10, x8
	stlxr	w11, x10, [x9]
	cbnz	w11, LBB75_248
LBB75_249:
	mov	w8, #255
	sturb	w8, [x29, #-40]
	ldur	x16, [x29, #-104]
	b	LBB75_343
LBB75_250:
	and	x1, x27, #0xfff
	mov	w2, #1
	bl	__RNvMs0_NtNtCslITGYdeqHog_13trames_memory8parallel4pageNtB5_11MutablePage4lock
	; InlineAsm Start
Ltmp4507:
	ldrb	w8, [x20]
	mov	w9, #1
	b	Ltmp4508
Ltmp4509:
	mov	x8, #0
	mov	w9, #0
Ltmp4508:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4507
	.quad	Ltmp4509
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	x0, LBB75_278
	add	x10, x0, #16
	ldur	x16, [x29, #-104]
LBB75_252:
	ldxr	x11, [x10]
	bic	x11, x11, x1
	stlxr	w12, x11, [x10]
	cbnz	w12, LBB75_252
	cbnz	w9, LBB75_284
	b	LBB75_279
LBB75_254:
	and	x1, x27, #0xfff
	mov	w2, #4
	str	x10, [sp, #8]
	bl	__RNvMs0_NtNtCslITGYdeqHog_13trames_memory8parallel4pageNtB5_11MutablePage4lock
	ldr	x8, [sp, #8]
	; InlineAsm Start
Ltmp4510:
	ldr	w8, [x8]
	mov	w9, #1
	b	Ltmp4511
Ltmp4512:
	mov	x8, #0
	mov	w9, #0
Ltmp4511:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4510
	.quad	Ltmp4512
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	x0, LBB75_325
	add	x10, x0, #16
	ldur	x16, [x29, #-104]
LBB75_256:
	ldxr	x11, [x10]
	bic	x11, x11, x1
	stlxr	w12, x11, [x10]
	cbnz	w12, LBB75_256
	cbnz	w9, LBB75_331
	b	LBB75_326
LBB75_258:
	and	x1, x27, #0xfff
	mov	w2, #8
	str	x10, [sp, #8]
	bl	__RNvMs0_NtNtCslITGYdeqHog_13trames_memory8parallel4pageNtB5_11MutablePage4lock
	ldr	x8, [sp, #8]
	; InlineAsm Start
Ltmp4513:
	ldr	x8, [x8]
	mov	w9, #1
	b	Ltmp4514
Ltmp4515:
	mov	x8, #0
	mov	w9, #0
Ltmp4514:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4513
	.quad	Ltmp4515
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	x0, LBB75_368
	add	x10, x0, #16
	ldur	x16, [x29, #-104]
LBB75_260:
	ldxr	x11, [x10]
	bic	x11, x11, x1
	stlxr	w12, x11, [x10]
	cbnz	w12, LBB75_260
	cbnz	w9, LBB75_374
	b	LBB75_369
LBB75_262:
	and	x1, x27, #0xfff
	mov	w2, #16
	bl	__RNvMs0_NtNtCslITGYdeqHog_13trames_memory8parallel4pageNtB5_11MutablePage4lock
	and	x12, x27, #0xffffffffffffff
	ldur	x16, [x29, #-104]
	mvn	x11, x1
LBB75_263:
	ldur	x8, [x29, #-96]
	add	x8, x8, x12
	; InlineAsm Start
Ltmp4516:
	ldr	q0, [x8]
	mov	w12, #1
	b	Ltmp4517
Ltmp4518:
	movi.16b	v0, #0
	mov	w12, #0
Ltmp4517:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4516
	.quad	Ltmp4518
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	w12, LBB75_286
	fmov	x8, d0
	mov.d	x9, v0[1]
	extr	x10, x9, x8, #56
	lsr	x9, x9, #56
	b	LBB75_287
LBB75_265:
	and	x1, x27, #0xfff
	mov	w2, #2
	str	x10, [sp, #8]
	bl	__RNvMs0_NtNtCslITGYdeqHog_13trames_memory8parallel4pageNtB5_11MutablePage4lock
	ldr	x8, [sp, #8]
	; InlineAsm Start
Ltmp4519:
	ldrh	w8, [x8]
	mov	w9, #1
	b	Ltmp4520
Ltmp4521:
	mov	x8, #0
	mov	w9, #0
Ltmp4520:
	.section	__DATA,__trames_mmu,regular,no_dead_strip
	.p2align	3, 0x0
	.quad	Ltmp4519
	.quad	Ltmp4521
	.section	__TEXT,__text,regular,pure_instructions

	; InlineAsm End
	cbz	x0, LBB75_381
	add	x10, x0, #16
	ldur	x16, [x29, #-104]
LBB75_267:
	ldxr	x11, [x10]
	bic	x11, x11, x1
	stlxr	w12, x11, [x10]
	cbnz	w12, LBB75_267
	cbnz	w9, LBB75_387
	b	LBB75_382
LBB75_269:
	add	x0, x20, #88
	and	x2, x27, #0xffffffff000000
	ldr	x1, [sp, #80]
	bl	__RNvMs2_NtNtCslITGYdeqHog_13trames_memory3mmu6workerNtB5_6Frozen12reached_slow
	ldur	x16, [x29, #-104]
	cbz	x0, LBB75_279
	ldr	x25, [x16, #4360]
	cbz	x25, LBB75_388
	ldr	x8, [x20, #120]
	stur	x8, [x29, #-80]
	b	LBB75_31
LBB75_272:
	add	x0, x26, #88
	and	x2, x27, #0xffffffff000000
	ldr	x1, [sp, #80]
	bl	__RNvMs2_NtNtCslITGYdeqHog_13trames_memory3mmu6workerNtB5_6Frozen12reached_slow
	ldur	x16, [x29, #-104]
	cbz	x0, LBB75_277
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_93
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	ldr	x8, [x0, #56]
	sub	x9, x24, x8
	cmp	x24, x8
	b.hs	LBB75_135
	b	LBB75_433
LBB75_275:
	add	x8, x0, #16
	ldur	x16, [x29, #-104]
LBB75_276:
	ldxr	x9, [x8]
	bic	x9, x9, x1
	stlxr	w10, x9, [x8]
	cbnz	w10, LBB75_276
LBB75_277:
	sub	x8, x29, #40
	sub	x2, x29, #24
	mov	x0, x16
	mov	x1, x27
	mov	w3, #1
	bl	__RNvXs1_NtCslITGYdeqHog_13trames_memory8parallelNtB5_4ViewNtNtB7_10cpu_memory9CpuMemory5write
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_324
	b	LBB75_341
LBB75_278:
	ldur	x16, [x29, #-104]
	cbnz	w9, LBB75_284
LBB75_279:
	sturb	wzr, [x29, #-48]
	sub	x0, x29, #24
	sub	x3, x29, #48
	mov	x1, x16
	mov	x2, x27
	mov	w4, #1
	bl	__RINvMs0_NtCslITGYdeqHog_13trames_memory8parallelNtB6_4View12read_checkedNtB8_11MemoryErrorNCNvXs1_B6_BK_NtNtB8_10cpu_memory9CpuMemory4read0ECsl4LB0XcnsVR_11trames_core
	ldurb	w9, [x29, #-24]
	cmp	w9, #255
	b.eq	LBB75_282
	ldurb	w8, [x29, #-23]
	ldr	x11, [sp, #32]
	ldr	x10, [x11]
	stur	x10, [x29, #-40]
	ldur	x10, [x11, #6]
	stur	x10, [x29, #-34]
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	ldp	x20, x23, [sp]
	cbz	x25, LBB75_283
LBB75_281:
	ldr	x10, [x16]
	ldr	x10, [x10, #120]
	stur	x10, [x29, #-80]
	ldr	x10, [x25]
	ldr	x10, [x10, #16]
	ldr	x11, [x25, #56]
	sub	x10, x10, x11
	stur	x10, [x29, #-96]
	cmp	w9, #255
	b.eq	LBB75_284
	b	LBB75_426
LBB75_282:
	ldurb	w8, [x29, #-48]
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	ldp	x20, x23, [sp]
	cbnz	x25, LBB75_281
LBB75_283:
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	cmp	w9, #255
	b.ne	LBB75_426
LBB75_284:
	mov	x9, #0
	and	x8, x8, #0xff
	ldp	x20, x23, [sp]
	tbz	w21, #0, LBB75_332
LBB75_285:
	ldr	x10, [sp, #88]
	ldr	x11, [sp, #16]
	add	x10, x10, x11, lsl #4
	stp	x8, x9, [x10]
	ldur	x21, [x29, #-88]
	mov	w26, #40
	b	LBB75_344
LBB75_286:
	mov	x10, #0
	mov	x9, #0
LBB75_287:
	cbz	x0, LBB75_290
	add	x13, x0, #16
LBB75_289:
	ldxr	x14, [x13]
	and	x14, x14, x11
	stlxr	w15, x14, [x13]
	cbnz	w15, LBB75_289
LBB75_290:
	cbnz	w12, LBB75_296
LBB75_291:
	stp	xzr, xzr, [x29, #-40]
	sub	x0, x29, #24
	sub	x3, x29, #40
	mov	x1, x16
	mov	x2, x27
	mov	w4, #16
	bl	__RINvMs0_NtCslITGYdeqHog_13trames_memory8parallelNtB6_4View12read_checkedNtB8_11MemoryErrorNCNvXs1_B6_BK_NtNtB8_10cpu_memory9CpuMemory4read0ECsl4LB0XcnsVR_11trames_core
	ldurb	w11, [x29, #-24]
	cmp	w11, #255
	b.eq	LBB75_294
	ldurb	w9, [x29, #-16]
	ldur	x10, [x29, #-24]
	sub	x12, x29, #24
	ldur	w8, [x12, #9]
	stur	w8, [x29, #-48]
	ldur	w8, [x12, #12]
	stur	w8, [x29, #-45]
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbz	x25, LBB75_295
LBB75_293:
	ldr	x12, [x16]
	ldr	x12, [x12, #120]
	stur	x12, [x29, #-80]
	ldr	x12, [x25]
	ldr	x12, [x12, #16]
	ldr	x13, [x25, #56]
	sub	x12, x12, x13
	stur	x12, [x29, #-96]
	cmp	w11, #255
	b.eq	LBB75_296
	b	LBB75_422
LBB75_294:
	ldp	x8, x9, [x29, #-40]
	extr	x10, x9, x8, #56
	lsr	x9, x9, #56
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_293
LBB75_295:
	stur	xzr, [x29, #-96]
	stur	xzr, [x29, #-80]
	cmp	w11, #255
	b.ne	LBB75_422
LBB75_296:
	bfi	x8, x10, #56, #8
	extr	x9, x9, x10, #8
	tbnz	w21, #0, LBB75_285
	b	LBB75_332
LBB75_297:
	stur	x13, [x29, #-80]
	add	x0, x13, #88
	and	x2, x27, #0xffffffff000000
	ldr	x1, [sp, #80]
	bl	__RNvMs2_NtNtCslITGYdeqHog_13trames_memory3mmu6workerNtB5_6Frozen12reached_slow
	and	x12, x27, #0xffffffffffffff
	ldur	x16, [x29, #-104]
	cbz	x0, LBB75_326
	ldr	x25, [x16, #4360]
	cbz	x25, LBB75_389
	ldur	x8, [x29, #-80]
	ldr	x8, [x8, #120]
	stur	x8, [x29, #-80]
	b	LBB75_39
LBB75_300:
	add	x0, x26, #88
	and	x2, x27, #0xffffffff000000
	ldr	x1, [sp, #80]
	bl	__RNvMs2_NtNtCslITGYdeqHog_13trames_memory3mmu6workerNtB5_6Frozen12reached_slow
	ldur	x16, [x29, #-104]
	cbz	x0, LBB75_323
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_101
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	b	LBB75_102
LBB75_303:
	stur	x13, [x29, #-80]
	add	x0, x13, #88
	and	x2, x27, #0xffffffff000000
	ldr	x1, [sp, #80]
	bl	__RNvMs2_NtNtCslITGYdeqHog_13trames_memory3mmu6workerNtB5_6Frozen12reached_slow
	and	x12, x27, #0xffffffffffffff
	ldur	x16, [x29, #-104]
	cbz	x0, LBB75_369
	ldr	x25, [x16, #4360]
	cbz	x25, LBB75_390
	ldur	x8, [x29, #-80]
	ldr	x8, [x8, #120]
	stur	x8, [x29, #-80]
	b	LBB75_16
LBB75_306:
	add	x0, x26, #88
	and	x2, x27, #0xffffffff000000
	ldr	x1, [sp, #80]
	bl	__RNvMs2_NtNtCslITGYdeqHog_13trames_memory3mmu6workerNtB5_6Frozen12reached_slow
	ldur	x16, [x29, #-104]
	cbz	x0, LBB75_340
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_84
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	b	LBB75_85
LBB75_309:
	stur	x13, [x29, #-80]
	add	x0, x13, #88
	and	x2, x27, #0xffffffff000000
	ldr	x1, [sp, #80]
	bl	__RNvMs2_NtNtCslITGYdeqHog_13trames_memory3mmu6workerNtB5_6Frozen12reached_slow
	and	x12, x27, #0xffffffffffffff
	ldur	x16, [x29, #-104]
	cbz	x0, LBB75_291
	ldr	x25, [x16, #4360]
	cbz	x25, LBB75_391
	ldur	x8, [x29, #-80]
	ldr	x8, [x8, #120]
	stur	x8, [x29, #-80]
	b	LBB75_49
LBB75_312:
	add	x0, x26, #88
	and	x2, x27, #0xffffffff000000
	ldr	x1, [sp, #80]
	bl	__RNvMs2_NtNtCslITGYdeqHog_13trames_memory3mmu6workerNtB5_6Frozen12reached_slow
	and	x11, x27, #0xffffffffffffff
	ldur	x16, [x29, #-104]
	cbz	x0, LBB75_377
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_111
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	b	LBB75_112
LBB75_315:
	stur	x13, [x29, #-80]
	add	x0, x13, #88
	and	x2, x27, #0xffffffff000000
	ldr	x1, [sp, #80]
	bl	__RNvMs2_NtNtCslITGYdeqHog_13trames_memory3mmu6workerNtB5_6Frozen12reached_slow
	and	x12, x27, #0xffffffffffffff
	ldur	x16, [x29, #-104]
	cbz	x0, LBB75_382
	ldr	x25, [x16, #4360]
	cbz	x25, LBB75_392
	ldur	x8, [x29, #-80]
	ldr	x8, [x8, #120]
	stur	x8, [x29, #-80]
	b	LBB75_59
LBB75_318:
	add	x0, x26, #88
	and	x2, x27, #0xffffffff000000
	ldr	x1, [sp, #80]
	bl	__RNvMs2_NtNtCslITGYdeqHog_13trames_memory3mmu6workerNtB5_6Frozen12reached_slow
	ldur	x16, [x29, #-104]
	cbz	x0, LBB75_380
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_121
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	b	LBB75_122
LBB75_321:
	add	x8, x0, #16
	ldur	x16, [x29, #-104]
LBB75_322:
	ldxr	x9, [x8]
	bic	x9, x9, x1
	stlxr	w10, x9, [x8]
	cbnz	w10, LBB75_322
LBB75_323:
	sub	x8, x29, #40
	sub	x2, x29, #24
	mov	x0, x16
	mov	x1, x27
	mov	w3, #4
	bl	__RNvXs1_NtCslITGYdeqHog_13trames_memory8parallelNtB5_4ViewNtNtB7_10cpu_memory9CpuMemory5write
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbz	x25, LBB75_341
LBB75_324:
	ldr	x8, [x16]
	ldr	x8, [x8, #120]
	stur	x8, [x29, #-80]
	ldr	x8, [x25]
	ldr	x8, [x8, #16]
	ldr	x9, [x25, #56]
	sub	x8, x8, x9
	stur	x8, [x29, #-96]
	b	LBB75_342
LBB75_325:
	ldur	x16, [x29, #-104]
	cbnz	w9, LBB75_331
LBB75_326:
	stur	wzr, [x29, #-48]
	sub	x0, x29, #24
	sub	x3, x29, #48
	mov	x1, x16
	mov	x2, x27
	mov	w4, #4
	bl	__RINvMs0_NtCslITGYdeqHog_13trames_memory8parallelNtB6_4View12read_checkedNtB8_11MemoryErrorNCNvXs1_B6_BK_NtNtB8_10cpu_memory9CpuMemory4read0ECsl4LB0XcnsVR_11trames_core
	ldurb	w9, [x29, #-24]
	cmp	w9, #255
	b.eq	LBB75_329
	ldur	w8, [x29, #-23]
	ldr	x11, [sp, #48]
	ldr	x10, [x11]
	stur	x10, [x29, #-40]
	ldur	w10, [x11, #7]
	stur	w10, [x29, #-33]
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbz	x25, LBB75_330
LBB75_328:
	ldr	x10, [x16]
	ldr	x10, [x10, #120]
	stur	x10, [x29, #-80]
	ldr	x10, [x25]
	ldr	x10, [x10, #16]
	ldr	x11, [x25, #56]
	sub	x10, x10, x11
	stur	x10, [x29, #-96]
	cmp	w9, #255
	b.eq	LBB75_331
	b	LBB75_424
LBB75_329:
	ldur	w8, [x29, #-48]
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_328
LBB75_330:
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	cmp	w9, #255
	b.ne	LBB75_424
LBB75_331:
	mov	x9, #0
	mov	w8, w8
	tbnz	w21, #0, LBB75_285
LBB75_332:
	cmp	w24, #1
	ldr	x10, [sp, #16]
	b.ls	LBB75_336
	cmp	w26, #8
	b.hi	LBB75_436
	mov	w9, #64
	sub	w9, w9, w26, lsl #3
	cmp	w9, #64
	b.hs	LBB75_435
	lsl	x8, x8, x9
	asr	x8, x8, x9
LBB75_336:
	cmp	w10, #31
	ldur	x21, [x29, #-88]
	mov	w26, #40
	b.eq	LBB75_344
	mov	w9, w8
	cmp	w24, #3
	csel	x8, x9, x8, eq
	ldr	x9, [sp, #120]
	str	x8, [x9, x10, lsl #3]
	b	LBB75_344
LBB75_338:
	add	x8, x0, #16
	ldur	x16, [x29, #-104]
LBB75_339:
	ldxr	x9, [x8]
	bic	x9, x9, x1
	stlxr	w10, x9, [x8]
	cbnz	w10, LBB75_339
LBB75_340:
	sub	x8, x29, #40
	sub	x2, x29, #24
	mov	x0, x16
	mov	x1, x27
	mov	w3, #8
	bl	__RNvXs1_NtCslITGYdeqHog_13trames_memory8parallelNtB5_4ViewNtNtB7_10cpu_memory9CpuMemory5write
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_324
LBB75_341:
	stur	xzr, [x29, #-96]
	stur	xzr, [x29, #-80]
LBB75_342:
	ldur	x21, [x29, #-88]
	mov	w26, #40
LBB75_343:
	ldurb	w8, [x29, #-40]
	cmp	w8, #255
	b.ne	LBB75_421
LBB75_344:
	ldr	w8, [sp, #108]
	tbz	w8, #0, LBB75_349
	ldr	x8, [sp, #72]
	cmp	w8, #31
	b.ne	LBB75_347
	ldr	x8, [sp, #112]
	b	LBB75_348
LBB75_347:
	ldr	x8, [sp, #64]
LBB75_348:
	ldr	x9, [sp, #56]
	str	x9, [x8]
LBB75_349:
	add	x19, x19, #4
	str	x19, [x21, #952]
	add	x23, x23, #1
	ldrb	w8, [x22, #8]
	cmp	w8, #23
	b.gt	LBB75_353
	cmp	w8, #22
	b.eq	LBB75_355
	cmp	w8, #23
	b.ne	LBB75_363
	ldrb	w8, [x22, #11]
	tbz	w8, #0, LBB75_359
	b	LBB75_363
LBB75_353:
	cmp	w8, #24
	b.eq	LBB75_358
	cmp	w8, #25
	b.eq	LBB75_359
	b	LBB75_363
LBB75_355:
	ldrb	w8, [x22, #10]
	cbz	w8, LBB75_359
	cmp	w8, #2
	b.ne	LBB75_363
	ldrb	w8, [x22, #11]
	tbnz	w8, #0, LBB75_359
	b	LBB75_363
LBB75_358:
	ldrb	w8, [x22, #9]
	tbz	w8, #0, LBB75_363
LBB75_359:
	sub	x8, x29, #72
	mov	x0, x16
	bl	__RNvXs1_NtCslITGYdeqHog_13trames_memory8parallelNtB5_4ViewNtNtB7_10cpu_memory9CpuMemory20instruction_identity
	ldur	x8, [x29, #-72]
	ldr	x9, [x28, #40]
	cmp	x8, x9
	b.ne	LBB75_393
	ldur	x8, [x29, #-64]
	ldr	x9, [x28, #48]
	cmp	x8, x9
	b.ne	LBB75_393
	ldur	x8, [x29, #-56]
	ldr	x9, [x28, #56]
	cmp	x8, x9
	b.ne	LBB75_393
	ldr	x19, [x21, #952]
	ldur	x16, [x29, #-104]
LBB75_363:
	ldur	x8, [x29, #-112]
	cmp	x23, x8
	b.hs	LBB75_394
	and	x8, x19, #0xfffffffffffff000
	ldr	x9, [x28, #8]
	cmp	x8, x9
	b.ne	LBB75_393
	tst	x19, #0x3
	b.ne	LBB75_395
	ubfx	x22, x19, #2, #10
	ldr	x8, [x28]
	umaddl	x9, w22, w26, x8
	ldr	x10, [x9]
	ldr	x11, [x28, #64]
	cmp	x10, x11
	b.ne	LBB75_430
	ldrb	w9, [x9, #8]
Lloh936:
	adrp	x12, LJTI75_0@PAGE
Lloh937:
	add	x12, x12, LJTI75_0@PAGEOFF
Ltmp4522:
	adr	x10, Ltmp4522
	ldrsw	x11, [x12, x9, lsl #2]
	add	x10, x10, x11
	br	x10
LBB75_368:
	ldur	x16, [x29, #-104]
	cbnz	w9, LBB75_374
LBB75_369:
	stur	xzr, [x29, #-40]
	sub	x0, x29, #24
	sub	x3, x29, #40
	mov	x1, x16
	mov	x2, x27
	mov	w4, #8
	bl	__RINvMs0_NtCslITGYdeqHog_13trames_memory8parallelNtB6_4View12read_checkedNtB8_11MemoryErrorNCNvXs1_B6_BK_NtNtB8_10cpu_memory9CpuMemory4read0ECsl4LB0XcnsVR_11trames_core
	ldurb	w9, [x29, #-24]
	cmp	w9, #255
	b.eq	LBB75_372
	ldur	x8, [x29, #-23]
	sub	x11, x29, #24
	ldur	w10, [x11, #9]
	stur	w10, [x29, #-48]
	ldur	w10, [x11, #12]
	stur	w10, [x29, #-45]
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbz	x25, LBB75_373
LBB75_371:
	ldr	x10, [x16]
	ldr	x10, [x10, #120]
	stur	x10, [x29, #-80]
	ldr	x10, [x25]
	ldr	x10, [x10, #16]
	ldr	x11, [x25, #56]
	sub	x10, x10, x11
	stur	x10, [x29, #-96]
	cmp	w9, #255
	b.eq	LBB75_374
	b	LBB75_425
LBB75_372:
	ldur	x8, [x29, #-40]
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_371
LBB75_373:
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	cmp	w9, #255
	b.ne	LBB75_425
LBB75_374:
	mov	x9, #0
	tbz	w21, #0, LBB75_332
	b	LBB75_285
LBB75_375:
	add	x8, x0, #16
	ldur	x16, [x29, #-104]
LBB75_376:
	ldxr	x9, [x8]
	bic	x9, x9, x1
	stlxr	w10, x9, [x8]
	cbnz	w10, LBB75_376
LBB75_377:
	sub	x8, x29, #40
	sub	x2, x29, #24
	mov	x0, x16
	mov	x1, x27
	mov	w3, #16
	bl	__RNvXs1_NtCslITGYdeqHog_13trames_memory8parallelNtB5_4ViewNtNtB7_10cpu_memory9CpuMemory5write
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_324
	b	LBB75_341
LBB75_378:
	add	x8, x0, #16
	ldur	x16, [x29, #-104]
LBB75_379:
	ldxr	x9, [x8]
	bic	x9, x9, x1
	stlxr	w10, x9, [x8]
	cbnz	w10, LBB75_379
LBB75_380:
	sub	x8, x29, #40
	sub	x2, x29, #24
	mov	x0, x16
	mov	x1, x27
	mov	w3, #2
	bl	__RNvXs1_NtCslITGYdeqHog_13trames_memory8parallelNtB5_4ViewNtNtB7_10cpu_memory9CpuMemory5write
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_324
	b	LBB75_341
LBB75_381:
	ldur	x16, [x29, #-104]
	cbnz	w9, LBB75_387
LBB75_382:
	sturh	wzr, [x29, #-48]
	sub	x0, x29, #24
	sub	x3, x29, #48
	mov	x1, x16
	mov	x2, x27
	mov	w4, #2
	bl	__RINvMs0_NtCslITGYdeqHog_13trames_memory8parallelNtB6_4View12read_checkedNtB8_11MemoryErrorNCNvXs1_B6_BK_NtNtB8_10cpu_memory9CpuMemory4read0ECsl4LB0XcnsVR_11trames_core
	ldurb	w9, [x29, #-24]
	cmp	w9, #255
	b.eq	LBB75_385
	ldurh	w8, [x29, #-23]
	ldr	x11, [sp, #40]
	ldr	x10, [x11]
	stur	x10, [x29, #-40]
	ldur	x10, [x11, #5]
	stur	x10, [x29, #-35]
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbz	x25, LBB75_386
LBB75_384:
	ldr	x10, [x16]
	ldr	x10, [x10, #120]
	stur	x10, [x29, #-80]
	ldr	x10, [x25]
	ldr	x10, [x10, #16]
	ldr	x11, [x25, #56]
	sub	x10, x10, x11
	stur	x10, [x29, #-96]
	cmp	w9, #255
	b.eq	LBB75_387
	b	LBB75_423
LBB75_385:
	ldurh	w8, [x29, #-48]
	ldur	x16, [x29, #-104]
	ldr	x25, [x16, #4360]
	cbnz	x25, LBB75_384
LBB75_386:
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	cmp	w9, #255
	b.ne	LBB75_423
LBB75_387:
	mov	x9, #0
	and	x8, x8, #0xffff
	tbnz	w21, #0, LBB75_285
	b	LBB75_332
LBB75_388:
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	ldr	x10, [x0, #56]
	sub	x9, x23, x10
	cmp	x23, x10
	b.hs	LBB75_124
	b	LBB75_433
LBB75_389:
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	b	LBB75_40
LBB75_390:
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	b	LBB75_17
LBB75_391:
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	b	LBB75_50
LBB75_392:
	stur	xzr, [x29, #-80]
	stur	xzr, [x29, #-96]
	b	LBB75_60
LBB75_393:
	mov	x8, #-1
	stp	x8, x23, [x20]
	b	LBB75_429
LBB75_394:
	mov	w8, #2
	stp	x8, x23, [x20]
	b	LBB75_429
LBB75_395:
	str	x23, [x21, #976]
	mov	w8, #3
	str	x8, [x20]
	str	wzr, [x20, #8]
	str	x19, [x20, #16]
	mov	w8, #2
	strb	w8, [x20, #24]
	str	x19, [x20, #32]
	b	LBB75_429
LBB75_396:
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj10_ECsl4LB0XcnsVR_11trames_core
LBB75_397:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKje_ECsl4LB0XcnsVR_11trames_core
LBB75_398:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKjb_ECsl4LB0XcnsVR_11trames_core
LBB75_399:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj4_ECsl4LB0XcnsVR_11trames_core
LBB75_400:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKjc_ECsl4LB0XcnsVR_11trames_core
LBB75_401:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj9_ECsl4LB0XcnsVR_11trames_core
LBB75_402:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj19_ECsl4LB0XcnsVR_11trames_core
LBB75_403:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj2_ECsl4LB0XcnsVR_11trames_core
LBB75_404:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKjf_ECsl4LB0XcnsVR_11trames_core
LBB75_405:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj3_ECsl4LB0XcnsVR_11trames_core
LBB75_406:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj7_ECsl4LB0XcnsVR_11trames_core
LBB75_407:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj0_ECsl4LB0XcnsVR_11trames_core
LBB75_408:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj1_ECsl4LB0XcnsVR_11trames_core
LBB75_409:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj11_ECsl4LB0XcnsVR_11trames_core
LBB75_410:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj14_ECsl4LB0XcnsVR_11trames_core
LBB75_411:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj5_ECsl4LB0XcnsVR_11trames_core
LBB75_412:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj15_ECsl4LB0XcnsVR_11trames_core
LBB75_413:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKjd_ECsl4LB0XcnsVR_11trames_core
LBB75_414:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj6_ECsl4LB0XcnsVR_11trames_core
LBB75_415:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj12_ECsl4LB0XcnsVR_11trames_core
LBB75_416:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKja_ECsl4LB0XcnsVR_11trames_core
LBB75_417:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj8_ECsl4LB0XcnsVR_11trames_core
LBB75_418:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj17_ECsl4LB0XcnsVR_11trames_core
LBB75_419:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj13_ECsl4LB0XcnsVR_11trames_core
LBB75_420:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached7tail_opNtNtCslITGYdeqHog_13trames_memory8parallel4ViewKj18_ECsl4LB0XcnsVR_11trames_core
LBB75_421:
	.cfi_restore_state
	.cfi_remember_state
	ldp	x9, x8, [x29, #-40]
	b	LBB75_428
LBB75_422:
	ldur	w8, [x29, #-48]
	stur	w8, [x29, #-63]
	ldur	w8, [x29, #-45]
	stur	w8, [x29, #-60]
	stur	x10, [x29, #-72]
	sturb	w9, [x29, #-64]
	b	LBB75_427
LBB75_423:
	ldur	x10, [x29, #-40]
	stur	x10, [x29, #-69]
	ldur	x10, [x29, #-35]
	stur	x10, [x29, #-64]
	sturb	w9, [x29, #-72]
	sturh	w8, [x29, #-71]
	b	LBB75_427
LBB75_424:
	ldur	x10, [x29, #-40]
	stur	x10, [x29, #-67]
	ldur	w10, [x29, #-33]
	stur	w10, [x29, #-60]
	sturb	w9, [x29, #-72]
	stur	w8, [x29, #-71]
	b	LBB75_427
LBB75_425:
	ldur	w10, [x29, #-48]
	stur	w10, [x29, #-63]
	ldur	w10, [x29, #-45]
	stur	w10, [x29, #-60]
	sturb	w9, [x29, #-72]
	stur	x8, [x29, #-71]
	b	LBB75_427
LBB75_426:
	ldur	x10, [x29, #-40]
	stur	x10, [x29, #-70]
	ldur	x10, [x29, #-34]
	stur	x10, [x29, #-64]
	sturb	w9, [x29, #-72]
	sturb	w8, [x29, #-71]
LBB75_427:
	ldp	x9, x8, [x29, #-72]
	ldur	x21, [x29, #-88]
LBB75_428:
	str	x23, [x21, #976]
	mov	w10, #3
	stp	x10, xzr, [x20]
	stp	x19, x9, [x20, #16]
	str	x8, [x20, #32]
LBB75_429:
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	ret
LBB75_430:
	.cfi_restore_state
	.cfi_remember_state
	ldr	x21, [sp, #96]
	ldur	x24, [x29, #-80]
	ldur	x26, [x29, #-96]
	.cfi_def_cfa wsp, 256
	ldp	x29, x30, [sp, #240]
	add	sp, sp, #256
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	__RINvNtCs7Hne36L0lGo_10trames_cpu6cached11tail_decodeNtNtCslITGYdeqHog_13trames_memory8parallel4ViewECsl4LB0XcnsVR_11trames_core
LBB75_431:
	.cfi_restore_state
Lloh938:
	adrp	x2, l_anon.731397bb9a610793db00e25b847a0223.475@PAGE
Lloh939:
	add	x2, x2, l_anon.731397bb9a610793db00e25b847a0223.475@PAGEOFF
	mov	x0, x8
	bl	__RNvNtCsce4t4En13qn_4core9panicking18panic_bounds_check
LBB75_432:
Lloh940:
	adrp	x0, l_anon.731397bb9a610793db00e25b847a0223.474@PAGE
Lloh941:
	add	x0, x0, l_anon.731397bb9a610793db00e25b847a0223.474@PAGEOFF
	bl	__RNvNtNtCsce4t4En13qn_4core9panicking11panic_const23panic_const_div_by_zero
LBB75_433:
Lloh942:
	adrp	x0, l_anon.731397bb9a610793db00e25b847a0223.473@PAGE
Lloh943:
	add	x0, x0, l_anon.731397bb9a610793db00e25b847a0223.473@PAGEOFF
	bl	__RNvNtNtCsce4t4En13qn_4core9panicking11panic_const24panic_const_sub_overflow
LBB75_434:
Lloh944:
	adrp	x2, l_anon.731397bb9a610793db00e25b847a0223.476@PAGE
Lloh945:
	add	x2, x2, l_anon.731397bb9a610793db00e25b847a0223.476@PAGEOFF
	mov	x0, x8
	bl	__RNvNtCsce4t4En13qn_4core9panicking18panic_bounds_check
LBB75_435:
Lloh946:
	adrp	x0, l_anon.731397bb9a610793db00e25b847a0223.373@PAGE
Lloh947:
	add	x0, x0, l_anon.731397bb9a610793db00e25b847a0223.373@PAGEOFF
	bl	__RNvNtNtCsce4t4En13qn_4core9panicking11panic_const24panic_const_shl_overflow
LBB75_436:
Lloh948:
	adrp	x0, l_anon.731397bb9a610793db00e25b847a0223.372@PAGE
Lloh949:
	add	x0, x0, l_anon.731397bb9a610793db00e25b847a0223.372@PAGEOFF
	bl	__RNvNtNtCsce4t4En13qn_4core9panicking11panic_const24panic_const_sub_overflow
LBB75_437:
Lloh950:
	adrp	x2, l_anon.731397bb9a610793db00e25b847a0223.226@PAGE
Lloh951:
	add	x2, x2, l_anon.731397bb9a610793db00e25b847a0223.226@PAGEOFF
	bl	__RNvNtCsce4t4En13qn_4core9panicking18panic_bounds_check
LBB75_438:
Lloh952:
	adrp	x2, l_anon.731397bb9a610793db00e25b847a0223.224@PAGE
Lloh953:
	add	x2, x2, l_anon.731397bb9a610793db00e25b847a0223.224@PAGEOFF
	bl	__RNvNtCsce4t4En13qn_4core9panicking18panic_bounds_check
LBB75_439:
Lloh954:
	adrp	x2, l_anon.731397bb9a610793db00e25b847a0223.225@PAGE
Lloh955:
	add	x2, x2, l_anon.731397bb9a610793db00e25b847a0223.225@PAGEOFF
	bl	__RNvNtCsce4t4En13qn_4core9panicking18panic_bounds_check
LBB75_440:
Lloh956:
	adrp	x2, l_anon.731397bb9a610793db00e25b847a0223.227@PAGE
Lloh957:
	add	x2, x2, l_anon.731397bb9a610793db00e25b847a0223.227@PAGEOFF
	bl	__RNvNtCsce4t4En13qn_4core9panicking18panic_bounds_check
LBB75_441:
Lloh958:
	adrp	x0, l_anon.731397bb9a610793db00e25b847a0223.231@PAGE
Lloh959:
	add	x0, x0, l_anon.731397bb9a610793db00e25b847a0223.231@PAGEOFF
Lloh960:
	adrp	x2, l_anon.731397bb9a610793db00e25b847a0223.235@PAGE
Lloh961:
	add	x2, x2, l_anon.731397bb9a610793db00e25b847a0223.235@PAGEOFF
	mov	w1, #127
	bl	__RNvNtCsce4t4En13qn_4core9panicking9panic_fmt
LBB75_442:
Lloh962:
	adrp	x0, l_anon.731397bb9a610793db00e25b847a0223.231@PAGE
Lloh963:
	add	x0, x0, l_anon.731397bb9a610793db00e25b847a0223.231@PAGEOFF
Lloh964:
	adrp	x2, l_anon.731397bb9a610793db00e25b847a0223.234@PAGE
Lloh965:
	add	x2, x2, l_anon.731397bb9a610793db00e25b847a0223.234@PAGEOFF
	mov	w1, #127
	bl	__RNvNtCsce4t4En13qn_4core9panicking9panic_fmt
LBB75_443:
Ltmp267:
	b	LBB75_448
LBB75_444:
Ltmp258:
	b	LBB75_448
LBB75_445:
Ltmp261:
	b	LBB75_448
LBB75_446:
Ltmp264:
	b	LBB75_448
LBB75_447:
Ltmp270:
LBB75_448:
	sub	x8, x29, #72
	ldur	x9, [x29, #-64]
	ldr	x8, [x8]
	add	x8, x8, #16
LBB75_449:
	ldxr	x10, [x8]
	bic	x10, x10, x9
	stlxr	w11, x10, [x8]
	cbnz	w11, LBB75_449
	bl	__Unwind_Resume
	.loh AdrpAdd	Lloh936, Lloh937
	.loh AdrpAdd	Lloh938, Lloh939
	.loh AdrpAdd	Lloh940, Lloh941
	.loh AdrpAdd	Lloh942, Lloh943
	.loh AdrpAdd	Lloh944, Lloh945
	.loh AdrpAdd	Lloh946, Lloh947
	.loh AdrpAdd	Lloh948, Lloh949
	.loh AdrpAdd	Lloh950, Lloh951
	.loh AdrpAdd	Lloh952, Lloh953
	.loh AdrpAdd	Lloh954, Lloh955
	.loh AdrpAdd	Lloh956, Lloh957
	.loh AdrpAdd	Lloh960, Lloh961
	.loh AdrpAdd	Lloh958, Lloh959
	.loh AdrpAdd	Lloh964, Lloh965
	.loh AdrpAdd	Lloh962, Lloh963
Lfunc_end39:
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
Lloh1006:
	adrp	x14, LJTI82_0@PAGE
Lloh1007:
	add	x14, x14, LJTI82_0@PAGEOFF
LBB82_1:
	mov	x16, x15
	madd	x17, x22, x13, x17
	ldrb	w15, [x17, #36]
	ldrb	w19, [x17, #37]
	ldrb	w22, [x17, #39]
	ldr	x17, [x17, #16]
	add	x27, x12, x19, lsl #3
	cmp	w19, #31
	csel	x19, x11, x27, eq
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
	ldr	x27, [x16]
	ldr	x28, [x10, #64]
	cmp	x27, x28
	b.ne	LBB82_36
	ldrb	w16, [x16, #8]
	mov	x23, x19
	adr	x19, LBB82_1
	ldrb	w27, [x14, x16]
	add	x19, x19, x27, lsl #2
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
	.loh AdrpAdd	Lloh1006, Lloh1007
	.cfi_endproc
