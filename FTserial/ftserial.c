/* Serial to USB interface with CY7C68013A-Chip from Cypress: FT232R simulator

+++This firmware features:
- FT232R USB serial adapter simulating a chinese clone with serialno:    A50285BI  (The clone of a clone; just basic implementation, no eeprom)
- USB uses EP1IN, EP1OUT. FullSpeed Device.(HighSpeed possible)
- Software UART is implemented with Baudrates between 1200 and 115200. (see assemblerfile "softuart.a51")
As the CY7C68013A 56Pin package does not expose the 8051 serial UART Pins, we need to do a software UART with BitBang.
enumerates:
On Windows: uses ftdibus.sys or ftser2k.sys, USB serial Port(COMxx)
On Linux: ttyUSB0

++++++++++  CY7C68013A NOTES +++++++++++++++
+++Autovector:
 The Autovector interrupt is a special feature that is only available on the Cypress parts. No other 8051 devices has this feature. 
 The Autovector interrupt uses special hardware to modify the interrupt vector address. 
 The single Autovector interrupt location at address 43H serves all USB related interrupts like Setup Data Available (ISR_Sudav), 
 Start of Frame (ISR_Sof), and all endpoint interrupts.

To write interrupt functions for the Autovector interrupt Cypress uses the #pragma NOINTVECTOR directive. 
This suppresses the C compiler generated interrupt vectors. 
Therefore, all interrupts that are served by the Autovector interrupt have the same interrupt number. For example:

void ISR_Sudav(void) interrupt 0
void ISR_Sutok(void) interrupt 0
void ISR_Sof(void) interrupt 0

Cypress examples use autovector , WE DONT!!!  All USB-interrupts are served via interrupt 8.(its only SUDAV/SetupRequest)

+++ CY7C68013A ENDPOINTS:
EP0 = always control endpoint.
EP1IN/OUT = only accessable by CPU, has 64Byte buffers, is the only bidirectional EP, to be used for small stuff like serial adapter.
EP2,4,6,8 = are unidirectional only (either IN or OUT), very flexible, highspeed.


+++SYNCDELAY:
certain registers need a settling time.(see manual). There is a Keil-Macro for this. (see fx2sdly.h)

+++CY7C68013A memory map:
0 - 0x7F = internal register and bit-addressable Bits, direct addressing DATA. NOTE: BOOL is bit and indeed a bit variable on this chip.
0x80 - 0xff = RAM DATA/STACK indirect addressing IDATA
0x80 - 0xFF = SFR registers, direct addressing
"pseudo external" program/DATA memory being inside chip: CODE or XDATA:
	0 - 0x1FFF/3FFF = Program+Data RAM (8/16K). CY7C68013-A has 16K, the old one 8K
0xE00 - 0xE200 = 0.5K Data RAM
0xE200 - 0xFFFF = 7.5KB(data)  control/status registers and endpoint  buffers(4K at 0xF000 - 0xFFFF)



Jan-2021  Thomas Krueger Germany
*/

// place data items in order defined!!! we use it for the descriptor tables below.
#pragma ORDER

#define ALLOCATE_EXTERN   // this a special Keil define to format the following includefiles correctly!
#include "fx2.h"
#include "fx2regs.h"
#include "fx2sdly.h"


// Macros: Bit operations on byte/word/long variables. Format: bset(Bit,variable). works also on SFRs(creates sbi,cbi)
#define bset(x,y) (y |= (1 << x))
#define bclr(x,y) (y &= (~(1 << x)))
#define btst(x,y) (y & (1 << x))


//macro: byte swap for the descriptor WORD-items
#define BIGTOLITTLE(x)  ((x>>8)|(x<<8))
#define CT_HTONS(x)    (((x >> 8) & 0x00FF) | ((x << 8) & 0xFF00))

