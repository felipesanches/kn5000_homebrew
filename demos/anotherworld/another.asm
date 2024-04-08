; (c) 2024 Felipe Correa da Silva Sanches <juca@members.fsf.org>
; Licensed under GPL version 2 or later.
;
; This is a port of the Another World VM (initially supporting only non-interactive
; playback of the game intro) to run from an extension board on a
; Technics SX-KN5000 musical keyboard.
;
; This program must replace the hd-ae5000_v2_01i.ic4 ROM of the extension board.
;
; This implementation is derived from my port of the Another World VM
; as a high-level emulation on MAME
;
; And that port was derived from Fabien Sanglard's AW-VM available at:
; https://github.com/fabiensanglard/Another-World-Bytecode-Interpreter
;
; The assembler I am using is compiled from source-code downloaded from
; http://john.ccac.rwth-aachen.de:8000/as/index.html
;
; Game assets must be extracted from an original copy of the game
; using the scripts provided at:
; https://github.com/felipesanches/AnotherWorld_VMTools
;
	cpu	96c141	; Actual CPU is 94c241f
	page	0
	maxmode	on

	ORG 0200000h

VM_VARIABLES:	DW	256 DUP (?)

THREADS_DATA:	DW	64*2 DUP (?)  ; For each of the 64 threads:
PC_OFFSET			EQU 0  ; 16 bits
REQUESTED_PC_OFFSET	EQU 2  ; 16 bits

VM_IS_CHANNEL_ACTIVE:	DB	64*2 DUP (?)   ; For each of the 64 threads:
CUR_STATE		EQU 0  ; boolean stored as a byte
REQUESTED_STATE	EQU 1  ; boolean stored as a byte

CURRENT_THREAD: DB ?
PC:				DW ?
VM_STACK_POINTER: DW ?
VM_STACK: DW 256 DUP (?)


POLYGON_NUM_POINTS:	DB ?
POLYGON_BBOX_W:		DW ?					; uint16_t
POLYGON_BBOX_H:		DW ?					; uint16_t
POLYGON_POINTS:		DW	50 DUP (?, ?, ?)
POLYGON_XMIN:	DW ?						; int16_t
POLYGON_XMAX:	DW ?						; int16_t
POLYGON_YMIN:	DW ?						; int16_t
POLYGON_YMAX:	DW ?						; int16_t
HLINEY:		DW ?							; int16_t
CUR_LINE:			; uint32_t
CUR_LINE_LOW:	DW ?
CUR_LINE_HIGH:	DW ?

CPT1:				; uint32_t
CPT1_LOW:	DW ?
CPT1_HIGH:	DW ?

CPT2:				; uint32_t
CPT2_LOW:	DW ?
CPT2_HIGH:	DW ?

STEP1:				; int32_t
STEP1_LOW:	DW ?
STEP1_HIGH:	DW ?

STEP2:				; int32_t
STEP2_LOW:	DW ?
STEP2_HIGH:	DW ?

; int16_t x1, x2;
X1:			DW ?
X2:			DW ?

; 	uint16_t h;
POLYGON_H:	DW ?
DX:			DW ?	; int16_t

;	int16_t xmax, xmin
LINE_XMIN:	DW ?
LINE_XMAX:	DW ?
CUR_PAGE_PTR_1: DD ?
CUR_PAGE_PTR_2: DD ?
CUR_PAGE_PTR_3: DD ?

STRING_X0: DW ?

	ORG 0280000h

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

CALC_LINE_XMAX_AND_XMIN:
	; int16_t xmax = MAX(x1, x2);
	LD WA, (X1)
	CP WA, (X2)
	JP GT, XMAX_OK
	LD WA, (X2)
XMAX_OK:
	LD (LINE_XMAX), WA
	
	; int16_t xmin = MIN(x1, x2);
	LD WA, (X1)
	CP WA, (X2)
	JP LT, XMIN_OK
	LD WA, (X2)
XMIN_OK:
	LD (LINE_XMIN), WA
	RET

drawLineN:
	; Inputs:
	; C = color
	PUSH XIX
	PUSH XHL
	
	CALL CALC_LINE_XMAX_AND_XMIN

	; for (int16_t x=xmin; x<=xmax; x++)
	; 	m_curPagePtr1->pix(m_hliney, x) = color;
	LD XIX, (CUR_LINE)
	ADDW (CUR_LINE_LOW), 320
	ADCW (CUR_LINE_HIGH), 0
	LD HL, (LINE_XMIN)
	EXTS XHL
	ADD XIX, XHL
	LD HL, (LINE_XMAX)
	SUB HL, (LINE_XMIN)

