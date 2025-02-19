	org 0x1f765
INTTX1_HANDLER:
	nop
	nop
	nop
	push xhl
	push xde
	push xbc
	push xwa
	CP (var_1038), 0x03
	JP NZ LABEL_1F7A9
	LD XBC, 0x1F7AE	; dump_address
	LD XDE, (XBC)
	INC 4, XBC ; now ptr to num_bytes
	LD HL, (XBC) ; num_bytes
	LD A, (XBC+1)
	CP A, 0
	JP Z, LABEL_1F7A9
    CP HL, 0
    JP NZ, not_finished_yet
    LD A, 0 ; indicate that dumping should stop

not_finished_yet:
	JP Z, LABEL_1F7A9
    DEC 1, HL
    LD A, (XDE + HL) ;; dump a byte
    LD (SC1BUF), A 	; SC1BUF=D4
    LD (XBC), HL  ; num_bytes--;
    NOP NOP
    POP XWA
    POP XBC
    POP XDE
    POP XHL
    RETI

;	1f7ab -> was RING_BUFFER_HAS_OVERRUN but its is OK to use
DUMP_ADDRESS: dd
NUM_BYTES: dw
DUMP_IT: db 0x1
MSG_INDEX: db 0