//protos:
void SetupCommand(void);
void Initialize(void);
bit vendorcommands(void);
BYTE receive(void);
void transmit(BYTE c);
void putcc(BYTE c);
BYTE getcc(void);
void getss(BYTE *buf);
void putss(const BYTE *ps);
void mainmenue(void);
void delay( WORD ms);
void latencytimer(void);
void putnumber(BYTE c);

//assembly functions:
extern void put_uart(unsigned char);
extern unsigned char get_uart(void);  // does not return until char received !!!!!!!!!!!!!!!!!!!!!

// ++++++    XDATA +++++++++++++++

// Descriptors:
#define VENDOR 	0x0403 
#define PRODUCT	0x6001 
#define VERSION	0x600

// MUST start at even address!! check linker mapfile
// insert dummy bytes if necessary
DEVICEDSCR xdata myDeviceDscr  =
{
    sizeof(DEVICEDSCR),		// Descriptor length
    DEVICE_DSCR,			// Descriptor type
    0,			// USB spec version 2.00
    0x02,
    0,				// Device class
    0,				// Device sub-class
    0,				// Device sub-sub-class
    64,				// Max packet size for EP0 (bytes). we have 64, but ftdi has 8
    BIGTOLITTLE(VENDOR), // Vendor ID (Future Technology Devices Intl). swap bytes!!
    BIGTOLITTLE(PRODUCT), // Product ID (FT8U100AX Serial Port)
    BIGTOLITTLE(VERSION), // Product version (1.00)
    1,				// Manufacturer string index
    2,				// Product string index
    3,				// Serial number string index
    1				// Number of configurations
};

// MUST start at even address!! see linker mapfile
DEVICEQUALDSCR  xdata myDeviceQualDscr   =
{
    sizeof(DEVICEQUALDSCR),	// Descriptor length
    DEVQUAL_DSCR,		// Descriptor type
    0,
    0x02,			// USB spec version 2.00 as a WORD: lsByte first
    0,				// Device class
    0,				// Device sub-class
    0,				// Device sub-sub-class
    64,				// Max packet size for EP0 (bytes)
    1				// Number of alternate configurations
};

// MUST start at even address!! check linker mapfile. This stuff is sent in one go alltogether!
BYTE xdata myConfigDscr[]  =
{
    sizeof(CONFIGDSCR),		// Descriptor length
    CONFIG_DSCR,			// Descriptor Type
    0x12,		// Total len of this array. Will be set in init function!!**************************
	0x34,
    1,				// Number of interfaces supported
    1,				// Configuration index for SetConfiguration()
    0,				// Config descriptor string index
    bmBUSPWR|bmRWU,		// Attributes 0xA0
    0x2d,				// Max power consumption; 90 mA

	// interface 0 descriptor Management CommunicationClass interface
    sizeof(INTRFCDSCR),		// Descriptor length
    INTRFC_DSCR,			// Descriptor type
    0,				// Index of this interface (zero based)
    0,				// Value used to select alternate
    2,				// Number of endpoints
    0xff,			// Class Code
    0xff,			// Subclass Code
    0xff,			// Protocol Code
    2,				// Index of interface string description
	

	//EP1 Descriptor bulk IN
	sizeof(ENDPNTDSCR),  ENDPNT_DSCR, 0x81, EP_BULK, 0x40, 0, 0,

//EP2 Descriptor bulk OUT
	sizeof(ENDPNTDSCR),  ENDPNT_DSCR, 0x01, EP_BULK, 0x40, 0, 0,
};


//languange ID
// MUST start at even address!! check linker mapfile
BYTE xdata string0[] =
{
    4,  // length of this array
    STRING_DSCR,
    0x09,
    0x04   // US languagne code
};

//manufacturer
// MUST start at even address!! check linker mapfile
BYTE xdata string1[] =
{
    10,  // length of this array
    STRING_DSCR,
    'F',00,
    'T',00,
    'D',00,
    'I',00,
};

