/*
This implements a virtual comport as cypress device.
CDC = USB Communication Device Class.

In order to be considered a COM port, the USB device declares 2 interfaces:
•     Abstract Control Model Communication, with 1 Interrupt IN endpoint, we use EP1IN as int.
•     Abstract Control Model Data, with 1 Bulk IN and 1 Bulk OUT endpoint, we use EP1OUT and EP8IN as bulk.

Data Interface( bulk or isochronous ):
EP1OUT = serial transmit (from PC).  See ISR_Ep1out(void) interrupt 0
EP8IN = serial receive (to PC)

Communication Interface (setting baudrate parity...etc) for device management:
Linecoding and control signals are supported as dummy.
Linecontrols ar send to PC by interrupt EP1IN.

RENUM = 0; The SIE engine handles all endpoint 0 controls
RENUM = 1; The firmware handles all endpoint 0 controls

*/


#pragma NOIV               // Do not generate interrupt vectors
#include "fx2.h"
#include "fx2regs.h"
#include "fx2sdly.h"			// SYNCDELAY macro


// Macros: Bit operations on byte/word/long variables. Format: bset(Bit,variable). works also on SFRs(creates sbi,cbi)
#define bset(x,y) (y |= (1 << x))
#define bclr(x,y) (y &= (~(1 << x)))
#define btst(x,y) (y & (1 << x))

extern BOOL   GotSUD;         // Received setup data flag
extern BOOL   Sleep;
extern BOOL   Rwuen;
extern BOOL   Selfpwr;
extern BYTE xdata LineCode[7] ; // the serial setup data, we dont use it
int RxCount,RxIndex;  // RxIndex=ep1out index; RxCount = total received bytes in ep1out. receive counters
char sbuf[32];


//protos:
unsigned char receive(void);
void transmit(unsigned char c);
void putcc(unsigned char c);
unsigned char getcc(void);
void getss(unsigned char *buf);
void putss(const unsigned char *ps);
void mainmenue(void);


void putcc(unsigned char c)
{
  transmit(c); 
}

unsigned char getcc(void)
{
    unsigned char c;
	c=receive();
	if (c != 0xff)  putcc(c);
    return c;
}

