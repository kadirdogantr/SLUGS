/*
The MIT License

Copyright (c) 2009 UCSC Autonomous Systems Lab

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

// ==============================================================
// interProcComm.c
// This is code implements via SPI the interprocessor communications
// to be used in the UCSC Autopilot project. It makes use of the 
// circular buffer data structure circBuffer.c. It has been 
// written to be implemented in Simulink. It configures SPI1 as slave
// for the control MCU and master for the sensor MCU, both 
// at 10 MHZ; it then initializes a circular buffer. 
// The main functions are: 1.- spiSend writes data to SPI1 in the predefined
// comm protocol. This will be implemented in the master (sensor) MCU.
// 2.- SPI1 interrupt gets bytes from the master MCU. It stores bytes in
// two buffers in ping-pong mode (ala DMA) 
// Note that by the nature of the SPI buses BOTH modules (slave and master)
// send and receive data simultaneously. This is advantage is used to 
// let the slave know what byte the master is expecting thus allowing an
// efective two-way communication
// 
// Code by: Mariano I. Lizarraga
// First Revision: Aug 27 2008 @ 21:15
// =========================================================
#include "apDefinitions.h"
#include "circBuffer.h"
#include <p33fxxxx.h>
#include <spi.h>

unsigned int spiRxBuf[3][SPIBUFSIZE];
unsigned char currentBuffer = 0, lastBuffer =2;


void spiSlaveInit(void){
    // SPI1CON1 Register Settings
    SPI1CON1bits.DISSCK = 0;    //Internal Serial Clock is Enabled.
    SPI1CON1bits.DISSDO = 0;    //SDOx pin is controlled by the module.
    SPI1CON1bits.MODE16 = 1;    //Communication is word-wide (16 bits).
    SPI1CON1bits.SMP    = 0;    //Cleared in slave mode.
    SPI1CON1bits.CKE    = 0;    //Serial output data changes on transition from
                                //Idle clock state to active clock state
    SPI1CON1bits.CKP    = 0;    //Idle state for clock is a low level;
                                //active state is a high level
    SPI1CON1bits.SSEN   = 1;    // SS Used in slave mode    
    SPI1CON1bits.MSTEN  = 0;    //Slave Mode Enabled
    
    // Configure the clock for 10 MHz
    SPI1CON1bits.SPRE   = 7;    //Secondary prescaler to 1:1
    SPI1CON1bits.PPRE   = 2;    //Primary prescaler 4:1
    
    
    // Enable the module 
    SPI1STATbits.SPIEN  = 1;    //Enable SPI Module
    
    // write the buffer to start getting data
    // this will not be sent until data is received
    WriteSPI1(0x0000);
    
    // Enable the interrupts
    IFS0bits.SPI1IF 	= 0;
    IPC2bits.SPI1IP 	= 6;
    IEC0bits.SPI1IE		= 1; 
}

void spiMasterInit(void){
    // SPI1CON1 Register Settings
    SPI1CON1bits.DISSCK = 0;    //Internal Serial Clock is Enabled.
    SPI1CON1bits.DISSDO = 0;    //SDOx pin is controlled by the module.
    SPI1CON1bits.MODE16 = 1;    //Communication is word-wide (16 bits).
    SPI1CON1bits.SMP    = 0;    //Cleared in slave mode.
    SPI1CON1bits.CKE    = 0;    //Serial output data changes on transition from
                                //Idle clock state to active clock state
    SPI1CON1bits.CKP    = 0;    //Idle state for clock is a low level;
                                //active state is a high level
    SPI1CON1bits.SSEN   = 1;    // SS Used in slave mode    
    SPI1CON1bits.MSTEN  = 1;    //Master Mode Enabled
    
    // Configure the clock for 10 MHz
    SPI1CON1bits.SPRE   = 7;    //Secondary prescaler to 1:1
    SPI1CON1bits.PPRE   = 2;    //Primary prescaler 4:1
    
    
    // Enable the module 
    SPI1STATbits.SPIEN  = 1;    //Enable SPI Module
}

void spiSend(unsigned char * data2Send){
	unsigned char i;
	unsigned int rcvdIdx = 0;
	// First send a dummy value to let know the 
	// slave that you are ready to transmit
	WriteSPI1(BEGINSPI);
	while(!DataRdySPI1());
	rcvdIdx = ReadSPI1();
	
	// While we do not receive the flag indicating
	// that we are done, keep sending the requested
	// index 
	while (rcvdIdx != ENDSPI){
		WriteSPI1(data2Send[rcvdIdx]);
		while(!DataRdySPI1());
		rcvdIdx = ReadSPI1();	
	}	
}

// Interrupt service routine for SPI1 Slave 
void __attribute__((__interrupt__, no_auto_psv)) _SPI1Interrupt(void){
 	static unsigned int spiBufIdx = 1; 
 	unsigned int dataRead;
	
	// get the next index ready for writting
	// if the SPI index is less than the size of the buffer
	if (spiBufIdx < SPIBUFSIZE){ 
		// request the next data
		WriteSPI1(spiBufIdx);
	}else{
		// if it is equals to the size of the buffer
		if (spiBufIdx == SPIBUFSIZE ){
			// signal that this is the end
			WriteSPI1(ENDSPI);
		} else {
			// if it is greater than the buffer, then write a zero 
			// no more data will be comming for this cycle after this
			// interrupt
			WriteSPI1(0x0000);
		}	
	}
	
 	// read the received data 
	dataRead = ReadSPI1();
	
	// if the received data is the begin indication
	if (dataRead == BEGINSPI){
		// just increment the outbuffer, discard the data read
		spiBufIdx++;
	} else{ // a data was received
		// Post-increment the SPIBUF index and assign the data 
		// accordingly
		spiRxBuf[currentBuffer][(spiBufIdx++) -2] = dataRead; 
		// if you reach the limit
		if (spiBufIdx > SPIBUFSIZE){
			// Reset the index
			spiBufIdx = 1;
			// switch the working buffers
			lastBuffer = currentBuffer;
			currentBuffer = currentBuffer > 2? 0:currentBuffer+1;			
		}
	}
	// clear the interrupt
	 IFS0bits.SPI1IF = 0;
}