//product
// MUST start at even address!! check linker mapfile
BYTE xdata string2[] =
{
    32,  // length of this array
    STRING_DSCR,
     'F',00,
     'T',00,
     '2',00,
     '3',00,
     '2',00,
     'R',00,
     ' ',00,
     'U',00,
     'S',00,
     'B',00,
     ' ',00,
     'U',00,
     'A',00,
     'R',00,
     'T',00,
};

//serial
BYTE xdata string3[] =
{
    18,  // length of this array
    STRING_DSCR,
    'A',00,
    '5',00,
    '0',00,
    '2',00,
    '8',00,
    '5',00,
    'B',00,
    'I',00,
};



// +++Data:
// we do not allow suspend/wakeup as processor never goes to sleep.
bit Rwuen_allowed = 0;	// DisAllow remote wakeup, this also means Suspend is not allowed
bit Rwuen = 0;		// Remote wakeup 0
bit Selfpwr = 0;		// Device is (not) self-powered

WORD ptr;
BYTE Configuration;  // Current set device configuration
BYTE Interface;      // Current set interface 
BYTE LatTimer;

#define RX PD3
#define TX PD1
BYTE Tcount = 0; // trasnmit bytecount in IN buffer
BYTE Rcount = 0; //receive bytecount from out buffer

// macro for generating the address of an endpoint's control and status register (EPnCS)
#define epcs(EP) (EPCS_Offset_Lookup_Table[(EP & 0x7E) | (EP > 128)] + 0xE6A1)

// this table is used by the epcs macro
const char code  EPCS_Offset_Lookup_Table[] =
{
    0,    // EP1OUT
    1,    // EP1IN
    2,    // EP2OUT
    2,    // EP2IN
    3,    // EP4OUT
    3,    // EP4IN
    4,    // EP6OUT
    4,    // EP6IN
    5,    // EP8OUT
    5,    // EP8IN
};



// ++++++++++++++++++++     CODE +++++++++++++++++++++++++++

void main(void)
{
	BYTE c;
    Initialize();
	
// Disconnect the USB interface,wait about 1,5 seconds,  reconnect as new device
   
    USBCS |= 0x0A; //disconnect (DISCON=1, RENUM=1)
	
	delay(1500);  //IMPORTANT, if no delay, device will not come up!
    USBIRQ = 0xff;          // Clear any pending USB interrupt requests. 
    EPIRQ = 0xff; // clear Endpoint ints
    EXIF &= ~0x10;      // clear USBINT

	CT1 |= 0x02; // become Full Speed Device
    USBCS &= ~(0x08); // reconnect (DISCON=0).reenumerate as new custom device (RENUM is 1)

//	delay(1500); //optional
	
    while(1) //mainloop
    {
		
		c=getcc();  // just echo chars via usb

/*
// use external uart as interface: being connected to another serial interface at same baudrate.
		c=get_uart();
		if (c!=0xff)
		{

		put_uart(c);
		putcc(c); // echo via usb
		}
*/	

		latencytimer();	//this needs to be called at leastevery 16ms
    }
}


//-----------------------------------------------------------------------------
// Control Endpoint 0 Device Request handler
//-----------------------------------------------------------------------------