void getss(unsigned char *buf)
{
    unsigned char *ps, c;
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

void putss(const unsigned char *ps)
{
    while (*ps)
    {
        putcc(*ps++);
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
*/
void mainmenue(void)
{
unsigned char c;


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
data is in fifo, (0-64) bytes
this routine expects that EP1OUT data has arrived in the EP1 interrupt and RxIndex and RxCount are set correctly.
Here we just read the next byte from EP1OUT fifo.
exit:
char received
0xff = nothing received
we do not wait
*/
unsigned char receive(void)
{
    unsigned char c;
	
    if (!(EP1OUTCS & 0x02)) // if EP is not busy. ie. BUSY =  SIE NOT ‘owns’ the buffer,
    {

        if(RxIndex<RxCount) // if there are bytes available in fifo
        {
	bclr(0,IOA);

            c=EP1OUTBUF[RxIndex]; // get it
            RxIndex++; //advance ep1out fifo index
			
            return c;
        }
        else // nobytes left, return it for new data to arrive
        {
	bset(0,IOA);
            EP1OUTBC = 0x04;// Arms EP1 endpoint. write any value to bytecount register to give back the fifo to SIE.
        }
    }
            return 0xff;
}

// send data to host on EP8IN
void transmit(unsigned char c)
{
//	bclr(0,IOA);
	while (!(EP2468STAT & bmEP8EMPTY)); // check if EP8 is empty 
    {
        EP8FIFOBUF [0] = c;
        EP8BCH = 0;    // endpoint bytecount H
        SYNCDELAY;
        EP8BCL = 1;  // endpoint bytecount L. this should ready the EP8 for the host to fetch.  This commits the EP for transfer.
        SYNCDELAY;
    }
//	bset(0,IOA);

}


// Called repeatedly while the device is idle
void TD_Poll(void)
{
    unsigned char c;
    // Serial State Notification that has to be sent periodically to the host

    if (!(EP1INCS & 0x02))      // check if EP1IN is available
    {
        EP1INBUF[0] = 0x0A;       // if it is available, then fill the first 10 bytes of the buffer with
        EP1INBUF[1] = 0x20;       // appropriate data.
        EP1INBUF[2] = 0x00;
        EP1INBUF[3] = 0x00;
        EP1INBUF[4] = 0x00;
        EP1INBUF[5] = 0x00;
        EP1INBUF[6] = 0x00;
        EP1INBUF[7] = 0x02;
        EP1INBUF[8] = 0x00;
        EP1INBUF[9] = 0x00;
        EP1INBC = 10;            // manually commit once the buffer is filled
    }

mainmenue();

/* echo all chars
        c=receive();

		if (c)
        transmit(c);
*/    

}



// Called once at startup
void DevInit(void)
{

  // enable leds
  OEA=0x03;
  IOA=0x03; //off

// set the CPU clock to 48MHz
    CPUCS = ((CPUCS & ~bmCLKSPD) | bmCLKSPD1) ;

    // set the slave FIFO interface to 48MHz
    IFCONFIG |= 0x40;
    SYNCDELAY;
    REVCTL = 0x03;
    SYNCDELAY;



    FIFORESET = 0x80; // activate NAK-ALL to avoid race conditions
    SYNCDELAY;       // see TRM section 15.14
    FIFORESET = 0x08; // reset, FIFO 8
    SYNCDELAY; //
    FIFORESET = 0x00; // deactivate NAK-ALL
    SYNCDELAY;


    EP1OUTCFG = 0xA0;    // Configure EP1OUT as BULK EP
    SYNCDELAY;
    EP1INCFG = 0xB0;     // Configure EP1IN as BULK IN EP
    SYNCDELAY;                    // see TRM section 15.14
    EP2CFG = 0x7F;       // Invalid EP
    SYNCDELAY;
    EP4CFG = 0x7F;      // Invalid EP
    SYNCDELAY;
    EP6CFG = 0x7F;      // Invalid EP
    SYNCDELAY;




    EP8CFG = 0xE0;      // Configure EP8 as BULK IN EP
    SYNCDELAY;

    EP8FIFOCFG = 0x00;  // Configure EP8 FIFO in 8-bit Manual Commit mode
    SYNCDELAY;
    T2CON  = 0x34;


    EPIE |= bmBIT3 ;              // Enable EP1 OUT Endpoint interrupts to receive chars

    AUTOPTRSETUP |= 0x01;         // enable dual autopointer feature
    Rwuen = TRUE;                 // Enable remote-wakeup
    EP1OUTBC = 0x04;




}



//-----------------------------------------------------------------------------
// USB Interrupt Handlers
//   The following functions are called by the USB interrupt jump table.
//-----------------------------------------------------------------------------

// host sent data on EP1OUT
void ISR_Ep1out(void) interrupt 0
{
    EZUSB_IRQ_CLEAR();		//Clears the USB interrupt
    EPIRQ = bmBIT3;			//Clears EP1 OUT interrupt request

    RxIndex=0; // set index to 0
    RxCount=EP1OUTBC; // amount of received bytes

}

void ISR_Sudav(void) interrupt 0
{

    GotSUD = TRUE;            // Set flag
    EZUSB_IRQ_CLEAR();
    USBIRQ = bmSUDAV;         // Clear SUDAV IRQ
}

// Setup Token Interrupt Handler
void ISR_Sutok(void) interrupt 0
{
    EZUSB_IRQ_CLEAR();
    USBIRQ = bmSUTOK;         // Clear SUTOK IRQ
}

void ISR_Sof(void) interrupt 0
{
    EZUSB_IRQ_CLEAR();
    USBIRQ = bmSOF;            // Clear SOF IRQ
}

void ISR_Ures(void) interrupt 0
{
    if (EZUSB_HIGHSPEED())
    {
        pConfigDscr = pHighSpeedConfigDscr;
        pOtherConfigDscr = pFullSpeedConfigDscr;
    }
    else
    {
        pConfigDscr = pFullSpeedConfigDscr;
        pOtherConfigDscr = pHighSpeedConfigDscr;
    }

    EZUSB_IRQ_CLEAR();
    USBIRQ = bmURES;         // Clear URES IRQ
}

void ISR_Susp(void) interrupt 0
{
    Sleep = TRUE;
    EZUSB_IRQ_CLEAR();
    USBIRQ = bmSUSP;

}

void ISR_Highspeed(void) interrupt 0
{
    if (EZUSB_HIGHSPEED())
    {
        pConfigDscr = pHighSpeedConfigDscr;
        pOtherConfigDscr = pFullSpeedConfigDscr;
    }
    else
    {
        pConfigDscr = pFullSpeedConfigDscr;
        pOtherConfigDscr = pHighSpeedConfigDscr;
    }

    EZUSB_IRQ_CLEAR();
    USBIRQ = bmHSGRANT;
}




void ISR_Ep0ack(void) interrupt 0
{
}
void ISR_Stub(void) interrupt 0
{
}
void ISR_Ep0in(void) interrupt 0
{
}
void ISR_Ep0out(void) interrupt 0
{


}
void ISR_Ep1in(void) interrupt 0
{
}

void ISR_Ep2inout(void) interrupt 0
{
}
void ISR_Ep4inout(void) interrupt 0
{

}
void ISR_Ep6inout(void) interrupt 0
{
}
void ISR_Ep8inout(void) interrupt 0
{
}
void ISR_Ibn(void) interrupt 0
{
}
void ISR_Ep0pingnak(void) interrupt 0
{
}
void ISR_Ep1pingnak(void) interrupt 0
{
}
void ISR_Ep2pingnak(void) interrupt 0
{
}
void ISR_Ep4pingnak(void) interrupt 0
{
}
void ISR_Ep6pingnak(void) interrupt 0
{
}
void ISR_Ep8pingnak(void) interrupt 0
{
}
void ISR_Errorlimit(void) interrupt 0
{
}
void ISR_Ep2piderror(void) interrupt 0
{
}
void ISR_Ep4piderror(void) interrupt 0
{
}
void ISR_Ep6piderror(void) interrupt 0
{
}
void ISR_Ep8piderror(void) interrupt 0
{
}
void ISR_Ep2pflag(void) interrupt 0
{
}
void ISR_Ep4pflag(void) interrupt 0
{
}
void ISR_Ep6pflag(void) interrupt 0
{
}
void ISR_Ep8pflag(void) interrupt 0
{
}
void ISR_Ep2eflag(void) interrupt 0
{
}
void ISR_Ep4eflag(void) interrupt 0
{
}
void ISR_Ep6eflag(void) interrupt 0
{
}
void ISR_Ep8eflag(void) interrupt 0
{
}
void ISR_Ep2fflag(void) interrupt 0
{
}
void ISR_Ep4fflag(void) interrupt 0
{
}
void ISR_Ep6fflag(void) interrupt 0
{
}
void ISR_Ep8fflag(void) interrupt 0
{
}
void ISR_GpifComplete(void) interrupt 0
{
}
void ISR_GpifWaveform(void) interrupt 0
{
}