drawLineN_loop:
	LDB (XIX), C
	INC XIX
	DJNZ HL, drawLineN_loop
	
	POP XHL
	POP XIX
	RET

drawLineP:
	; TODO: Implement-me!
	jp drawLineN ; THIS IS INCORRECT!

	; Inputs:
	; C = color
	CALL CALC_LINE_XMAX_AND_XMIN

	; for (int16_t x=xmin; x<=xmax; x++)
	; {
	; 	color = m_page_bitmaps[0].pix(m_hliney, x);
	; 	m_curPagePtr1->pix(m_hliney, x) = color;
	; }
	RET

drawLineBlend:
	; TODO: Implement-me!
	jp drawLineN ; THIS IS INCORRECT!

	; Inputs:
	; C = color
	CALL CALC_LINE_XMAX_AND_XMIN

	; for (int16_t x=xmin; x<=xmax; x++)
	; {
	;	color = m_curPagePtr1->pix(m_hliney, x);
	;	m_curPagePtr1->pix(m_hliney, x) = (color & 0x7) | 0x8;
	; }
	RET

drawPoint:
	PUSH XIX
	LD WA, 0
	LD WA, HL
	MUL XWA, 320
	; FIXME: do we need to zero the upper 16 bits of XDE here?
	ADD XWA, XDE
	LD XIX, 01a0000h
	ADD XIX, XWA
	LD (XIX), BC
	POP XIX
	RET

video MACRO type,data,x,y
	LD XIX, INTRO_VIDEO_type
	ADD XIX, data
	LD DE, x
	LD HL, y
	LD BC, 0FF40h
	CALL readAndDrawPolygon
	ENDM


text MACRO stringid, x, y, color
	LD WA, stringid
	LD DE, x
	LD HL, y
	LD B, color
	CALL DRAW_STRING
	ENDM


BREAK:
	; This is a temporary placeholder
	; while we still do not emulate
	; the VM thread execution

	LD WA, 0FEh		; end-of-frame
	CALL UPDATE_DISPLAY
	CALL PAUSE
	RET


UPDATE_DISPLAY:
	; WA: PageID
	CP WA, 0FEh
	JP EQ, _UPDATE_DISPLAY

	CP WA, 0FFh
	JP EQ, _UPDATE_DISPLAY_PAGEID_FF

_UPDATE_DISPLAY_PAGEID_NOT_FE_OR_FF:
	CALL GET_PAGE_PTR
	LD (CUR_PAGE_PTR_2), XWA
	JP _UPDATE_DISPLAY
	
_UPDATE_DISPLAY_PAGEID_FF:
	LD XIY, (CUR_PAGE_PTR_2)
	LD XWA, (CUR_PAGE_PTR_3)
	LD (CUR_PAGE_PTR_2), XWA
	LD (CUR_PAGE_PTR_3), XIY

_UPDATE_DISPLAY:
	LD XHL, (CUR_PAGE_PTR_2)
	LD XDE, 001a0000h + 20*320	; vga memory, skipping the first 20 lines because the image has 320x200 resolution and the screen has 320x240
	LD XBC, 320 * 200 / 2		; bitmap data length in 16-bit words (320x200 pixels)
	LDIRW
	RET


ENTRY:
	EI 06 ; DISABLE INTERRUPTS

VIDEO_START:
	LD XIX, PAGE_BITMAP_2
	LD (CUR_PAGE_PTR_1), XIX
	LD (CUR_PAGE_PTR_2), XIX
	LD XIX, PAGE_BITMAP_1
	LD (CUR_PAGE_PTR_3), XIX
	LDB (CURRENT_THREAD), 0
	LD XIX, VM_STACK
	LD (VM_STACK_POINTER), XIX

MAIN_LOOP:

	CALL EXECUTE_INSTRUCTION

	JP MAIN_LOOP

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
	
	LD A, (XIX)
	INC XIX
	CP A, 0C0H
	JP ULT, VALUE_IS_LT_C0

VALUE_IS_GTE_C0:

	CP B, 128
	JP ULT, COLOR_BIT_7_IS_OFF
	LD B, A
	ANDB B, 03Fh				;	if (color & 0x80) color = i & 0x3F;
COLOR_BIT_7_IS_OFF:
	
	;(&m_polygonData[m_data_offset], zoom);
	call readVertices

	; (color, pt)
	CALL fillPolygon

	JP end_of_readAndDrawPolygon

VALUE_IS_LT_C0: ; for now, here we simply assume value is == 2 without checking.
	;(zoom, pt)

	PUSH XIX
	PUSH BC ; C=zoom
	PUSH DE ; x
	PUSH HL ; y
	CALL readAndDrawPolygonHierarchy
	POP HL
	POP DE
	POP BC
	POP XIX