/*
bmRequestType:
D7 Data Phase Transfer Direction
0 = Host to Device
1 = Device to Host
D6..5 Type
0 = Standard
1 = Class
2 = Vendor
3 = Reserved
D4..0 Recipient
0 = Device
1 = Interface
2 = Endpoint
3 = Other
4..31 = Reserved
SETUPDAT[0] = bmRequestType
SETUPDAT[1] = bmRequest
SETUPDAT[2:3] = wValue
SETUPDAT[4:5] = wIndex
SETUPDAT[6:7] = wLength
*/
void SetupCommand(void)
{



    // Errors are signaled by stalling endpoint 0.

    switch(SETUPDAT[0] & SETUP_MASK)    //0x60: switch by type
    {

    case SETUP_STANDARD_REQUEST: //0 = standard requests
        switch(SETUPDAT[1])
        {
        case SC_GET_DESCRIPTOR:
            switch(SETUPDAT[3])
            {
            case GD_DEVICE:
                SUDPTRH = MSB(&myDeviceDscr);
                SUDPTRL = LSB(&myDeviceDscr);
                break;
				
            case GD_DEVICE_QUALIFIER:
                SUDPTRH = MSB(&myDeviceQualDscr);
                SUDPTRL = LSB(&myDeviceQualDscr);
                break;
				
            case GD_CONFIGURATION:
               SUDPTRH = MSB(&myConfigDscr);
                SUDPTRL = LSB(&myConfigDscr);
                break;
            case GD_OTHER_SPEED_CONFIGURATION:
                SUDPTRH = MSB(&myConfigDscr);
                SUDPTRL = LSB(&myConfigDscr);
                break;
            case GD_STRING: //get string descriptor asto index in SETUPDAT[2]
                switch (SETUPDAT[2])
                {
                case 0: //lang
                    ptr = &string0;
                    break;
                case 1:
                    ptr = &string1;
                    break;
                case 2:
                    ptr = &string2;
                    break;
                case 3:
                    ptr = &string3;
                    break;
                default:
                    ptr = 0;
                    break;
                }
                if (ptr) // if valid string supported
                {
                    SUDPTRH = MSB(ptr);
                    SUDPTRL = LSB(ptr);
                }
                else
                    EZUSB_STALL_EP0();
                break;

            default:            // Invalid request
                EZUSB_STALL_EP0();
            }
            break;


        case SC_GET_INTERFACE:
                EP0BUF[0] = Interface;
                EP0BCH = 0;
                EP0BCL = 1;
            break;
        case SC_SET_INTERFACE:
               Interface = SETUPDAT[2];
            break;
        case SC_SET_CONFIGURATION:
            Configuration = SETUPDAT[2];
            break;
        case SC_GET_CONFIGURATION:
            EP0BUF[0] = Configuration;
            EP0BCH = 0;
            EP0BCL = 1;
            break;
        case SC_GET_STATUS:
            switch(SETUPDAT[0])
            {
            case GS_DEVICE:
                EP0BUF[0] = ((BYTE)Rwuen << 1) | (BYTE)Selfpwr;
                EP0BUF[1] = 0;
                EP0BCH = 0;
                EP0BCL = 2;
                break;
            case GS_INTERFACE:
                EP0BUF[0] = 0;
                EP0BUF[1] = 0;
                EP0BCH = 0;
                EP0BCL = 2;
                break;
            case GS_ENDPOINT: //get stall status
                EP0BUF[0] = *(BYTE xdata *) epcs(SETUPDAT[4]) & bmEPSTALL;
                EP0BUF[1] = 0;
                EP0BCH = 0;
                EP0BCL = 2;
                break;
            default:            // Invalid Command
                EZUSB_STALL_EP0();
            }
            break;
        case SC_CLEAR_FEATURE:
            switch(SETUPDAT[0])
            {
            case FT_DEVICE:
                if(SETUPDAT[2] == 1)
                    Rwuen = 0;       // Disable Remote Wakeup
                else
                    EZUSB_STALL_EP0();
                break;
            case FT_ENDPOINT: // take out of stall
                if(SETUPDAT[2] == 0)
                {
                    *(BYTE xdata *) epcs(SETUPDAT[4]) &= ~bmEPSTALL;
                    EZUSB_RESET_DATA_TOGGLE( SETUPDAT[4] );
                }
                else
                    EZUSB_STALL_EP0();
                break;
            }
            break;
        case SC_SET_FEATURE:
            switch(SETUPDAT[0])
            {
            case FT_DEVICE:
                if((SETUPDAT[2] == 1) && Rwuen_allowed)
                    Rwuen = 1;      // Enable Remote Wakeup
                else
                    EZUSB_STALL_EP0();
                break;
            case FT_ENDPOINT: // stall endpoint
                *(BYTE xdata *) epcs(SETUPDAT[4]) |= bmEPSTALL;
                break;
            default:
                EZUSB_STALL_EP0();
            }
            break;
        default:                     // *** Invalid Command
            EZUSB_STALL_EP0();
            break;
        }
        break;
		// end standard requests
		
	case 0x40: // vendor commands
	if (vendorcommands() == 1)
	break;
    default:
        EZUSB_STALL_EP0();
        break;
    }

    // Acknowledge handshake phase of device request
    EP0CS |= bmHSNAK;
}


