	org 0x1f736

INTRX1_HANDLER:
	nop
	nop
	nop
	push xhl
	push xde
	push xbc
	push xwa
	LD E, (SC1BUF)
    LD XBC, 0x1f7ae	; dump_address
    LD XHL, 0x1f7b5 ; MSG_INDEX
    LD A, (XHL)
    EXTZ WA
    LD (XBC + WA), E
    INC 1, A
    CP A, 7
    JP NZ, END ; 0x01f760
    LD A, 0
END:
	LD (XHL), A ; MSG_INDEX
	JP T 1F7A9 ; Since this new routine is too big
	           ; we reuse the end of INTTX_HANDLER here.
