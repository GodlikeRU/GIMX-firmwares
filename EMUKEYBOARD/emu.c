/*
  Copyright 2018 Daniel Kucner (darkandan [at] windowslive [dot] com)

  Redistributed under the GPLv3 licence.

  Based on code by
    Denver Gingerich (denver [at] ossguy [dot] com)
    Dean Camera (dean [at] fourwalledcubicle [dot] com)
    Mathieu Laurendeau (mat.lau [at] laposte [dot] net)

  Original licence:

  Permission to use, copy, modify, distribute, and sell this
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaim all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

/** \file
 *
 *  Main source file for the controller. This file contains the main tasks and
 *  is responsible for the initial application hardware configuration.
 */

#include "emu.h"

#include <LUFA/Drivers/Peripheral/Serial.h>
#include "../adapter_protocol.h"

#define MAX_CONTROL_TRANSFER_SIZE 64

#define USART_BAUDRATE 500000
#define USART_DOUBLE_SPEED false
//#define DEBUG_INFO true

static bool UsingReportProtocol = true; // USE HID mode for Keyboard

/*
 * The reference report data.
 * gimxcontroller takes care of data placement
 */
static uint8_t report[8] =
{
	  0x00,// Modifier
    0x00,// Reserved
    0x00,0x00,// Keys
    0x00,0x00,
    0x00,0x00,
};

// Reference data to hold previous report for comparison
static uint8_t prev_report[8] =
{
	  0x00,// Modifier
    0x00,// Reserved
    0x00,0x00,// Keys
    0x00,0x00,
    0x00,0x00,
};


static uint8_t* pdata;
static unsigned char i = 0;

/*
 * These variables are used in both the main and serial interrupt,
 * therefore they have to be declared as volatile.
 */
static volatile unsigned char sendReport = 0;
static volatile unsigned char started = 0;
static volatile unsigned char packet_type = 0;
static volatile unsigned char value_len = 0;

static inline int16_t Serial_BlockingReceiveByte(void)
{
  while(!Serial_IsCharReceived());
  return UDR1;
}

/** Main program entry point. This routine configures the hardware required by the application, then
 *  enters a loop to run the application tasks in sequence.
 */
int main(void)
{
	SetupHardware();

	for (;;)
	{
		HID_Task();
		USB_USBTask();
	}
}

static inline void handle_packet(void)
{
  switch(packet_type)
  {
    case BYTE_TYPE:
      Serial_SendByte(BYTE_TYPE);
      Serial_SendByte(BYTE_LEN_1_BYTE);
      Serial_SendByte(BYTE_TYPE_KEYBOARD);
      break;
    case BYTE_STATUS:
      Serial_SendByte(BYTE_STATUS);
      Serial_SendByte(BYTE_LEN_1_BYTE);
      Serial_SendByte(started);
      break;
    case BYTE_START:
      Serial_SendByte(BYTE_START);
      Serial_SendByte(BYTE_LEN_1_BYTE);
      Serial_SendByte(started);
      started = 1;
      break;
    case BYTE_CONTROL_DATA:
      break;
    case BYTE_IN_REPORT:
      sendReport = 1;
      //no answer
      break;
  }
}

static unsigned char buf[MAX_CONTROL_TRANSFER_SIZE];

ISR(USART1_RX_vect)
{
  packet_type = UDR1;
  value_len = Serial_BlockingReceiveByte();
  if(packet_type == BYTE_IN_REPORT)
  {
    pdata = report;
  }
  else
  {
    pdata = buf;
  }
  while(i < value_len)
  {
    pdata[i++] = Serial_BlockingReceiveByte();
  }
  i = 0;
  handle_packet();
}

void serial_init(void)
{
  Serial_Init(USART_BAUDRATE, USART_DOUBLE_SPEED);

  UCSR1B |= (1 << RXCIE1); // Enable the USART Receive Complete interrupt (USART_RXC)
}

/** Configures the board hardware and chip peripherals for the demo's functionality. */
void SetupHardware(void)
{
  /* Disable watchdog */
  MCUSR = 0;
  wdt_disable();

	/* Disable clock division */
	clock_prescale_set(clock_div_1);

	serial_init();


  GlobalInterruptEnable();

	/* Hardware Initialization */
	LEDs_Init();

  while(!started);

	USB_Init();
	

}

