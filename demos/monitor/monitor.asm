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
	PUSH XIX
	PUSH XIY
	PUSH DE
	PUSH HL
	PUSH BC
	PUSH WA
	LD XIX, stringptr
	LD DE, x
	LD HL, y
	LD B, color
	CALL DRAW_STRING
	POP WA
	POP BC
	POP HL
	POP DE
	POP XIY
	POP XIX
	ENDM

hex_number MACRO value, x, y, color
	PUSH XIX
	PUSH XIY
	PUSH DE
	PUSH HL
	PUSH BC
	PUSH WA
	LD XIX, value
	LD DE, x
	LD HL, y
	LD B, color
	CALL PRINT_HEX
	POP WA
	POP BC
	POP HL
	POP DE
	POP XIY
	POP XIX
	ENDM
	
;	ORG 01E8000h  ; mainboard DRAM
;	ORG 0000800h  ; mainboard SRAM
	ORG 0200000h  ; HDAE5000 SRAM

STRING_X0: DW ?
HEX_NUM_STRING: DB 11 DUP (?)


;	ORG 0EF05E8h ; without preamble

	ORG 0E0018Eh + 020h ; with preamble at the fw_update screen bitmaps

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
	

SEND_BYTE:
	LD (0160000h), A
	call PAUSE
	XOR (0160004h), 1
	call PAUSE
	ret

FOO:
	push BC
	push HL
	PUSH DE
	LD HL, 4
	LD DE, 8
	LD B, 15
	LD C, 'M'
	; DE: x
	; HL: y
	; B: color
	; C: character
	call DRAW_CHAR
	POP DE
	POP HL
	POP BC
	CALL PAUSE
	RET

MAIN:
	
	call 0EF55A7h        ;	CALL Some_VGA_setup

	LD BC, 030h
	LD DE, 050h
	call draw_please_wait_bitmap

	CALL LONG_PAUSE
	
;	CALL FOO

	LD BC, 040h
	LD DE, 060h
	call draw_completed_bitmap
	CALL LONG_PAUSE

	ret



	LD A, 083h  ; Ports A & C-upper: output / Port B & C-lower: input 
;	LD (0160006h), A
	
_LOOP:
	call send_PL

	LD C, (0160004h)
_wait_receive_strobe:
	LD A, (0160004h)
	XOR A, C
	AND A, 2
	JP Z, _wait_receive_strobe

	LD A, (0160002h)
	LD BC, 00h
	LD DE, 00h
	LD C, A
	AND C, 15
	EX D, E
	call draw_completed_bitmap
	
	JP _LOOP
	; receive_length
	; receive_buffer
	; execute_payload
	
	RET

;	text FROM_BOOT_STR, 1, 3, 3

	LD A, 080h
	LD (0160006h), A
	call PAUSE

	ld XIX, 0300000h
	; Will read 256 * 4kb = 1Mb

;	ld BC, 256
	ld BC, 16
	CALL DUMP ; 300000h

	call draw_completed_bitmap

;	text RESUME_BOOT_STR, 5, 4, 3	
;	CALL LONG_PAUSE
	RET
	
TODO__OTHER_ROM_DUMPS:
	ld BC, 256
	CALL DUMP ; 400000h

	ld BC, 256
	CALL DUMP ; 500000h

	ld BC, 256
	CALL DUMP ; 600000h

	ld BC, 256
	CALL DUMP ; 700000h

	ld BC, 256
	CALL DUMP ; 800000h

	ld BC, 256
	CALL DUMP ; 900000h

	ld BC, 256
	CALL DUMP ; a00000h

	ld BC, 256
	CALL DUMP ; b00000h

	ld BC, 256
	CALL DUMP ; c00000h

	ld BC, 256
	CALL DUMP ; d00000h

	ld BC, 256
	CALL DUMP ; e00000h

	ld BC, 256
	CALL DUMP ; f00000h

	call send_END
	
	RET


DUMP:	; BC chunks of 4kb
	PUSH DE
_BC_loop:
	call send_OK
	; hex_number XIX, 4, 4, 15
	LD DE, 01000h
_DE_loop:
	LD A, (XIX+)
	call SEND_BYTE
	DJNZ DE, _DE_loop
	DJNZ BC, _BC_loop
	POP DE
	RET


draw_please_wait_bitmap:
	PUSH XWA
	PUSH BC
	PUSH DE
	PUSHW 0008h
	PUSHW 0003h
	LD XWA, 00e00b2eh  ; "Please Wait !!"
	LD BC, 0030h
	LD DE, 0050h
	CALL 0EF5040h        ; call Draw_FlashMemUpdate_message_bitmap
	POP DE
	POP BC
	POP XWA
	RET

draw_completed_bitmap:
	PUSH XWA
	PUSH BC
	PUSH DE
	PUSHW 0008h
	PUSHW 0003h
	LD XWA, 00e008c6h  ; "Completed!"
;	LD BC, 0030h
;	LD DE, 0050h
	CALL 0EF5040h        ; call Draw_FlashMemUpdate_message_bitmap
	POP DE
	POP BC
	POP XWA
	RET

init_vga:
	LD A, 'V'
	call SEND_BYTE
	
	LD A, 'G'
	call SEND_BYTE

	LD A, 'A'
	call SEND_BYTE

	call 0EF55A7h        ;	CALL Some_VGA_setup
	ret

send_PL:  ; payload
	LD A, 'P'
	call SEND_BYTE

	LD A, 'L'
	call SEND_BYTE
	ret

send_OK:
	LD A, 'O'
	call SEND_BYTE

	LD A, 'K'
	call SEND_BYTE
	ret

send_END:
	LD A, 'E'
	call SEND_BYTE

	LD A, 'N'
	call SEND_BYTE

	LD A, 'D'
	call SEND_BYTE
	ret


FROM_BOOT_STR: db "DUMPING A ROM...", 0
RESUME_BOOT_STR: db "WILL NOW RESUME BOOT SEQUENCE...", 0

LONG_PAUSE:
	PUSH BC
	PUSH DE
	LD BC, 0
PAUSE_LOOP1:
	LD DE, 03h
PAUSE_LOOP2:
	DJNZ DE, PAUSE_LOOP2
	DJNZ BC, PAUSE_LOOP1
	POP DE
	POP BC
	ret

PAUSE:
	PUSH DE
	LD DE, 0C0h
PAUSE_LOOP_:
	DJNZ DE, PAUSE_LOOP_
	POP DE
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