end_of_readAndDrawPolygon:
	POP BC
	POP HL
	POP DE
	RET

readVertices:
	PUSH XIX
	PUSH DE
	PUSH HL
	PUSH BC

	LD WA, 0
	LD A, (XIX)
	INC XIX
	LD DE, 0
	LD E, C
	MUL XWA, DE			; *= ZOOM
	SRAW 6, WA			; /= default_zoom (40h)
	LD (POLYGON_BBOX_W), WA

	LD WA, 0
	LD A, (XIX)
	INC XIX
	LD DE, 0
	LD E, C
	MUL XWA, DE			; *= ZOOM
	SRAW 6, WA			; /= default_zoom (40h)
	LD (POLYGON_BBOX_H), WA

	LD B, (XIX)
	INC XIX
	LD (POLYGON_NUM_POINTS), B

	LD XIY, POLYGON_POINTS
	LD DE, 0
	LD E, C

READ_THE_COORDINATES:

	LD WA, 0
	LD A, (XIX)
	INC XIX
	MUL XWA, DE			; *= ZOOM
	SRAW 6, WA			; /= default_zoom (40h)
	LD (XIY), WA
	INC 2, XIY

	LD WA, 0
	LD A, (XIX)
	INC XIX
	MUL XWA, DE			; *= ZOOM
	SRAW 6, WA			; /= default_zoom (40h)
	LD (XIY), WA
	INC 2, XIY

	DJNZ B, READ_THE_COORDINATES	

	POP BC
	POP HL
	POP DE
	POP XIX	
	RET

fillPolygon:
	; DE: x
	; HL: y
	; B: color (Black = FFh)

	PUSH BC
	LD C, B
	LD B, 0

	;if (m_polygon.bbox_w == 0 && m_polygon.bbox_h == 1 && m_polygon.numPoints == 4)
	CPW (POLYGON_BBOX_W), 0
	JP NZ, NOT_A_POINT
	CPW (POLYGON_BBOX_H), 1
	JP NZ, NOT_A_POINT
	CP (POLYGON_NUM_POINTS), 4
	JP NZ, NOT_A_POINT
	
	;(color, pt.x, pt.y);
	CALL drawPoint
	JP end_of_fillPolygon
	
	NOT_A_POINT:

	PUSH IX
	; int16_t xmin = pt.x - m_polygon.bbox_w / 2;
	LD IX, (POLYGON_BBOX_W)
	SRA 1, IX
	LD (POLYGON_XMIN), DE
	SUB (POLYGON_XMIN), IX

	; int16_t xmax = pt.x + m_polygon.bbox_w / 2;
	LD (POLYGON_XMAX), DE
	ADD (POLYGON_XMAX), IX

	; int16_t ymin = pt.y - m_polygon.bbox_h / 2;
	LD IX, (POLYGON_BBOX_H)
	SRA 1, IX
	LD (POLYGON_YMIN), HL
	SUB (POLYGON_YMIN), IX

	; int16_t ymax = pt.y + m_polygon.bbox_h / 2;	
	LD (POLYGON_YMAX), HL
	ADD (POLYGON_YMAX), IX
	POP IX


	;if (xmin >= 320 || xmax < 0 || ymin >= 200 || ymax < 0)
	;	return;
	CPW (POLYGON_XMIN), 320
	JP GE, end_of_fillPolygon
	CPW (POLYGON_XMAX), 0
	JP LT, end_of_fillPolygon
	CPW (POLYGON_YMIN), 200
	JP GE, end_of_fillPolygon
	CPW (POLYGON_YMAX), 0
	JP LT, end_of_fillPolygon


	LD WA, (POLYGON_YMIN)
	LD (HLINEY), WA

	LD XIX, POLYGON_POINTS				; i = 0;
	LD XIY, POLYGON_POINTS
	LD XWA, 0
	LD A, (POLYGON_NUM_POINTS)
	DEC A
	SLA 2, XWA
	ADD XIY, XWA						; j = m_polygon.numPoints - 1;

	; x2 = m_polygon.points[i].x + xmin;
	LD WA, (XIX)
	ADD WA, (POLYGON_XMIN)
	LD (X2), WA

	; x1 = m_polygon.points[j].x + xmin;
	LD WA, (XIY)
	ADD WA, (POLYGON_XMIN)
	LD (X1), WA

	INC 4, XIX		; 	i++;
	DEC 4, XIY		; 	j--;


	CP C, 10h
	JP Z, BLEND
	JP UGT, LINE_P

	LD XHL, drawLineN	; 	drawFct = &another_world_vm_state::drawLineN;
	JP AFTER_SETTING_DRAW_CALLBACK

	LINE_P:
	LD XHL, drawLineP	; 	drawFct = &another_world_vm_state::drawLineP;
	JP AFTER_SETTING_DRAW_CALLBACK

	BLEND:
	LD XHL, drawLineBlend