void Initialize(void)
{

	// init the 2 LEDs outputs for debug
    OEA=0x03; // output direction
    IOA=0x03; //off
	
	// init Software uart pins. check defines for RX,TX. These are used in the software uart in softuart.a51 assembler file.
	bclr(3,OED); // RX input
	bset(1,OED); // TX output
	bset(1,IOD); // set to 1
	

	// init timer0/1
    TMOD = 0x11; //timer0 and timer1 as 16bit up counter,clocked at 4Mhz. This will overflow in 16ms interval

	
    CPUCS = ((CPUCS & ~bmCLKSPD) | bmCLKSPD1) ; // set CPU clock to 48MHz
	CKCON=0; // set stretch value to 0 = fastest DATA access. also timers are clocked by 48MHZ/12
	

	
	
	// adjust the length of the total config descriptor, as it cannot be evaluated at compiletime
	ptr = sizeof(myConfigDscr);
	myConfigDscr[2]=ptr;
	myConfigDscr[3]=ptr>>8;


    IFCONFIG=0xc0;  // select 48MHz internal clock for fifos/gpif
    SYNCDELAY;

    REVCTL=0x03;  // See TRM...seems necessary 
    SYNCDELAY;
	
// reset all fifos to default state
    FIFORESET = 0x80; // activate NAK-ALL to avoid race conditions
    SYNCDELAY;       // see TRM section 15.14
    FIFORESET = 0x0f; // reset all 4 fifos
    SYNCDELAY; //
    FIFORESET = 0x00; // deactivate NAK-ALL
    SYNCDELAY;


// from here, EP config	
	
//EP1IN:	
    EP1INCFG = 0xA0;     // Configure EP1IN as BULK IN EP
    SYNCDELAY;                   

//EP1OUT:
    EP1OUTCFG = 0xA0;     // Configure EP1OUT as BULK IN EP
    SYNCDELAY; 
    EP1OUTBC = 0x04; //arm Endpoint
    SYNCDELAY; 


    EP2CFG = 0x7F;       // enable EP2OUT bulk, double bufering
    SYNCDELAY;
    EP4CFG = 0x7F;      // not used
    SYNCDELAY;
    EP6CFG = 0x7F;      // not used
    SYNCDELAY;
    EP8CFG = 0xE0;      // not used
    SYNCDELAY;


//    AUTOPTRSETUP |= 0x01;         // enable dual autopointer feature for easy data copy
//    Rwuen = 1;                 // Enable remote-wakeup
   INTSETUP=0; //disable autovector interrupts
    GPIFIE=0; //disable INT4 ints, GPIF stuff




    SUDPTRCTL = 1; //use automatic descriptor sending in SETUP requests ( AUTO mode). descriptor needs to be at even address!!
	
// EndPoint interrupts: are enabled in EPIE. They are flagged in EPIRQ. Active EPIRQ ints cause a USBINT (int2) where the source can be checked in EPIRQ.
// in the USBINT service routine you can also check the autovector-value in INT2VEC register to tell the source.
    EPIE |= 0x04 ;              // Enable EP1in


	// Note: The Endpoint Interrupts also cause a USBINT, even when nothing is enabled in USBIE.
	// USBINT is actually INT2 being used as USB interrupt
    // Enable Bits in the USBIE interrupt source register, served by interrupt 8 vector in USBINT:
    USBIE = 0x01; //SUDAV
// general interrupt enable
    EIE = 0x01; // enable USB interrupt
    IE = 0x80; //global interrupt enable
}




