; #########   SUART = Software UART 8051 / Cypress / Keil uVision ##################
; timer version

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
; The above formular for the bittime would then be: (((Fosci/baud)/4) - 5) / 3 

; For FX2 we use the Timer0 for the Bittime duration measure.
; Timer0 is clocked at 4Mhz, so the resolution per count is: 250 nanosecs or 0,000 000 25 secs. 
; To set the 16Bit Timer, we substract the needed Bittime_clock_count from 0xffff, set the value and wait for the overflow flag.
; The needed Bittime_clock_count is: Timer0 frequency / 1/BitTimeValue = 4000000 / 1/BitTimeValue_

; BitTime    Baudrate      Frequency	 Timercount		Timerload-H     Timerload-L (-Byte)
; ==================================================================================================
; 3333탎 	300				300Hz			13333		0xcb		0xeb
; 833탎 	1200			1200Hz			3333		0xf2		0xfb
; 416탎 	2400			2403Hz			1664		0xf9		0x80
; 208탎 	4800			4807Hz			832			0xfc		0xc0
; 104탎 	9600			9615Hz			416			0xfe		0x60
; 69탎 		14400			14492Hz			276			0xfe		0xec
; 52탎 		19200			19230Hz			208			0xff		0x30
; 34탎 		28800			29411Hz			136			0xff		0x78
; 26탎 		38400			38461Hz			104			0xff		0x98
; 17.3탎 	57600			57803Hz			69			0xff		0xbb
; 8탎 		115200			125000Hz		32			0xff		0xe0
; 4.34탎 	230400 			230414Hz		17			0xff		0xef


; FORMAT: fixed 8N1

; using 9600
BITTIM equ 13333



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
	MOV R0,#1		;Wait full bit-time
	acall timeloop	
	
	MOV R1,#8			;Send 8 bits
putc1:
	RRC A				;shift accu into carry
	MOV txd_pin,C		;Write next bit which is in carry
	MOV R0,#1		;Wait full bit-time
	acall timeloop	
	DJNZ R1,putc1		; 8 bits loop
	
	SETB txd_pin		;Set line high for the Stopbit
	RRC A				;Restore ACC contents
	MOV R0,#1		;Wait full bit-time. ie. send stopbit
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
	MOV R0,#0	;delay 1/2 bit-time
	acall timeloop	

	MOV R1,#8			;Read in 8 bits, we are now in the middle of the startbit. we always want to sample in the middle;)
getc1:
	MOV R0,#1		;Wait full bit-time, again hoping to land in the middle of a bit
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
	




FULLBIT equ 65536 - (BITTIM/2)
HALFBIT equ 65536 - (BITTIM>>1)
FULLH equ FULLBIT>>8
FULLL equ FULLBIT&0xff
HALFH equ HALFBIT>>8
HALFL equ HALFBIT&0xff
	
; subroutine timeloop. for bittime delay
; parameter: R0 : 1 = Full bittime; 0=halfbittime;
; uses  R0,R2
; execution delay: 20*83ns = 1,7us
timeloop:
	clr tr0 ; 1; stop timer
	clr tf0 ; clear overflow flag
	mov TH0,#FULLH  ; 2
	mov TL0,#FULLL
	DJNZ R0, time1 ; 3
	mov TH0,#HALFH  ; 2
	mov TL0,#HALFL
time1:
	setb tr0  ;start timer
	jnb TF0,$	; wait for overflow
	ret	
	
	END