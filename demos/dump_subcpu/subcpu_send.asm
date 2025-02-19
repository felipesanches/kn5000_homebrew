	cpu	96c141	; Actual CPU is 94c241f
	page	0
	maxmode	on

SC1BUF: EQU 0D4h

	ORG 01F765h

INTTX1_HANDLER:
	nop
	nop
	nop
	PUSH XHL
	push xde
	push xbc
	push xwa
	CP (01038h), 3     ; variable 0x1038 from the original subcpu payload
	JP NZ, end_of_inttx1_handler
	LD XBC, 01F7AEh	; dump_address
	LD XDE, (XBC)
	INC 4, XBC ; now ptr to num_bytes
	LD HL, (XBC) ; num_bytes
	LD A, (XBC+1)
	CP A, 0
	JP Z, end_of_inttx1_handler
    CP HL, 0
    JP NZ, not_finished_yet
    LD A, 0 ; indicate that dumping should stop

not_finished_yet:
	JP Z, end_of_inttx1_handler
    DEC 1, HL
    LD A, (XDE + HL) ;; dump a byte
    LD (SC1BUF), A 	; SC1BUF=D4
    LD (XBC), HL  ; num_bytes--;
    NOP
    NOP

end_of_inttx1_handler:
    POP XWA
    POP XBC
    POP XDE
    POP XHL
    RETI

;	1f7ab -> was RING_BUFFER_HAS_OVERRUN but its is OK to use

DUMP_ADDRESS: dd 0120E3h  ; we'll validate correctness of this method
                          ; by ensuring that we receive the string "KN5000 SOUND RAM"
                          ; which we know is located in this address of the
                          ; subcpu executable payload in RAM
NUM_BYTES: dw 16 ; This is the length of the known string.
DUMP_IT: db 1    ; dumping is triggered by any non-zero value here
MSG_INDEX: db 0  ; this variable is used for keeping track of the request message
                 ; bytes as they arrive to the subcpu via the serial port #1
                 ; ("COMPUTER INTERFACE" connection via a mini-din cable)

