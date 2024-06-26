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

CINEMATIC_138							EQU 0CEFAh
CINEMATIC_139							EQU 0CF0Ah
CINEMATIC_140							EQU 0CEA6h
CINEMATIC_141							EQU 0CE8Eh
CINEMATIC_142							EQU 0CE76h
CINEMATIC_143							EQU 0CECEh
CINEMATIC_CARKEY						EQU 0F6D2h
CINEMATIC_CARKEY_BACKGROUND				EQU 0F6C6h
CINEMATIC_WALKING_FEET_ARRIVING_0		EQU 00F72h
CINEMATIC_WALKING_FEET_ARRIVING_1		EQU 00F7Eh
CINEMATIC_WALKING_FEET_ARRIVING_2		EQU 00FA2h
CINEMATIC_WALKING_FEET_ARRIVING_3		EQU 00FCAh
CINEMATIC_WALKING_FEET_ARRIVING_4		EQU 00FF2h
CINEMATIC_WALKING_FEET_ARRIVING_5		EQU 0101Ah
CINEMATIC_WALKING_FEET_ARRIVING_6		EQU 01048h
CINEMATIC_WALKING_FEET_ARRIVING_7		EQU 0105Eh
CINEMATIC_WALKING_FEET_ARRIVING_8		EQU 01082h
CINEMATIC_WALKING_FEET_ARRIVING_9		EQU 010B6h
CINEMATIC_WALKING_FEET_ARRIVING_10		EQU 010E2h
CINEMATIC_WALKING_FEET_ARRIVING_11		EQU 0110Ah
CINEMATIC_WALKING_FEET_ARRIVING_12		EQU 0113Eh
CINEMATIC_WALKING_FEET_ARRIVING_13		EQU 01158h
CINEMATIC_WALKING_FEET_ARRIVING_14		EQU 0117Eh
CINEMATIC_WALKING_FEET_ARRIVING_15		EQU 011ACh
CINEMATIC_WALKING_FEET_ARRIVING_16		EQU 011C6h
CINEMATIC_WALKING_FEET_ARRIVING_17		EQU 01200h
CINEMATIC_WALKING_FEET_ARRIVING_18		EQU 0122Ah
CINEMATIC_WALKING_FEET_ARRIVING_19		EQU 01278h
CINEMATIC_WALKING_FEET_ARRIVING_20		EQU 01292h
CINEMATIC_WALKING_FEET_ARRIVING_21		EQU 012ACh
CINEMATIC_WALKING_FEET_ARRIVING_22		EQU 012F2h
CINEMATIC_WALKING_FEET_ARRIVING_23		EQU 0130Ch
CINEMATIC_WALKING_FEET_ARRIVING_24		EQU 01326h
CINEMATIC_WALKING_FEET_ARRIVING_25		EQU 01340h
CINEMATIC_WALKING_FEET_ARRIVING_26		EQU 0135Ah
CINEMATIC_WALKING_FEET_ARRIVING_27		EQU 01374h
CINEMATIC_WALKING_FEET_ARRIVING_28		EQU 013EAh
CINEMATIC_WALKING_FEET_ARRIVING_29		EQU 01404h
CINEMATIC_WALKING_FEET_ARRIVING_30		EQU 0141Eh
CINEMATIC_WALKING_FEET_ARRIVING_31		EQU 0143Ch
CINEMATIC_WALKING_FEET_ARRIVING_32		EQU 0145Ah
CINEMATIC_WALKING_FEET_ARRIVING_33		EQU 01478h
CINEMATIC_WALKING_FEET_ARRIVING_34		EQU 014F6h
CINEMATIC_WALKING_FEET_ENTERING_0		EQU 01514h
CINEMATIC_WALKING_FEET_ENTERING_1		EQU 01532h
CINEMATIC_WALKING_FEET_ENTERING_2		EQU 01584h
CINEMATIC_WALKING_FEET_ENTERING_3		EQU 015B6h
CINEMATIC_WALKING_FEET_ENTERING_4		EQU 015D0h
CINEMATIC_WALKING_FEET_ENTERING_5		EQU 015EAh
CINEMATIC_WALKING_FEET_ENTERING_6		EQU 01658h
CINEMATIC_WALKING_FEET_ENTERING_7		EQU 0166Ah
CINEMATIC_WALKING_FEET_ENTERING_8		EQU 016A2h
CINEMATIC_WALKING_FEET_ENTERING_9		EQU 016EAh
CINEMATIC_WALKING_FEET_ENTERING_10		EQU 01712h

	ORG 0200000h

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

MAIN_LOOP:
	LD XWA, 03h
	CALL SETUP_PALETTE
	LD XHL, BITMAP_1
	CALL LOAD_SCREEN
	CALL BREAK

	CALL LONG_PAUSE

	LD XWA, 0Bh
	CALL SETUP_PALETTE
	video 1, CINEMATIC_138, 322, 145
	video 1, CINEMATIC_139, 322, 59
	video 1, CINEMATIC_140, 17, 100
	video 1, CINEMATIC_141, 322, 100
	video 1, CINEMATIC_142, 65534, 99
	video 1, CINEMATIC_143, 160, 108
	video 1, CINEMATIC_CARKEY_BACKGROUND, 82, 82
	video 1, CINEMATIC_CARKEY, 160, 100
	text 0192h, 22, 16, 5	; "IDENTIFICATION"
	text 0191h, 3, 168, 0Bh ; "I see you have driven here in your\nFerrari."

	CALL BREAK

	CALL LONG_PAUSE

	LD XWA, 04h
	CALL SETUP_PALETTE

	video 1, CINEMATIC_WALKING_FEET_ARRIVING_1, 230, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_2, 230, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_3, 230, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_4, 229, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_5, 229, 97
;	play id=0x003C, freq=0x14, vol=0x3F, channel=0x00
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_6, 230, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_7, 229, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_8, 230, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_9, 231, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_10, 230, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_11, 231, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_12, 231, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_13, 232, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_14, 232, 97
;	play id=0x003C, freq=0x0A, vol=0x3F, channel=0x00
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_15, 231, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_16, 232, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_17, 232, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_18, 231, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_19, 230, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_20, 226, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_21, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_22, 230, 96
	CALL BREAK
;	play id=0x003C, freq=0x0F, vol=0x0F, channel=0x02
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_23, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_24, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_25, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_26, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_27, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_28, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_29, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_30, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_31, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_32, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_33, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ARRIVING_34, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_0, 230, 96
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_1, 125, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_2, 125, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_3, 125, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_4, 125, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_5, 125, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_6, 125, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_7, 125, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_8, 125, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_9, 125, 98
;	play id=0x003C, freq=0x0A, vol=0x14, channel=0x02
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_10, 125, 98
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_10, 94, 97
	CALL BREAK
	video 1, CINEMATIC_WALKING_FEET_ENTERING_10, 71, 115
	CALL BREAK

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
