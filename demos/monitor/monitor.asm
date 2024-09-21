; (c) 2024 Felipe Correa da Silva Sanches <juca@members.fsf.org>
; Licensed under GPL version 2 or later.
;
; Some useful code to run as an injected payload during the boot of
; Technics SX-KN5000 musical keyboard.
;
; The assembler I am using is compiled from source-code downloaded from
; http://john.ccac.rwth-aachen.de:8000/as/index.html
;
;
	cpu	96c141	; Actual CPU is 94c241f
	page	0
	maxmode	on

text MACRO stringptr, x, y, color
	LD XIX, stringptr
	LD DE, x
	LD HL, y
	LD B, color
	CALL DRAW_STRING
	ENDM

hex_number MACRO value, x, y, color
	LD XIX, value
	LD DE, x
	LD HL, y
	LD B, color
	CALL PRINT_HEX
	ENDM

	ORG 01E8000h

STRING_X0: DW ?
HEX_NUM_STRING: DB 11 DUP (?)


;	ORG 0EF05E8h ; without preamble

	ORG 0E0018Eh + 020h ; with preamble at the fw_update screen bitmaps

ENTRY:
	;;;;; EI 6 ; disable interrupts
	PUSH XWA
	PUSH XBC
	PUSH XDE
	PUSH XHL 
	PUSH XIX
	PUSH XIY
	CALL MAIN
	POP XIY
	POP XIX
	POP XHL
	POP XDE
	POP XBC
	POP XWA
	;;;;;;;	EI 0 ; enable interrupts
	RET
	

MAIN:
	LD C, 0
	LD A, 'S'
	LD (0160000h), A
	call PAUSE
	DEC C
	LD (0160004h), C
	call PAUSE

	ld BC, 8h
	ld XIX, 0E00038h

	LD A, 080h
	LD (0160006h), A
	call PAUSE
DUMP:
	LD A, (XIX+)
	LD (0160000h), A
	call PAUSE
	LD (0160004h), C
	call PAUSE
	DJNZ BC, DUMP
	DEC C

	; call init_vga
	; call print_OK
	; text FROM_BOOT_STR, 1, 3, 3
	call print_END
	ret
	
; infloop:
;	JP infloop

init_vga:
	LD A, 'V'
	LD (0160000h), A
	call PAUSE
	DEC C
	LD (0160004h), C
	call PAUSE
	
	LD A, 'G'
	LD (0160000h), A
	call PAUSE
	DEC C
	LD (0160004h), C
	call PAUSE

	LD A, 'A'
	LD (0160000h), A
	call PAUSE
	DEC C
	LD (0160004h), C
	call PAUSE

	call 0EF55A7h        ;	CALL Some_VGA_setup
	ret

print_OK:
	LD A, 'O'
	LD (0160000h), A
	DEC C
	LD (0160004h), C
	call PAUSE

	call PAUSE
	LD A, 'K'
	LD (0160000h), A
	call PAUSE
	DEC C
	LD (0160004h), C
	call PAUSE
	ret

print_END:
	LD (0160000h), 'E'
	call PAUSE
	DEC C
	LD (0160004h), C
	call PAUSE

	LD (0160000h), 'N'
	call PAUSE
	DEC C
	LD (0160004h), C
	call PAUSE

	LD (0160000h), 'D'
	call PAUSE
	DEC C
	LD (0160004h), C
	call PAUSE
	ret


FROM_BOOT_STR: db "RUNNING DURING BOOT OF KN5000.", 0

PAUSE:
	PUSH BC
	PUSH DE
	LD BC, 0
PAUSE_LOOP1:
	LD DE, 020h
PAUSE_LOOP2:
	DJNZ DE, PAUSE_LOOP2
	DJNZ BC, PAUSE_LOOP1
	POP DE
	POP BC
	ret

PRINT_HEX:
; XIX: number
; DE: x
; HL: y
; B: color

	LD XIY, HEX_NUM_STRING
	LD (XIY+), '0'
	LD (XIY+), 'x'
	LD C, 8
print_hex_digit_loop:
	RLC 4, XIX
	LD WA, IX
	AND A, 0fh
	CP A, 10
	JR UGE, HEX_LETTERS
HEX_NUMBERS:
	ADD A, '0'
	LD (XIY), A
	INC XIY
	JP NEXT_NIBBLE
	
HEX_LETTERS:
	SUB A, 10
	ADD A, 'A'
	LD (XIY), A
	INC XIY

NEXT_NIBBLE:
	DJNZ C, print_hex_digit_loop

	XOR A, A
	LD (XIY), A
	text HEX_NUM_STRING, DE, HL, B
	RET

DRAW_STRING:
; XIX: string
; DE: x
; HL: y
; B: color

 	; x = 1 + 8 * x;
	SLA 3, DE
	INC DE

 	;	y = 1 + 9 * y;
	PUSH WA
	LD WA, HL
	SLA 3, HL
	ADD HL, WA
	POP WA
	INC HL

	LDW (STRING_X0), DE	;	uint16_t x0 = x;
	LD C, (XIX)
	INC XIX

DRAW_STRING_LOOP:
	CP C, 0
	RET Z		;	for (; *c != '\0'; c++)

	CP C, 10	;		if (*c == '\n')
	JP NE, NOT_A_LINE_BREAK

LINE_BREAK:
	INC 8, HL			; y+=8;
	LD DE, (STRING_X0)	; x=x0;
	LD C, (XIX)
	INC XIX
	JP DRAW_STRING_LOOP

NOT_A_LINE_BREAK:
	CALL DRAW_CHAR
	INC 8, DE		; x+=8
	LD C, (XIX)
	INC XIX
	JP DRAW_STRING_LOOP


DRAW_CHAR:
	; DE: x
	; HL: y
	; B: color
	; C: character

	LD WA, 0
	LD A, C
	SUB A, 020h
	EXTS XWA
	SLA 3, XWA
	LD XIY, BITMAP_FONT
	ADD XIY, XWA

	PUSH XIX
	PUSH XHL
	PUSH BC

	EXTS XDE
	EXTS XHL

	LD XIX, 1a0000h
	MUL XHL, 320
	ADD XIX, XHL
	ADD XIX, XDE

	LD WA, 8
DRAW_CHAR_J_LOOP:
	LD C, (XIY)
	INC XIY			; uint8_t row = font[(character - ' ') * 8 + j];

	LD QWA, 8
DRAW_CHAR_I_LOOP:
	SLA 1, C
	JP NC, DONT_PLOT_THIS_PIXEL
	LD (XIX), B
DONT_PLOT_THIS_PIXEL:
	INC XIX
	DJNZ QWA, DRAW_CHAR_I_LOOP

	DEC 8, XIX
	ADD XIX, 320
	DJNZ WA, DRAW_CHAR_J_LOOP

	POP BC
	POP XHL
	POP XIX
	RET

BITMAP_FONT:
	binclude "hardcoded_data/anotherworld_chargen.rom"

