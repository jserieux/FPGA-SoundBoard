#include "demo.h"
#include "audio/audio.h"
#include "dma/dma.h"
#include "intc/intc.h"
#include "userio/userio.h"
#include "iic/iic.h"
//user includes
#include "xgpio.h"
#define SW_CHANNEL 1
#define LED_CHANNEL 1
#define SWITCH_CHANNEL 1
//end user includes
#include "xparameters.h"
#include "xil_exception.h"
#include "xdebug.h"
#include "xiic.h"
#include "xaxidma.h"
#include "xtime_l.h"
#ifdef XPAR_INTC_0_DEVICE_ID
 #include "xintc.h"
 #include "microblaze_sleep.h"
#else
 #include "xscugic.h"
#include "sleep.h"
#include "xil_cache.h"
#endif

// Audio constants
// Number of seconds to record/playback
#define NR_SEC_TO_REC_PLAY		10

// ADC/DAC sampling rate in Hz
//#define AUDIO_SAMPLING_RATE		1000
#define AUDIO_SAMPLING_RATE	  96000

// Number of samples to record/playback
#define NR_AUDIO_SAMPLES		(NR_SEC_TO_REC_PLAY*AUDIO_SAMPLING_RATE)

/* Timeout loop counter for reset
 */
#define RESET_TIMEOUT_COUNTER	10000

#define TEST_START_VALUE	0x0

/************************** Function Prototypes ******************************/
#if (!defined(DEBUG))
extern void xil_printf(const char *format, ...);
#endif


/************************** Variable Definitions *****************************/
/*
 * Device instance definitions
 */

static XIic sIic;
static XAxiDma sAxiDma;		/* Instance of the XAxiDma */

static XGpio sUserIO;

#ifdef XPAR_INTC_0_DEVICE_ID
 static XIntc sIntc;
#else
 static XScuGic sIntc;
#endif

//
// Interrupt vector table
#ifdef XPAR_INTC_0_DEVICE_ID
const ivt_t ivt[] = {
	//IIC
	{XPAR_AXI_INTC_0_AXI_IIC_0_IIC2INTC_IRPT_INTR, (XInterruptHandler)XIic_InterruptHandler, &sIic},
	//DMA Stream to MemoryMap Interrupt handler
	{XPAR_AXI_INTC_0_AXI_DMA_0_S2MM_INTROUT_INTR, (XInterruptHandler)fnS2MMInterruptHandler, &sAxiDma},
	//DMA MemoryMap to Stream Interrupt handler
	{XPAR_AXI_INTC_0_AXI_DMA_0_MM2S_INTROUT_INTR, (XInterruptHandler)fnMM2SInterruptHandler, &sAxiDma},
	//User I/O (buttons, switches, LEDs)
	{XPAR_AXI_INTC_0_AXI_GPIO_0_IP2INTC_IRPT_INTR, (XInterruptHandler)fnUserIOIsr, &sUserIO}
};
#else
const ivt_t ivt[] = {
	//IIC
	{XPAR_FABRIC_AXI_IIC_0_IIC2INTC_IRPT_INTR, (Xil_ExceptionHandler)XIic_InterruptHandler, &sIic},
	//DMA Stream to MemoryMap Interrupt handler
	{XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR, (Xil_ExceptionHandler)fnS2MMInterruptHandler, &sAxiDma},
	//DMA MemoryMap to Stream Interrupt handler
	{XPAR_FABRIC_AXI_DMA_0_MM2S_INTROUT_INTR, (Xil_ExceptionHandler)fnMM2SInterruptHandler, &sAxiDma},
	//User I/O (buttons, switches, LEDs)
	{XPAR_FABRIC_AXI_GPIO_0_IP2INTC_IRPT_INTR, (Xil_ExceptionHandler)fnUserIOIsr, &sUserIO}
};
#endif


