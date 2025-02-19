	cpu	96c141	; Actual CPU is 94c241f
	page	0
	maxmode	on

SC1BUF: EQU 0D4h

	ORG 01F736h

INTRX1_HANDLER:
	push xhl
	push xde
	push xbc
	push xwa
	LD E, (SC1BUF)
    LD XBC, DUMP_ADDRESS	; dump_address
    LD XHL, 01f7b5h ; MSG_INDEX
    LD A, (XHL)
    EXTZ WA
    LD (XBC + WA), E
    INC 1, A
    CP A, 7
    JP NZ, FOO ; 0x01f760
    LD A, 0
FOO:
	LD (XHL), A ; MSG_INDEX
	
	; !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	JP 01F7A9h ; Since this new routine is too big
	           ; we reuse the end of INTTX_HANDLER here.
	; !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

; !!!!
; TODO: THIS IS WRONG!!!!!! These vars must be shared between the two patches !!!!!!!
; !!!!
;
DUMP_ADDRESS: dd 0120E3h  ; we'll validate correctness of this method
                          ; by ensuring that we receive the string "KN5000 SOUND RAM"
                          ; which we know is located in this address of the
                          ; subcpu executable payload in RAM
NUM_BYTES: dw 16 ; This is the length of the known string.
DUMP_IT: db 1    ; dumping is triggered by any non-zero value here
MSG_INDEX: db 0  ; this variable is used for keeping track of the request message
                 ; bytes as they arrive to the subcpu via the serial port #1
                 ; ("COMPUTER INTERFACE" connection via a mini-din cable)