AFTER_SETTING_DRAW_CALLBACK:

	; uint32_t cpt1 = ((uint32_t) x1) << 16;
	LD WA, (X1)
	EXTS XWA
	SLA 16, XWA
	LD (CPT1), XWA

	; uint32_t cpt2 = ((uint32_t) x2) << 16;
	LD WA, (X2)
	EXTS XWA
	SLA 16, XWA
	LD (CPT2), XWA

POLYGON_RASTER_LOOP:
	DEC 2, (POLYGON_NUM_POINTS)

	CP (POLYGON_NUM_POINTS), 0
	JP Z, end_of_fillPolygon		; 	if (m_polygon.numPoints == 0) break;


	; 	int32_t step1 = calcStep(m_polygon.points[j + 1], m_polygon.points[j], h);
	PUSH XHL ; SAVE DRAW_FUNC_PTR
	PUSH XIX
	PUSH XIY
	
	; pt1 = j+1 / pt2 = j
	LD XIX, XIY
	INC 4, XIX
	LD XHL, STEP1
	CALL calcStep

	POP XIY
	POP XIX

	; 	int32_t step2 = calcStep(m_polygon.points[i - 1], m_polygon.points[i], h);
	PUSH XIX
	PUSH XIY

	; pt1 = i-1 / pt2 = i
	LD XIY, XIX
	DEC 4, XIX
	LD XHL, STEP2
	CALL calcStep

	LD XIX, (CUR_PAGE_PTR_1)
	LD XHL, 0
	LD HL, (HLINEY)
	ADD HL, 20
	MUL XHL, 320
	ADD XIX, XHL
	LD (CUR_LINE), XIX

	POP XIY
	POP XIX
	POP XHL ; RESTORE DRAW_FUNC_PTR

	INC 4, XIX		; 	i++;
	DEC 4, XIY		; 	j--;

	LDW (CPT1_LOW), 07FFFh		; 	cpt1 = (cpt1 & 0xFFFF0000) | 0x7FFF;
	LDW (CPT2_LOW), 08000h		; 	cpt2 = (cpt2 & 0xFFFF0000) | 0x8000;

	CPW (POLYGON_H), 0
	JP Z, POLYGON_H_IS_ZERO
	
FOR_H_LOOP:			; for (; h != 0; --h)
	CPW (HLINEY), 0
	JP LT, AFTER_DRAWFUNC_CALL

	LD WA, (CPT1_HIGH)		; x1 = cpt1 >> 16;
	LD (X1), WA

	LD WA, (CPT2_HIGH)		; x2 = cpt2 >> 16;
	LD (X2), WA


	; if (x1 < 320 && x2 >= 0)
	CPW (X1), 320
	JP GE, AFTER_DRAWFUNC_CALL
	CPW (X2), 0
	JP LT, AFTER_DRAWFUNC_CALL

	;	if (x1 < 0) x1 = 0;
	CPW (X1), 0
	JP GE, X1_NOT_NEGATIVE	
	LDW (X1), 0
X1_NOT_NEGATIVE:

	;   if (x2 > 319) x2 = 319;
	CPW (X2), 319
	JP LE, X2_LESS_THAN_SCREEN_W
	LDW (X2), 319
X2_LESS_THAN_SCREEN_W:
	
	;(x1, x2, color);
	CALL (XHL) ; drawfunc

AFTER_DRAWFUNC_CALL:
	
	LD WA, (STEP1_LOW)
	ADD (CPT1_LOW), WA
	LD WA, (STEP1_HIGH)
	ADC (CPT1_HIGH), WA	; 	cpt1 += step1;
	
	LD WA, (STEP2_LOW)
	ADD (CPT2_LOW), WA
	LD WA, (STEP2_HIGH)
	ADC (CPT2_HIGH), WA	; 	cpt2 += step2;

	INCW (HLINEY)
	CPW (HLINEY), 199
	JP UGT, POLYGON_RASTER_LOOP	; if (m_hliney > 199) return;

	DECW (POLYGON_H)
	JP NZ, FOR_H_LOOP

	JP POLYGON_RASTER_LOOP

POLYGON_H_IS_ZERO:
	LD WA, (STEP1_LOW)
	ADD (CPT1_LOW), WA
	LD WA, (STEP1_HIGH)
	ADC (CPT1_HIGH), WA	; 	cpt1 += step1;
	
	LD WA, (STEP2_LOW)
	ADD (CPT2_LOW), WA
	LD WA, (STEP2_HIGH)
	ADC (CPT2_HIGH), WA	; 	cpt2 += step2;

	JP POLYGON_RASTER_LOOP