/*****************************************************************************/
/**
*
* Main function
*
* This function is the main entry of the interrupt test. It does the following:
*	Initialize the interrupt controller
*	Initialize the IIC controller
*	Initialize the User I/O driver
*	Initialize the DMA engine
*	Initialize the Audio I2S controller
*	Enable the interrupts
*	Wait for a button event then start selected task
*	Wait for task to complete
*
* @param	None
*
* @return
*		- XST_SUCCESS if example finishes successfully
*		- XST_FAILURE if example fails.
*
* @note		None.
*
******************************************************************************/
int main(void)
{
	int Status;

	Demo.u8Verbose = 0;

	//user edits
	XGpio Gpio;
	u32 sw_data, led_data;

	// Initialize GPIO for LEDs and switches
	    Status = XGpio_Initialize(&Gpio, XPAR_GPIO_0_DEVICE_ID);
	    if (Status != XST_SUCCESS) {
	        xil_printf("GPIO Initialization Failed\r\n");
	        return XST_FAILURE;
	    }

	    XGpio_SetDataDirection(&Gpio, SWITCH_CHANNEL, 0xF); // Set SW3-SW0 as inputs (mask 0xF)
	    XGpio_SetDataDirection(&Gpio, LED_CHANNEL, 0x0); // Set LD3-LD0 as outputs (mask 0x0)
	//end user edits
	//Xil_DCacheDisable();

	xil_printf("\r\n--- Entering main() --- \r\n");


	//
	//Initialize the interrupt controller

	Status = fnInitInterruptController(&sIntc);
	if(Status != XST_SUCCESS) {
		xil_printf("Error initializing interrupts");
		return XST_FAILURE;
	}


	// Initialize IIC controller
	Status = fnInitIic(&sIic);
	if(Status != XST_SUCCESS) {
		xil_printf("Error initializing I2C controller");
		return XST_FAILURE;
	}

    // Initialize User I/O driver
    Status = fnInitUserIO(&sUserIO);
    if(Status != XST_SUCCESS) {
    	xil_printf("User I/O ERROR");
    	return XST_FAILURE;
    }


	//Initialize DMA
	Status = fnConfigDma(&sAxiDma);
	if(Status != XST_SUCCESS) {
		xil_printf("DMA configuration ERROR");
		return XST_FAILURE;
	}


	//Initialize Audio I2S
	Status = fnInitAudio();
	if(Status != XST_SUCCESS) {
		xil_printf("Audio initializing ERROR");
		return XST_FAILURE;
	}

	{
		XTime  tStart, tEnd;

		XTime_GetTime(&tStart);
		do {
			XTime_GetTime(&tEnd);
		}
		while((tEnd-tStart)/(COUNTS_PER_SECOND/10) < 20);
	}
	//Initialize Audio I2S
	Status = fnInitAudio();
	if(Status != XST_SUCCESS) {
		xil_printf("Audio initializing ERROR");
		return XST_FAILURE;
	}


	// Enable all interrupts in our interrupt vector table
	// Make sure all driver instances using interrupts are initialized first
	fnEnableInterrupts(&sIntc, &ivt[0], sizeof(ivt)/sizeof(ivt[0]));



	xil_printf("----------------------------------------------------------\r\n");
	xil_printf("Zybo Z7-20 DMA Audio Demo\r\n");
	xil_printf("----------------------------------------------------------\r\n");
	xil_printf("  Controls:\r\n");
	xil_printf("  BTN1: Record from MIC IN\r\n");
	xil_printf("  BTN2: Play on HPH OUT\r\n");
	xil_printf("  BTN3: Record from LINE IN\r\n");
	xil_printf("  Use switches choose tracks\r\n");
	xil_printf("----------------------------------------------------------\r\n");

    //main loop

    while(1) {


		// Checking the DMA S2MM event flag
		if (Demo.fDmaS2MMEvent)
		{
			xil_printf("\r\nRecording Done...");

			// Disable Stream function to send data (S2MM)
			Xil_Out32(I2S_STREAM_CONTROL_REG, 0x00000000);
			Xil_Out32(I2S_TRANSFER_CONTROL_REG, 0x00000000);

			Xil_DCacheInvalidateRange((u32) MEM_BASE_ADDR, 10*NR_AUDIO_SAMPLES);
			//microblaze_invalidate_dcache();
			// Reset S2MM event and record flag
			Demo.fDmaS2MMEvent = 0;
			Demo.fAudioRecord = 0;
		}

		// Checking the DMA MM2S event flag
		if (Demo.fDmaMM2SEvent)
		{
			xil_printf("\r\nPlayback Done...");

			// Disable Stream function to send data (S2MM)
			Xil_Out32(I2S_STREAM_CONTROL_REG, 0x00000000);
			Xil_Out32(I2S_TRANSFER_CONTROL_REG, 0x00000000);
			//Flush cache
			Xil_DCacheFlushRange((u32) MEM_BASE_ADDR, 10*NR_AUDIO_SAMPLES);
			//Reset MM2S event and playback flag
			Demo.fDmaMM2SEvent = 0;
			Demo.fAudioPlayback = 0;
		}

		// Checking the DMA Error event flag
		if (Demo.fDmaError)
		{
			xil_printf("\r\nDma Error...");
			xil_printf("\r\nDma Reset...");


			Demo.fDmaError = 0;
			Demo.fAudioPlayback = 0;
			Demo.fAudioRecord = 0;

		}

		// Checking the btn change event

		if(Demo.fUserIOEvent) {

			switch(Demo.chBtn) {
				case 'u':
					if (!Demo.fAudioRecord && !Demo.fAudioPlayback)
					{
						xil_printf("\r\nStart Recording...\r\n");
						fnSetMicInput();

						fnAudioRecord(sAxiDma,NR_AUDIO_SAMPLES);
						Demo.fAudioRecord = 1;
					}
					else
					{
						if (Demo.fAudioRecord)
						{
							xil_printf("\r\nStill Recording...\r\n");
						}
						else
						{
							xil_printf("\r\nStill Playing back...\r\n");
						}
					}
					break;
				case 'd':
					if (!Demo.fAudioRecord && !Demo.fAudioPlayback)
					{
						xil_printf("\r\nStart Playback...\r\n");
						fnSetHpOutput();
						fnAudioPlay(sAxiDma,NR_AUDIO_SAMPLES);
						Demo.fAudioPlayback = 1;
					}
					else
					{
						if (Demo.fAudioRecord)
						{
							xil_printf("\r\nStill Recording...\r\n");
						}
						else
						{
							xil_printf("\r\nStill Playing back...\r\n");
						}
					}
					break;
				case 'r':
					if (!Demo.fAudioRecord && !Demo.fAudioPlayback)
					{
						xil_printf("\r\nStart Recording...\r\n");
						fnSetLineInput();
						fnAudioRecord(sAxiDma,NR_AUDIO_SAMPLES);
						Demo.fAudioRecord = 1;
					}
					else
					{
						if (Demo.fAudioRecord)
						{
							xil_printf("\r\nStill Recording...\r\n");
						}
						else
						{
							xil_printf("\r\nStill Playing back...\r\n");
						}
					}
					break;
				case 'l':
					if (!Demo.fAudioRecord && !Demo.fAudioPlayback)
					{
						xil_printf("\r\nStart Playback...");
						fnSetLineOutput();
						fnAudioPlay(sAxiDma,NR_AUDIO_SAMPLES);
						Demo.fAudioPlayback = 1;
					}
					else
					{
						if (Demo.fAudioRecord)
						{
							xil_printf("\r\nStill Recording...\r\n");
						}
						else
						{
							xil_printf("\r\nStill Playing back...\r\n");
						}
					}
					break;
				default:
					break;
			}

			// Reset the user I/O flag
			Demo.chBtn = 0;
			Demo.fUserIOEvent = 0;


			sw_data = XGpio_DiscreteRead(&Gpio, SWITCH_CHANNEL); // Read SW3-SW0 state
			        led_data = sw_data & 0xF; // Mask the read data to consider only SW3-SW0
			        XGpio_DiscreteWrite(&Gpio, LED_CHANNEL, led_data); // Write the data to LD3-LD0
		}

    }

	xil_printf("\r\n--- Exiting main() --- \r\n");


	return XST_SUCCESS;

}