/* 
INT2 = USBINT  general interrupt service:  (except RESUME)
This is the only vector for all USB related interrupts.
- all USBINTS flagged in USBIRQ-register and enabled in USBIE register
- all flagged EPIRQ ints enabled in EPIE
The 27 sources of the USBINT can be determined by the register INT2IVEC.
It hold the autovector value of the source, but we disabled autovectoring, so use a switch to indentify the source.
*/
void USB_isr(void) interrupt 8 
{


	
	switch (INT2IVEC)
	{
		case 0: //SUDAV setup command received
		USBIRQ = 0x01;  // Clear SUDAV IRQ_bit by write a 1 to it.
		SetupCommand(); //serve the setup request
		break;
		
		case 0x28: // EP1IN. access to EP1INBUF granted (we own it). init it with 2 status bytes.
		EPIRQ = 0x04; // clear flag
		Tcount=2;
		//FTDI always prepends in transfers with 2 linestatus bytes!!!
        EP1INBUF [0] = 0x11; //0x11, Full Speed 64 byte MAX packet, CTS
        EP1INBUF [1] = 0x60; //Line Status: 0x60, Transmitter Holding Register Empty, Transmitter Empty
 		break;	
		
		default:
		break;
	}
    EXIF &= ~0x10; // Clear  USB_IRQ irq flag

}



// ++++++++++++++++++++++++++++++++++   serial IO +++++++++++++++++++++++++++++++++++++++


void putcc(BYTE c)
{
  transmit(c); // to host
}

BYTE getcc(void)
{
    BYTE c;
	c=receive(); //from host
	if (c != 0xff)  putcc(c);
	if (c==0x0d) putcc(0x0a);
    return c;
}

/*
// get a string from keyboard
void getss(BYTE *buf)
{
    BYTE *ps, c;
    ps=buf;
    while (1)
    {
        c=getcc();
        if (( c == 0x0d)||(c==0x0a)) // CR LF
            break;
        *ps++=c;
    }
    *ps=0;
    putcc(0x0d);
    putcc(0x0a);
}
*/

void putss(const BYTE *ps)
{
    while (*ps)
    {
        putcc(*ps++);
    }
}

// output 3 digit dec number in reziproc order. 123 will output 321. just for debug
void putnumber(BYTE c)
{
	BYTE i;
	for (i=0;i<3;i++)
	{
		putcc(0x30+(c%10));
		c/=10;
	}
}

const char *menue="\n***Menue***\n"
                  "l-list state\n"
                  "c-clock1\n"
                  "s-start i2s\n"
                  "x-stop i2s\n"
                  "i-set inport\n"
                  ;
const char *prompt="\nfx2:>";



/*
called repeatedly in the main loop
if a char is received, process it.
this is a one char menue!
to be used for ?? a menue ;)
*/
void mainmenue(void)
{
BYTE c;


        c = getcc();
    if ( c != 0xff) // if char was received
	{
        switch(c)
        {
        case 'l': //list
            putss("hello");
            break;


        default:
            putss(menue);
        }
        putss(prompt);
	}

}

/* get one byte from host
exit:
char received
0xff = nothing received
we do not wait
*/
BYTE receive(void)
{
    BYTE c;
	
    if (!(EP1OUTCS & 0x02)) // if EP is not busy. ie. BUSY =  SIE NOT ‘owns’ the buffer,
    {
		if (Rcount == EP1OUTBC)
		{
				EP1OUTBC = 0x04;// Arms EP1 endpoint. write any value to bytecount register to give back the fifo to SIE.
				Rcount=0;
		}
		else
		{
				 c=EP1OUTBUF[Rcount]; // get it
				 Rcount++;
				return c;
		}
	}
            return 0xff;
}

