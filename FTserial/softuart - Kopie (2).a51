; #########   SUART = Software UART 8051 / Cypress / Keil uVision ##################

; this cannot be done in C, i tried it.

; project requires the startup.a51 from the keil lib for the startup.code. This is linked automaticly by Keil compiler.

; make shure Portpins are set to correct direction in your C init.

txd_pin	EQU	0xB0.1		;Transmit on this pin. Port B or Port D or Port A!!
rxd_pin	EQU	0xB0.3		;Receive on this pin
 
; bit-addressable operations on portpins: setb, clr, cpl(toggle)....   8051 very good at bits!
;Formula to calculate the bit time delay constant for normal 8051: (found on the internet)
;This constant is calculated as: (((Fosci/baud)/12) - 5) / 2         (the -5 ?? maybe some rounding stuff??)
;Fosci is the frequency of CPU-clock in Hz
;baud is required baudrate

;	
; normal 8051 takes 12 clock_cycles per instruction_cycle. 
; the Cypress FX2 only takes 4 clock_cycles per instruction_cycle.!
; So the time for one instruction_cycle is: 1/(Foscillator/4) = 83 nanosecs
; the various instructions may take  more than one instruction-cycle.

; This for FX2:
; Formular: TimeValue = (Foscillator/4/Baudrate)/Num-Instruction_cycles-timeloop(Bittime).   
; TimeLoop:
; 
; Number of Instruction_cycles: (DJNZ Rn, rel)=3;-this is used for the timeloop!!,  (JB bit, rel)=4;
;
; Cypress FX2 at clock =48Mhz:  (48000000/4/40) / Baudrate  = 300000 / Baudrate = BITTIM-Value
 
; FORMAT: fixed 8N1
; LIMITS: the BITTIM is loaded into an 8Bit Register, so 255 is max!! This limits the lower end if the Baudrate.
; Baudrate table at 40 cycles: BITTIM
; 1200 = 250 
; 2400 = 125
; 4800 = 62,5
; 9600 = 31,2
; 19200 = 15,6
; 38400 = 7,8
; 57600 = 5,2
; 115200 = 2,6

BITTIM	EQU	31  ; the Bit-time for selected Baudrate (in CYCLES * (1/(48000000/4))
CYCLES EQU 40   ; the number of machine-cycles used to calculate BITTIM-value


; this makes assembly code relocatable to be linked with c files.(by defining an own code segment): 
softuart   SEGMENT CODE
        
                RSEG    softuart   ; switch to this code segment

                USING   0               ; state register_bank used
                                        ; for the following program code.  
 
;----------------- TRANSMIT ---------------------------
;
;to call from C programs.
;Protoype definition in C file :
;		extern void put_uart(unsigned char);
;
;Usage Assembly:
;	data to be send has to be moved to R7
;	for example:
;		mov R7,#'a'
;		lcall _put_uart
; uses a, R0,R1
;--------------------------------------------

PUBLIC _put_uart

_put_uart:
	push ACC
	Push PSW

	
	mov a,r7
	CLR txd_pin			;Drop line for startbit = low for 1 bittime
	MOV R0,#BITTIM		;Wait full bit-time
	acall timeloop	
	
	MOV R1,#8			;Send 8 bits
putc1:
	RRC A				;shift accu into carry
	MOV txd_pin,C		;Write next bit which is in carry
	MOV R0,#BITTIM		;Wait full bit-time
	acall timeloop	
	DJNZ R1,putc1		; 8 bits loop
	
	SETB txd_pin		;Set line high for the Stopbit
	RRC A				;Restore ACC contents
	MOV R0,#BITTIM		;Wait full bit-time. ie. send stopbit
	acall timeloop	
	
	POP PSW
	pop ACC
	RET
 
;----------------- RECEIVE --------------------------
;
;to call from C programs.
;Protoype definition in C file :
;
;	extern unsigned char get_uart(void);
;
; there is no errorchecking if format is not 8N1 or baudrate is wrong. in this case garbage is returned.!
; returns only if at least 1 linetoggle appears !! so may wait forever!
;Usage Assembly:
;		lcall get_uart
;	Return:
;		data received is returned in R7
; uses a, R0,R1
;--------------------------------------------
PUBLIC get_uart

get_uart:	
	Push ACC
	Push PSW
	
; check about 16ms for a startbit. if not, return 0xff
	mov a,#0xff
	mov R0,a
	mov R1,a
gets1:	
	JNB rxd_pin,getstart		;check if line is low
	djnz R0,gets1   ; if its 0, it will rollover to 0xff
	djnz R1,gets1
	; failed, return 0xff which is still in accu
	sjmp getret

getstart:	
	MOV R0,#BITTIM/2	;delay 1/2 bit-time
	acall timeloop	

	MOV R1,#8			;Read in 8 bits, we are now in the middle of the startbit. we always want to sample in the middle;)
getc1:
	MOV R0,#BITTIM		;Wait full bit-time, again hoping to land in the middle of a bit
	acall timeloop	
	MOV C,rxd_pin		;Read bit. 0=0,1=1, easy..into carry
	RRC A				;Shift carry into ACC
	DJNZ R1,getc1		;read 8 bits
	; skip the last stopbit
getret:	
	mov r7,a
	POP PSW
	pop ACC
	RET					;return
	
	
; subroutine timeloop. delays for exactly CYCLES machine cycles times count
; parameter: R0 = timevalue/count
; we need to cover CYCLES amount of machine-cycles per stepcount in R0
; uses  R0,R2
timeloop:
; the first CYCLES  is done manually to include the acall and ret and MOV insrtructions
; acall = 3 + ret = 4  + MOV Rn, #data = 2   = 9
OVERHEAD equ CYCLES-15 ; must be divby-3: (9+2+3)
	mov R2,#OVERHEAD  ; 2 

	DJNZ R2, $ ; 3
	DJNZ R0, time1 ; 3
	sjmp timee  ; already zero...return (count in R0 was 1)
	
	 ; loop exactly CYCLES  per count in R0
time1:
	mov R2,#CYCLES/3  ; 2
	djnz R2,$ ; 3
	DJNZ R0, time1  ; 3	 

timee:
ret	
	
	END