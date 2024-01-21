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
	CALL SETUP_PALETTE
	LD XBC, SOME_BITMAP
	LD XWA, 001a0000h + 20*320	; vga memory, skipping the first 20 lines because the image has 320x200 resolution and the screen has 320x240
	LD XDE, 320 * 200 / 2		; bitmap data length in 16-bit words (320x200 pixels)
	CALL Copy_DE_words_from_XBC_to_XWA

INFINITE_LOOP:
	JP INFINITE_LOOP

Copy_DE_words_from_XBC_to_XWA: 
	LD XIY, XBC
	LD XIX, XWA
	LD BC, DE
	LDIRW
	RET

SETUP_PALETTE:
	LD (01703c8h), 00h		; VGA 3c8 port (select color palette index)
	LD XIY, SOME_PALETTE 
	LD XIX, 01703c9h		; VGA 3c9 port (for setting the color palette values: r, g and b)
	LD XBC, 3*16 / 2		; data length: 16 colors, 3 components, in number of 16-bit words
	LDIRW
	RET

SOME_PALETTE:
	db "some_palette.bin";	include "some_palette.bin"

SOME_BITMAP:
	db "some_bitmap.bin"; include "some_bitmap.bin"
	
	org 02fffffh
	db 0ffh