end_of_fillPolygon:
	POP BC
	RET


calcStep:
	; Inputs:
	; XHL = ptr to STEP_1 or STEP_2
	; XIX = p1
	; XIY = p2

	PUSHW 4000h		; uint16_t v = 0x4000;

	LD WA, (XIY)
	LD (DX), WA		; dx = p2.x

	LD WA, (XIX)
	SUB (DX), WA		; dx -= p1.x;

	INC 2, XIX
	INC 2, XIY
	
	LD WA, (XIY)
	LD (POLYGON_H), WA		; dy = p2.y

	LD WA, (XIX)
	SUB (POLYGON_H), WA		; dy -= p1.y

	CPW (POLYGON_H), 0
	JP LE, POLYGON_H_IS_LE_ZERO  ; if (dy>0)
	
	LD WA, (XSP)
	EXTS XWA
	LD DE, (POLYGON_H)
	DIV XWA, DE
	LD (XSP), WA			; v = 0x4000/(POLYGON_H)

POLYGON_H_IS_LE_ZERO:
	
	LD WA, (DX)
	EXTS XWA

	LD DE, (XSP)		; v
	MULS XWA, DE
	SLA 2, XWA
	LD (XHL), XWA		; return dx * v * 4

	INC 2, XSP
	RET


readAndDrawPolygonHierarchy:
	LD D, 0
	LD E, C ; zoom
	;	pt.x -= m_polygonData[m_data_offset++] * zoom / DEFAULT_ZOOM;
	LD WA, 0
	LD A, (XIX)
	INC XIX
	LD HL, (XSP + 6)
	MUL XWA, DE			; PT.X *= ZOOM
	SRA 6, WA			; PT.X /= default_zoom (40h)
	SUB HL, WA
	LD (XSP + 6), HL

	;	pt.y -= m_polygonData[m_data_offset++] * zoom / DEFAULT_ZOOM;
	LD WA, 0
	LD A, (XIX)
	INC XIX
	LD HL, (XSP + 4)
	MUL XWA, DE			; PT.Y *= ZOOM
	SRA 6, WA			; PT.Y /= default_zoom (40h)
	SUB HL, WA
	LD (XSP + 4), HL

	ld B, 0
	LD C, (XIX)		; num_children - 1
	INC XIX
	INC BC			; BC = num_children

children_loop:
	LD WA, (XIX); offset 
	INC 2, XIX
	EX W, A
	PUSH WA
	PUSHW (XSP + 8); po.x = pt.x
	PUSHW (XSP + 8); po.y = pt.y

	; po.x += m_polygonData[m_data_offset++] * zoom / DEFAULT_ZOOM;
	LD WA, 0
	LD A, (XIX)
	INC XIX
	LD HL, (XSP + 2)
	MUL XWA, DE			; PO.X *= ZOOM
	SRA 6, WA			; PO.X /= default_zoom (40h)
	ADD HL, WA
	LD (XSP + 2), HL
	
	; po.y += m_polygonData[m_data_offset++] * zoom / DEFAULT_ZOOM;
	LD WA, 0
	LD A, (XIX)
	INC XIX
	LD HL, (XSP)
	MUL XWA, DE			; PO.Y *= ZOOM
	SRA 6, WA			; PO.Y /= default_zoom (40h)
	ADD HL, WA
	LD (XSP), HL

	PUSH DE			; save zoom
	LD DE, 0FFh				; uint16_t color = 0xFF;
	LD HL, (XSP + 6) ;offset
	AND HL, 8000h
	JP Z, OFFSET_BIT15_NOT_SET
	LD DE, 0
	LD E,(XIX)				; 	color = m_polygonData[m_data_offset++] & 0x7F;
	AND E, 7Fh
	INC 2, XIX				; 	m_data_offset++; //and waste a byte...
OFFSET_BIT15_NOT_SET:

	PUSH XIX			;	 uint16_t backup = m_data_offset;
	LD XIX, 0
	LD IX, (XSP + 0Ah) ;offset
	SLA 1, IX		 ; m_data_offset = (offset & 0x7FFF) * 2;
	ADD XIX, INTRO_VIDEO_1
	
	LD HL, DE
	; here L is the computer new color
	; and BC is the children loop counter

	; BC needs to become color | zoom params and
	; DE needs to become x param
	; to the readAndDrawPolygon routine
	
	LD DE, (XSP + 4)	; restore zoom

	PUSH BC
	LD B, L		; color
	LD C, E		; zoom
	LD DE, (XSP + 0ah)		; PO.x
	LD HL, (XSP + 08h)		; PO.y
	; (color, zoom, po)
	CALL readAndDrawPolygon
	POP BC
	POP XIX				; m_data_offset = backup;
	POP DE		; restore zoom

	INC 6, XSP ; local vars offset, po.x, po.y
	DJNZ BC, children_loop
	RET