/** Event handler for the USB_Connect event. This indicates that the device is enumerating via the status LEDs and
 *  starts the library USB task to begin the enumeration and USB management process.
 */
void EVENT_USB_Device_Connect(void)
{
	/* Indicate USB enumerating */
	//LEDs_SetAllLEDs(LEDMASK_USB_ENUMERATING);
}

/** Event handler for the USB_Disconnect event. This indicates that the device is no longer connected to a host via
 *  the status LEDs and stops the USB management and joystick reporting tasks.
 */
void EVENT_USB_Device_Disconnect(void)
{
	/* Indicate USB not ready */
	//LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
}

/** Event handler for the USB_ConfigurationChanged event. This is fired when the host set the current configuration
 *  of the USB device after enumeration - the device endpoints are configured and the joystick reporting task started.
 */
void EVENT_USB_Device_ConfigurationChanged(void)
{
	bool ConfigSuccess = true;

	/* Setup HID Report Endpoint */
	ConfigSuccess &= Endpoint_ConfigureEndpoint(IN_EPNUM, EP_TYPE_INTERRUPT,EPSIZE, 1);

	/* Indicate endpoint configuration success or failure */
	//LEDs_SetAllLEDs(ConfigSuccess ? LEDMASK_USB_READY : LEDMASK_USB_ERROR);
}



/** Event handler for the USB_ControlRequest event. This is used to catch and process control requests sent to
 *  the device from the USB host before passing along unhandled control requests to the library for processing
 *  internally.
 */
void EVENT_USB_Device_ControlRequest(void)
{
  /* Handle HID Class specific requests */
	switch (USB_ControlRequest.bRequest)
	{
		case HID_REQ_GetReport:
			if (USB_ControlRequest.bmRequestType == (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE))
			{
			  Endpoint_ClearSETUP();


			    /* Write the report data to the control endpoint */
				Endpoint_Write_Control_Stream_LE(report, sizeof(report));
			 

				Endpoint_ClearOUT();
			}

			break;
		case HID_REQ_SetReport:
			if (USB_ControlRequest.bmRequestType == (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				// Ignore LED report, this is a fake keyboard
			}

			break;
		case HID_REQ_GetProtocol:
			if (USB_ControlRequest.bmRequestType == (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				Endpoint_ClearSETUP();

				/* Write the current protocol flag to the host */
				Endpoint_Write_8(UsingReportProtocol);

				Endpoint_ClearIN();
				Endpoint_ClearStatusStage();
			}

			break;
		case HID_REQ_SetProtocol:
			if (USB_ControlRequest.bmRequestType == (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				Endpoint_ClearSETUP();
				Endpoint_ClearStatusStage();

				/* Set or clear the flag depending on what the host indicates that the current Protocol should be */
				UsingReportProtocol = (USB_ControlRequest.wValue != 0);
			}
			break;
			//Ignore rest of requests, they are not needed
	}
}

/** Sends the next HID report to the host, via the IN endpoint. */
void SendNextReport(void)
{
  bool hasDataChanged = false;

  /* Select the IN Report Endpoint */
	Endpoint_SelectEndpoint(IN_EPNUM);

  if (sendReport)
  {
    // Compare previous report with current one, if it does not changed then there's no need to send it
    hasDataChanged = (memcmp(prev_report, report, sizeof(report)) != 0);

    /* Check if Keyboard Endpoint Ready for Read/Write and report changed */
   if(Endpoint_IsReadWriteAllowed() && hasDataChanged)	
	 {
    
    // Copy current report to previous report for comparison purposes next time
    memcpy(prev_report, report, sizeof(report));

    #ifdef DEBUG_INFO
      // Report useful debug data back to GIMX
      Serial_SendByte(BYTE_DEBUG);
      Serial_SendByte(0x08);
      Serial_SendData(report, sizeof(report)); 
    #endif

		/* Write IN Keyboard Report Data */
		Endpoint_Write_Stream_LE(report, sizeof(report), NULL);

		sendReport = 0;

		/* Finalize the stream transfer to send the last packet */
		Endpoint_ClearIN();
	 }
  }
}

/** Function to manage HID report generation and transmission to the host. */
void HID_Task(void)
{
	/* Device must be connected and configured for the task to run */
	if (USB_DeviceState != DEVICE_STATE_Configured)
	  return;

  /* Send the next keypress report to the host */
  SendNextReport();
}
