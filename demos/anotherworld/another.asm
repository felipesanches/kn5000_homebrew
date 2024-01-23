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
	LD XHL, BITMAP_1
	CALL DRAW_BITMAP

	LD WA, 7
	CALL SETUP_PALETTE
	CALL PAUSE

;	(CINEMATIC_CARKEY, COLOR_BLACK=0xFF, zoom=0x40, x=160, y=100);
	LD XIX, INTRO_VIDEO_1
	ADD XIX, 0F6D2h			; CINEMATIC_CARKEY
	LD DE, 160
	LD HL, 100
	LD BC, 0FF40h
	CALL readAndDrawPolygon
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

readAndDrawPolygon:
	; Inputs:
	; XIX: polygon data pointer
	; DE: x
	; HL: y
	; B: color (Black = FFh)
	; C: zoom (default = 40h)
	PUSH DE
	PUSH HL
	PUSH BC
	
	PUSH XIX
	LD A, (XIX)
	INC XIX
	CP A, 0C0H
	JP ULT, VALUE_IS_LT_C0

VALUE_IS_GTE_C0:
	POP XIX
	;(&m_polygonData[m_data_offset], zoom, m_polygonData, 0);
	call readVertices
	; (color, pt)
	CALL fillPolygon
	JP end_of_readAndDrawPolygon

VALUE_IS_LT_C0: ; for now, here we simply assume value is == 2 without checking.
	;(zoom, pt)
	PUSH BC ; C=zoom
	PUSH DE ; x
	PUSH HL ; y
	CALL readAndDrawPolygonHierarchy
	POP BC
	POP DE
	POP HL

	POP XIX

end_of_readAndDrawPolygon:
	POP BC
	POP HL
	POP DE
	RET

readVertices:
	; implement-me!
	RET

fillPolygon:
	; implement-me!
	RET

readAndDrawPolygonHierarchy:

	;	pt.x -= m_polygonData[m_data_offset++] * zoom / DEFAULT_ZOOM;
	;	pt.y -= m_polygonData[m_data_offset++] * zoom / DEFAULT_ZOOM;
	LD WA, 0
	LD A, (XIX)
	INC XIX
	LD HL, (XSP + 6)
	MUL XHL, WA			; PT.X *= ZOOM
	SRAW 6, HL			; PT.X /= default_zoom (40h)
	LD (XSP + 6), HL
	
	LD WA, 0
	LD A, (XIX)
	INC XIX
	LD HL, (XSP + 4)
	MUL XHL, WA				; PT.Y *= ZOOM
	SRAW 6, HL				; PT.Y /= default_zoom (40h)
	LD (XSP + 4), HL

	ld B, 0
	LD C, (XIX)
	INC XIX
	
children_loop:
	PUSH HL
	; HL = offset
	LD HL, (XIX)
	POP HL

	; VMPoint po(pt);
	; po.x += m_polygonData[m_data_offset++] * zoom / DEFAULT_ZOOM;
	; po.y += m_polygonData[m_data_offset++] * zoom / DEFAULT_ZOOM;

	; uint16_t color = 0xFF;
	; if (offset & 0x8000)
	; {
	; 	color = m_polygonData[m_data_offset++] & 0x7F;
	; 	m_data_offset++; //and waste a byte...
	; }

	; uint16_t backup = m_data_offset;

	; m_data_offset = (offset & 0x7FFF) * 2;

	; (color, zoom, po)
	CALL readAndDrawPolygon

	; m_data_offset = backup;
	
	DJNZ BC, children_loop
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
