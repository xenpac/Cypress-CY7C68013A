; #########   SUART = Software UART 8051 / Cypress / Keil uVision ##################

; make shure Portpins are set to correct direction.

; The problem with this driver is: If you send a string nonstop, "get_uart" function cant keep up, so receives garbage!!!!!!!!!!!!!!!!!!!!!
; It works perfectl (from 1200 to 115200 baud) if you just type on the keyboard and echo, but response strings from the computer get garbled!
; maybe thats all what software uart can do ?! #########################################
; i havent tried interrupt yet! maybe int0 as rxin.

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
; This for FX2:
; Formular: ((Foscillator/4/Baudrate)-5)/Instruction_cycles.    The 5 ?? dont know, some rounding stuff(small effect)
; Instruction_cycles: (DJNZ Rn, rel)=3;-this is used for the timeloop!!
;
; Cypress FX2 at clock =48Mhz:  ((48000000/4/Baudrate) - 5) / 3  = BITTIM-Value


; FORMAT: 8N1
; LIMITS: the BITTIM is loaded into a 8Bit Register, so 255 is max!! This limits the lower end if the Baudrate.
; Baudrate table: BITTIM
; VALH and HAFH must be set to highbyte! the formula is not accurate for 16bit value, found by experiment.!!!!!!!!!!!!!!!!!!!
; 1200 = 3630	;ok was 3331 factor 1,089
; 2400 = 1910  ;ok  was 1665 factor 1,14
; 4800 = 1085  ;ok  was 831 factor 1,3
; 9600 = 669   ; ok   was 415  factor 1,61
; VALH and HAFH must be 1 for below, 8Bit Values!
; 19200 = 206   
; 38400 = 102
; 57600 = 68
; 115200 = 33    maximum value!!

BITTIM	EQU	3630

; if BITTIM > 0xff
VALH equ BITTIM>>8
; else
//VALH equ 1
VALL equ BITTIM&0xff
; if BITTIM > 0xff
HAFH equ (BITTIM/2)>>8
; else
//HAFH equ 1
HAFL equ (BITTIM/2)&0xff


; this makes assembly code relocatable to be linked with c files.: 
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
;--------------------------------------------
//RSEG ?SU?PUT_UART SEGMENT CODE
PUBLIC _put_uart
_put_uart:
	push ACC
	Push PSW
	mov a,r7
	CLR txd_pin			;Drop line for startbit = low for 1 bittime
	
	mov R2,#VALH
	MOV R0,#VALL		;Wait full bit-time, startbit
L1:	
	DJNZ R0,$			
	DJNZ R2,L1			
	
	MOV R1,#8			;Send 8 bits
putc1:
	RRC A				;shift next bit into carry
	MOV txd_pin,C		;Write next bit
	
	
	mov R2,#VALH
	MOV R0,#VALL		;Wait full bit-time, databit
L2:	
	DJNZ R0,$			
	DJNZ R2,L2			
	;
	DJNZ R1,putc1		;write 8 bits
	SETB txd_pin		;Set line high for the Stopbit
	RRC A				;shift next bit into carry
	
	mov R2,#VALH
	MOV R0,#VALL		;Wait full bit-time, final stopbit
L3:	
	DJNZ R0,$			
	DJNZ R2,L3			
	
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
	mov R2,#HAFH
	MOV R0,#HAFL		;Wait half bit-time
L4:	
	DJNZ R0,$			 ; if its 0, it will rollover to 0xff next turn
	DJNZ R2,L4			

	MOV R1,#8			;Read in 8 bits, we are now in the middle of the startbit. we always want to sample in the middle;)
getc1:

	mov R2,#VALH
	MOV R0,#VALL		;Wait full bit-time
L5:	
	DJNZ R0,$			
	DJNZ R2,L5			
	
	MOV C,rxd_pin		;Read bit. 0=0,1=1, easy..into carry
	RRC A				;Shift carry into ACC
	DJNZ R1,getc1		;read 8 bits
	; skip the last stopbit
getret:	
	mov r7,a
	POP PSW
	pop ACC
	RET					;return
	
	END