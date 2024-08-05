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


	ORG 01E8000h

STRING_X0: DW ?


	ORG 0E6477Eh

ENTRY:
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
	RET


MAIN:
	call 0EF55A7h        ;	CALL Some_VGA_setup
	
	text HELLO_WORLD_STR, 1, 1, 1
	text TEST_STR, 1, 2, 2
	text FELIPE_STR, 1, 3, 3

	text YEAH_STR, 1, 5, 7

	RET

HELLO_WORLD_STR: db "HELLO WORLD", 0
TEST_STR: db "RUNNING DURING BOOT OF KN5000.", 0
FELIPE_STR: db "FELIPE CORREA DA SILVA SANCHES", 0
YEAH_STR: db "FUCK YEAH", 0


LONG_PAUSE:
	LD BC, 0
LONG_PAUSE_LOOP1:
	LD DE, 080h
LONG_PAUSE_LOOP2:
	DJNZ DE, LONG_PAUSE_LOOP2
	DJNZ BC, LONG_PAUSE_LOOP1
	RET

PAUSE:
	LD BC, 0
PAUSE_LOOP1:
	LD DE, 01h
PAUSE_LOOP2:
	DJNZ DE, PAUSE_LOOP2
	DJNZ BC, PAUSE_LOOP1
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