LOAD_SCREEN:
; todo: ACTUALLY SELECT BITMAP VIA screen_id

	LD XDE, PAGE_BITMAP_0 + 20*320	; vga memory, skipping the first 20 lines because the image has 320x200 resolution and the screen has 320x240
	LD XBC, 320 * 200 / 2		; bitmap data length in 16-bit words (320x200 pixels)
	LDIRW
	RET

SETUP_PALETTE:
	LD BC,0
	LD XDE, 01703c8h		; VGA 3c8 port (select color palette index
	LD (XDE), C

	LD BC, 2*16							; data length: 16 colors, 4 bits per component
	LD XDE, 01703c9h					; VGA 3c9 port (for setting the color palette values: r, g and b)
	LD XHL, INTRO_PALETTES
	SLA 5, XWA
	ADD XHL, XWA

PALETTE_LOOP:
	; red
	LD A, (XHL)
	ANDB A, 0Fh
	LD (XDE), A
	INC XHL

	; green
	LD A, (XHL)
	ANDB A, 0Fh
	LD (XDE), A

	; blue
	LD A, (XHL)
	SRA 4, A
	LD (XDE), A
	INC XHL

	DJNZ BC, PALETTE_LOOP
	RET


DRAW_STRING:
; WA: stringId
; DE: x
; HL: y
; B: color

	DEC DE
	SLA 3, DE 	;	x = 8 * (x-1);
	LDW (STRING_X0), DE	;	uint16_t x0 = x;
	LD XIX, STRING_INDEX
	EXTS XWA
	ADD XIX, XWA
	ADD XIX, XWA
	LD IX, (XIX)
	EXTS XIX
	ADD XIX, STRING_DATA
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

	LD XIX, (CUR_PAGE_PTR_1)
	ADD XHL, 20
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


GET_PAGE_PTR:
	; A: pageId

	CP A, 0FFh
	JP NE, PAGEID_NOT_FF
	LD XWA, (CUR_PAGE_PTR_3)
	RET

PAGEID_NOT_FF:
	CP A, 0FEh
	JP NE, PAGEID_NOT_FE
	LD XWA, (CUR_PAGE_PTR_2)
	RET

PAGEID_NOT_FE:
	CP A, 3
	JP UGT, PAGEID_OTHER_VALUE
	AND WA, 3   ; 10000h bytes per page = enough for 320x200 pixels
	LD QWA, WA
	LD WA, 0
	ADD XWA, PAGE_BITMAP_0
	RET

PAGEID_OTHER_VALUE:
	LD XWA, PAGE_BITMAP_0
	RET


; These off-screen video pages are stored
; on the SRAM of the HDAE5000 extension card:

PAGE_BITMAP_0 EQU 240000h
PAGE_BITMAP_1 EQU 250000h
PAGE_BITMAP_2 EQU 260000h
PAGE_BITMAP_3 EQU 270000h


SELECT_VIDEO_PAGE:
	; A: pageId
	CALL GET_PAGE_PTR
	LD (CUR_PAGE_PTR_1), XWA
	RET



EXECUTE_INSTRUCTION:
	PUSH XIX
	PUSH XIY

;	LD WA, 0
;	LD A, (CURRENT_THREAD)
;	SLA WA, 2
;	EXTS XWA
;	ADD XWA, THREADS_DATA
;	; Thread* current = &m_threads[m_currentThread];

	LD IX, (PC)
	EXTS XIX
	ADD XIX, INTRO_BYTECODE			; FIXME

FETCH_OPCODE:
	LD A, (XIX)  ; opcode = fetch_byte();
	INC XIX

	BIT 7, A	; if (opcode & 0x80)
	JP Z, OPCODE_BIT_7_NOT_SET
;
;		uint16_t offset = ((opcode << 8) | fetch_byte()) * 2;
;
;		m_useVideo2 = false;
;		int16_t x = fetch_byte();
;		int16_t y = fetch_byte();
;		int16_t h = y - 199;
;		if (h > 0)
;		{
;			y = 199;
;			x += h;
;		}
;
;		// This switches the polygon database to "cinematic" and probably
;		// draws a black polygon over all the screen.
;		((another_world_vm_state*) owner())->setDataBuffer(CINEMATIC, offset);
;		((another_world_vm_state*) owner())->readAndDrawPolygon(COLOR_BLACK, DEFAULT_ZOOM, VMPoint(x,y));

	JP _end_of_EXECUTE_INSTRUCTION

OPCODE_BIT_7_NOT_SET:
	BIT 6, A	; if (opcode & 0x40)
	JP Z, OPCODE_BIT_6_NOT_SET
;
;		int16_t x, y;
;		uint16_t offset = fetch_word() * 2;
;		x = fetch_byte();
;
;		m_useVideo2 = false;
;
;		if (!(opcode & 0x20))
;		{
;			if (!(opcode & 0x10))
;				x = (x << 8) | fetch_byte();
;			else
;				x = read_vm_variable(x);
;		}
;		else
;		{
;		    if (opcode & 0x10)
;		        x += 0x100;
;		}
;
;		y = fetch_byte();
;
;		if (!(opcode & 8))
;		{
;			if (!(opcode & 4))
;				y = (y << 8) | fetch_byte();
;			else
;				y = read_vm_variable(y);
;		}
;
;		uint16_t zoom = 0x40;
;
;		switch (opcode & 0x03)
;		{
;			case 0:
;				zoom = 0x40;
;				break;
;			case 1:
;				zoom = read_vm_variable(fetch_byte());
;				break;
;			case 2:
;				fetch_byte();
;				break;
;			case 3:
;				m_useVideo2 = true;
;				zoom = 0x40;
;				break;
;		}
;
;		((another_world_vm_state*) owner())->setDataBuffer(m_useVideo2 ? VIDEO_2 : CINEMATIC, offset);
;		((another_world_vm_state*) owner())->readAndDrawPolygon(COLOR_BLACK, zoom, VMPoint(x, y));

	JP _end_of_EXECUTE_INSTRUCTION

OPCODE_BIT_6_NOT_SET:

	CP A, 0
	JP NE, INSTRUCTION_IS_NOT_MOVCONST
	; ====  MOV CONST instruction  ====
	LD WA, 0
	LD A, (XIX)		; uint8_t variableId = fetch_byte();
	INC XIX
	LD DE, (XIX)	; int16_t value = fetch_word();
	INC 2, XIX
	LD XIY, VM_VARIABLES
	SLA 1, WA
	EXTS XWA
	ADD XIY, XWA
	LD (XIY), DE	; write_vm_variable(variableId, value);
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_MOVCONST:

	CP A, 1
	JP NE, INSTRUCTION_IS_NOT_MOV
	; ====  MOV instruction  ====
	LD WA, 0
	LD A, (XIX)		; uint8_t dstVariableId = fetch_byte();
	INC XIX
	PUSH WA
	LD WA, 0
	LD A, (XIX)		; uint8_t srcVariableId = fetch_byte();
	INC XIX
	LD XIY, VM_VARIABLES
	SLA 1, WA
	EXTS XWA
	ADD XIY, XWA
	LD DE, (XIY)	; value = read_vm_variable(srcVariableId);
	POP WA
	LD (XIY), DE	; write_vm_variable(dstVariableId, value);	
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_MOV:

	CP A, 2
	JP NE, INSTRUCTION_IS_NOT_ADD
	; ====  ADD instruction  ====
	LD WA, 0
	LD A, (XIX)		; uint8_t dstVariableId = fetch_byte();
	INC XIX
	PUSH WA
	LD WA, 0
	LD A, (XIX)		; uint8_t srcVariableId = fetch_byte();
	INC XIX
	LD XIY, VM_VARIABLES
	SLA 1, WA
	EXTS XWA
	ADD XIY, XWA
	LD DE, (XIY)	; value = read_vm_variable(srcVariableId);
	POP WA
	LD (XIY), DE	; write_vm_variable(dstVariableId, value);	
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_ADD:

	CP A, 3
	JP NE, INSTRUCTION_IS_NOT_ADD_CONST
	; ====  ADD CONST instruction  ====
	LD WA, 0
	LD A, (XIX)		; uint8_t variableId = fetch_byte();
	INC XIX
	LD XIY, VM_VARIABLES
	SLA 1, WA
	EXTS XWA
	ADD XIY, XWA
	LD WA, (XIX)	; int16_t value = fetch_byte();
	INC 2, XIX
	ADDW (XIY), WA	; vm_variable += value;	
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_ADD_CONST:

	CP A, 4
	JP NE, INSTRUCTION_IS_NOT_CALL
	; ====  CALL subroutine instruction  ====
	LD WA, (XIX)		; uint16_t address;
	EX W, A
	INC 2, XIX
	LD XIY, (VM_STACK_POINTER)
	LD DE, (PC)
	inc 2, DE
	LD (XIY), DE		; push current program counter to VM stack
	INC 2, XIY
	LD (VM_STACK_POINTER), XIY
	LD (PC), WA
	POP XIY
	POP XIX
	RET
INSTRUCTION_IS_NOT_CALL:

	CP A, 5
	JP NE, INSTRUCTION_IS_NOT_RET
	; ====  RET instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_RET:

	CP A, 6
	JP NE, INSTRUCTION_IS_NOT_PAUSE_THREAD
	; ====  PAUSE_THREAD instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_PAUSE_THREAD:

	CP A, 7
	JP NE, INSTRUCTION_IS_NOT_JUMP
	; ====  JUMP instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_JUMP:

	CP A, 8
	JP NE, INSTRUCTION_IS_NOT_SET_VECT
	; ====  SET_VECT instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_SET_VECT:

	CP A, 9
	JP NE, INSTRUCTION_IS_NOT_DJNZ
	; ====  DJNZ instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_DJNZ:

	CP A, 0Ah
	JP NE, INSTRUCTION_IS_NOT_COND_JUMP
	; ====  COND_JUMP instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_COND_JUMP:

	CP A, 0Bh
	JP NE, INSTRUCTION_IS_NOT_SET_PALETTE
	; ====  SET_PALETTE instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_SET_PALETTE:

	CP A, 0Ch
	JP NE, INSTRUCTION_IS_NOT_RESET_THREAD
	; ====  RESET_THREAD instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_RESET_THREAD:

	CP A, 0Dh
	JP NE, INSTRUCTION_IS_NOT_SELECT_VIDEO_PAGE
	; ====  SELECT_VIDEO_PAGE instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_SELECT_VIDEO_PAGE:

	CP A, 0Eh
	JP NE, INSTRUCTION_IS_NOT_FILL_VIDEO_PAGE
	; ====  FILL_VIDEO_PAGE instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_FILL_VIDEO_PAGE:

	CP A, 0Fh
	JP NE, INSTRUCTION_IS_NOT_COPY_VIDEO_PAGE
	; ====  COPY_VIDEO_PAGE instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_COPY_VIDEO_PAGE:

	CP A, 10h
	JP NE, INSTRUCTION_IS_NOT_BLIT_FRAMEBUFFER
	; ====  BLIT_FRAMEBUFFER instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_BLIT_FRAMEBUFFER:

	CP A, 11h
	JP NE, INSTRUCTION_IS_NOT_KILL_THREAD
	; ====  KILL_THREAD instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_KILL_THREAD:

	CP A, 12h
	JP NE, INSTRUCTION_IS_NOT_DRAW_STRING
	; ====  DRAW_STRING instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_DRAW_STRING:

	CP A, 13h
	JP NE, INSTRUCTION_IS_NOT_SUB
	; ====  SUB instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_SUB:

	CP A, 14h
	JP NE, INSTRUCTION_IS_NOT_AND
	; ====  AND instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_AND:

	CP A, 15h
	JP NE, INSTRUCTION_IS_NOT_OR
	; ====  OR instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_OR:

	CP A, 16h
	JP NE, INSTRUCTION_IS_NOT_SHL
	; ====  SHL instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_SHL:

	CP A, 17h
	JP NE, INSTRUCTION_IS_NOT_SHR
	; ====  SHR instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_SHR:

	CP A, 18h
	JP NE, INSTRUCTION_IS_NOT_PLAY_SOUND
	; ====  PLAY_SOUND instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_PLAY_SOUND:

	CP A, 19h
	JP NE, INSTRUCTION_IS_NOT_LOAD
	; ====  LOAD instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_LOAD:

	CP A, 1Ah
	JP NE, INSTRUCTION_IS_NOT_PLAY_MUSIC
	; ====  PLAY_MUSIC instruction  ====
	; Implement-me!
	JP _end_of_EXECUTE_INSTRUCTION
INSTRUCTION_IS_NOT_PLAY_MUSIC:
		
_end_of_EXECUTE_INSTRUCTION:
	SUB XIX, INTRO_BYTECODE
	LD (PC), IX
	POP XIY
	POP XIX
	RET


BITMAP_FONT:
	binclude "hardcoded_data/anotherworld_chargen.rom"

STRING_INDEX:
	binclude "hardcoded_data/str_index.rom"

STRING_DATA:
	binclude "hardcoded_data/str_data.rom"

INTRO_BYTECODE:
	binclude "resources/resource-0x18.bin"

INTRO_PALETTES:
	binclude "resources/resource-0x17.bin"  ; intro

INTRO_VIDEO_1:
	binclude "resources/resource-0x19.bin"

BITMAP_1:
	binclude "another_world_logo.bin"
BITMAP_2:
	binclude "other_bitmap.bin"
	
	org 02fffffh
	db 0ffh
