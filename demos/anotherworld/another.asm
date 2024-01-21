; This program must replace the hd-ae5000_v2_01i.ic4 ROM
; of the extension board connected to the Technics SX-KN5000 musical keyboard

	cpu	96c141	; Actual CPU is 94c241f
	page	0
	maxmode	on
	
	org 0280000h

EXTENSION_HEADER:
	db 'XAPR'
	dd POINTERS
	JP ENTRY
POINTERS:
	db 0Eh, 00h, 00h, 00h; EMPTY_ROUTINE 
	db 0Eh, 00h, 00h, 00h; EMPTY_ROUTINE 
	db 0Eh, 00h, 00h, 00h; EMPTY_ROUTINE 
	db 0Eh, 00h, 00h, 00h; EMPTY_ROUTINE 
	db 0Eh, 00h, 00h, 00h; EMPTY_ROUTINE 

ENTRY:
	EI 06 ; DISABLE INTERRUPTS
	LD XWA, 0

MAIN_LOOP:
	LD XHL, BITMAP_2
	CALL DRAW_BITMAP

	LD WA, 7
	CALL SETUP_PALETTE
	CALL PAUSE

	LD WA, 8
	CALL SETUP_PALETTE
	CALL PAUSE

	LD WA, 9
	CALL SETUP_PALETTE
	CALL PAUSE

	LD WA, 10
	CALL SETUP_PALETTE
	CALL PAUSE
	
	LD XHL, BITMAP_1
	CALL DRAW_BITMAP

	LD WA, 11
	CALL SETUP_PALETTE
	CALL PAUSE

	LD WA, 12
	CALL SETUP_PALETTE
	CALL PAUSE

	LD WA, 13
	CALL SETUP_PALETTE
	CALL PAUSE

	LD WA, 14
	CALL SETUP_PALETTE
	CALL PAUSE

	JP MAIN_LOOP

PAUSE:
	LD BC, 0
PAUSE_LOOP1:
	LD DE, 020h
PAUSE_LOOP2:
	DJNZ DE, PAUSE_LOOP2
	DJNZ BC, PAUSE_LOOP1
	RET

DRAW_BITMAP:
	LD XDE, 001a0000h + 20*320	; vga memory, skipping the first 20 lines because the image has 320x200 resolution and the screen has 320x240
	LD XBC, 320 * 200 / 2		; bitmap data length in 16-bit words (320x200 pixels)
	LDIRW
	RET

SETUP_PALETTE:
	LD BC,0
	LD XDE, 01703c8h		; VGA 3c8 port (select color palette index
	LD (XDE), C

	LD BC, 3*16							; data length: 16 colors, 3 components, in number of bytes
	LD XDE, 01703c9h					; VGA 3c9 port (for setting the color palette values: r, g and b)
	LD XHL, INTRO_PALETTES
	SLA 4, XWA
	ADD XHL, XWA
	ADD XHL, XWA
	ADD XHL, XWA

	; arbitrarily choosing one of the palettes here, for now (the 7th one)
PALETTE_LOOP:
	LD C, (XHL)
	INC XHL
	LD (XDE), C
	DJNZ BC, PALETTE_LOOP
	RET

INTRO_BYTECODE:
	binclude "resources/resource-0x18.bin"

INTRO_PALETTES:
	binclude "intro_palettes.bin"

INTRO_VIDEO_1:
	binclude "resources/resource-0x19.bin"

BITMAP_1:
	binclude "another_world_logo.bin"
BITMAP_2:
	binclude "other_bitmap.bin"
	
	org 02fffffh
	db 0ffh