// send data to host on EP1IN
void transmit(BYTE c)
{

		if (!(EP1INCS & 0x02)) // claim buffer. if buffer is iwned by SIE, then Host did not read the endpoint, so forget it(nobody is reading)
		{
        EP1INBUF [Tcount] = c;
			if (Tcount >63)
			{
			EP1INBC = Tcount;  // send it
			SYNCDELAY;
			}
			else
			Tcount++;

		}
    
//	reset latencytimer
TR1=0;
TF1=0;
TL1=0;
TH1=0;
TR1=1;
}

/*
8051 timer can only count up.
we configured timer0 as 16bit up counter.
when it overflows, it sets the overflow flag which we evaluate as 16ms expired.
the Timer is clocked by 48MHZ/12=4MHZ.
A 0xffff count cycle therefore lasts 16,3ms.
61 cycles makeup 1 second.
entry:
ms = miliseconds wanted, >= 16
*/
void delay( WORD ms)
{
	WORD cycles=ms/16;

	TH0 = 0;			// load 16bit count-register
	TL0 = 0;			
	TR0 = 1;			// Start timer0 
	while (cycles)
	{
		if (TF0)
		{
			cycles--;
			TF0 = 0;	
		}
	}
	
	TR0 = 0;			// Stop timer0
	
}

// FTDI requires to send 2bytes-status info every 16ms
void latencytimer(void)
{
	if (LatTimer!=16) return; // must be enabled by host
	TR1=1; //start latencytimer, if not already running
	if (TF1) // if timer1 overflowed, 16ms passed, commit the buffer
	{
		if (!(EP1INCS & 0x02)) // claim buffer
		{
        EP1INBC = Tcount;  // endpoint bytecount L.  This commits the EP for transfer.
        SYNCDELAY;
		}
		TF1=0;
		
	}
}

//returns 1 if successfull processed, else 0
bit vendorcommands(void)
{
/* FTDI commands on EP0
 we basicly ignore all data send to us.
 we only send valid modemstatus really.
 Control messages hold data in wValue and Portinfo wIndex, with wLength=0.
 wLength otherwise is the bytecount requested from the device via EP0, like in getmodemstatus.
 SETUPDAT[2:3] = wValue
SETUPDAT[4:5] = wIndex
SETUPDAT[6:7] = wLength

*/
    if (SETUPDAT[0] == 0x40) // if vendor specific control write to device
    {
       switch(SETUPDAT[1])
        {
            // SETUPDAT[1] contains the ftdi command:
        case 0: // reset, purge RX buffer, purge TX buffer: we just ignore these commands.
            break;

        case 1: //setflowctrl.modem control: 0x01=DTR=1;0x02=RTS=1; 
            break;
        case 2: //set DTR/RTS. SET_FLOW_CTRL, is never queryd
            break;
        case 3: //set baudrate
            break;
        case 4: //set line property, dataformat 8n1 etc
            break;
        case 6: //set event char
            break;
        case 7: //set error char
            break;

        case 9: //set latency time. value in SETUPDAT[2]
		LatTimer=SETUPDAT[2];
            break;
/*
        case 0x0b: //set bitbang mode
            break;

        case 0x91: //write eeprom
            break;
        case 0x92: //erase eeprom
            break;
*/			
			default:
    return(0); // not supported
        }
    }
    else if (SETUPDAT[0] == 0xC0) // if vendor specific control read from device
    {

        switch(SETUPDAT[1])
        {
        case 5: //get modem status: 2 bytes
			EP0BUF[0] = 0x11;
			EP0BUF[1] = 0x60;
			EP0BCH = 0;
			EP0BCL = 2;
			break;
        case 0x0a: //get latency time: 16
			EP0BUF[0] = 16;
			EP0BCH = 0;
			EP0BCL = 1;
            break;
//        case 0x0c: //read pins,
//        case 0x90: //read eeprom,
			default:
    return(0); // not supported
        }
	}
    return(1); //OK
}

