/*
 * serial_bootloader.c
 *
 * Created: 29/04/2016 09:26:07
 * Author : Paul Qureshi
 */ 

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stddef.h>
#include "sp_driver.h"
#include "eeprom.h"
#include "protocol.h"


#define EEPROM_NUM_PAGES		(EEPROM_SIZE / EEPROM_PAGE_SIZE)
#define APP_SECTION_NUM_PAGES	(APP_SECTION_SIZE / APP_SECTION_PAGE_SIZE)
#if APP_SECTION_PAGE_SIZE < 255
	#define PAGE_INDEX_t	uint8_t
#else
	#define PAGE_INDEX_t	uint16_t
#endif


#define BOOTLOADER_VERSION	1

// USART settings, uses default 2MHz CPU clock
#define BL_USART			USARTC1
#define BL_BSEL				1539		// 19200
#define BL_BSCALE			-7
#define BL_CLK2X			USART_CLK2X_bm
// for half duplex RS485
#define	BL_CTRL_PORT		PORTC
#define	BL_CTRL_DE_PIN_bm	PIN4_bm
#define	BL_CTRL_nRE_PIN_bm	PIN5_bm
#define	BL_CTRL_RX_MODE		_delay_us(1000); BL_CTRL_PORT.OUTCLR = BL_CTRL_DE_PIN_bm | BL_CTRL_nRE_PIN_bm; BL_USART.DATA; BL_USART.DATA;
#define	BL_CTRL_TX_MODE		_delay_ms(10); BL_CTRL_PORT.OUTSET = BL_CTRL_DE_PIN_bm | BL_CTRL_nRE_PIN_bm; _delay_us(1000);

#define LED_PORT			PORTF
#define	LED_PIN_bm			PIN5_bm
#define	LED_ENABLE			do { LED_PORT.OUTCLR = LED_PIN_bm; } while(0)
#define	LED_DISABLE			do { LED_PORT.OUTSET = LED_PIN_bm; } while(0)
#define	LED_TOGGLE			do { LED_PORT.OUTTGL = LED_PIN_bm; } while(0)


typedef void (*AppPtr)(void) __attribute__ ((noreturn));

uint8_t		page_buffer[APP_SECTION_PAGE_SIZE];


/**************************************************************************************************
* Get a character from the USART
*/
inline uint8_t get_char(void)
{
	while (!(BL_USART.STATUS & USART_RXCIF_bm));
	return BL_USART.DATA;
}

/**************************************************************************************************
* Non-blocking get_char, returns 0 if no char available
*/
inline uint8_t get_char_nonblocking(void)
{
	if (BL_USART.STATUS & USART_RXCIF_bm)
		return BL_USART.DATA;
	return 0;
}

/**************************************************************************************************
* Send a character from the USART
*/
inline void put_char(uint8_t byte)
{
	while (!(BL_USART.STATUS & USART_DREIF_bm));
	BL_USART.DATA = byte;
}

/**************************************************************************************************
* Send a uint16 from the USART
*/
void put_uint16(uint16_t word)
{
	put_char(word & 0xFF);
	put_char((word >> 8) & 0xFF);
}

/**************************************************************************************************
* Send a uint32 from the USART
*/
void put_uint32(uint32_t word)
{
	put_char(word & 0xFF);
	put_char((word >> 8) & 0xFF);
	put_char((word >> 16) & 0xFF);
	put_char((word >> 24) & 0xFF);
}

/**************************************************************************************************
* Write to a CCP protected register
*/
void CCPWrite(volatile uint8_t *page, uint8_t value)
{
        uint8_t	saved_sreg;

        // disable interrupts if running
		saved_sreg = SREG;
		cli();
		
		volatile uint8_t * tmpAddr = page;
        RAMPZ = 0;

        asm volatile(
                "movw r30,  %0"       "\n\t"
                "ldi  r16,  %2"       "\n\t"
                "out   %3, r16"       "\n\t"
                "st     Z,  %1"       "\n\t"
                :
                : "r" (tmpAddr), "r" (value), "M" (CCP_IOREG_gc), "i" (&CCP)
                : "r16", "r30", "r31"
                );

        SREG = saved_sreg;
}

/**************************************************************************************************
* Main entry point
*/
int main(void)
{
	LED_PORT.DIR = LED_PIN_bm;
	
	// platform specific stuff
	PORTB.OUTSET = PIN2_bm | PIN3_bm;	// flood relay and bypass relay
	PORTB.DIRSET = PIN2_bm;
	PORTB.DIRSET = PIN3_bm;
	
	PORTC.DIRSET = PIN7_bm;	// USART TX

	// set up USART
	BL_USART.BAUDCTRLA = (uint8_t)BL_BSEL;
	BL_USART.BAUDCTRLB = (BL_BSCALE << 4) | (BL_BSEL >> 8);
	BL_USART.CTRLA = 0;
	BL_USART.CTRLB = USART_RXEN_bm | USART_TXEN_bm | BL_CLK2X;
	BL_USART.CTRLC = USART_CMODE_ASYNCHRONOUS_gc | USART_PMODE_DISABLED_gc | USART_CHSIZE_8BIT_gc;

	BL_CTRL_PORT.DIRSET = BL_CTRL_DE_PIN_bm | BL_CTRL_nRE_PIN_bm;
	BL_CTRL_RX_MODE;

	// timeout using RTC
	RTC.CTRL = RTC_PRESCALER_OFF_gc;						// make sure clock isn't running while we configure it
	CLK.RTCCTRL = CLK_RTCSRC_ULP_gc | CLK_RTCEN_bm;			// 1024Hz clock from ULP
	while (RTC.STATUS & RTC_SYNCBUSY_bm)					// essential to wait for this condition or the RTC doesn't work
		;
	RTC.PER = 0xFFFF;
	RTC.CNT = 0;
	RTC.INTCTRL = 0;
	RTC.CTRL = RTC_PRESCALER_DIV1_gc;

	LED_ENABLE;

	char c;
	do
	{
		if (RTC.CNT > 2048)			// 2 second time-out
		{
			// exit bootloader
			LED_DISABLE;
			RTC.CTRL = 0;
			AppPtr application_vector = (AppPtr)0x000000;
			CCP = CCP_IOREG_gc;		// unlock IVSEL
			PMIC.CTRL = 0;			// disable interrupts, set vector table to app section
			EIND = 0;				// indirect jumps go to app section
			RAMPZ = 0;				// LPM uses lower 64k of flash
			application_vector();
		}
		c = get_char_nonblocking();
	} while (c != CMD_NOP);
	BL_CTRL_TX_MODE;
	put_char(RES_OK);	// acknowledge start of bootloader
	BL_CTRL_RX_MODE;
	LED_DISABLE;

	//CCP = CCP_IOREG_gc;				// unlock IVSEL
	//PMIC.CTRL |= PMIC_IVSEL_bm;		// set interrupt vector table to bootloader section
	
	// bootloader
	for(;;)
	{
		c = get_char();
		asm("wdr");
		LED_TOGGLE;
		switch (c)
		{
			
			case CMD_ERASE_APP_SECTION:
				SP_WaitForSPM();
				SP_EraseApplicationSection();
				BL_CTRL_TX_MODE;
				put_char(RES_OK);
				BL_CTRL_RX_MODE;
				break;
			
			case CMD_WRITE_PAGE:
			{
				uint16_t page;
				page = get_char() << 8;
				page |= get_char();
				if (page >= APP_SECTION_NUM_PAGES)
				{
					BL_CTRL_TX_MODE;
					put_char(RES_FAIL);
					BL_CTRL_RX_MODE;
					break;
				}
				BL_CTRL_TX_MODE;
				put_char(RES_OK);
				BL_CTRL_RX_MODE;
				
				for (PAGE_INDEX_t i = 0; i < APP_SECTION_PAGE_SIZE; i++)
					page_buffer[i] = get_char();
				SP_WaitForSPM();
				SP_LoadFlashPage(page_buffer);
				SP_WriteApplicationPage(APP_SECTION_START + ((uint32_t)page * APP_SECTION_PAGE_SIZE));
				SP_WaitForSPM();
				BL_CTRL_TX_MODE;
				put_char(RES_OK);
				BL_CTRL_RX_MODE;
				break;
			}
			
			case CMD_READ_PAGE:
			{
				uint16_t page;
				page = get_char() << 8;
				page |= get_char();
				if (page >= APP_SECTION_NUM_PAGES)
				{
					put_char(RES_FAIL);
					break;
				}
				put_char(RES_OK);
				page *= APP_SECTION_PAGE_SIZE;
				
				put_uint16(APP_SECTION_PAGE_SIZE);
				for (PAGE_INDEX_t i = 0; i < APP_SECTION_PAGE_SIZE; i++)
					put_char(*(uint8_t *)page++);
				break;
			}
			
			case CMD_READ_FLASH_CRCS:
				put_char(RES_OK);
				put_uint32(SP_ApplicationCRC());
				put_uint32(SP_BootCRC());
				break;

			case CMD_READ_MCU_IDS:
				put_char(RES_OK);
				put_uint16(4);
				put_char(MCU.DEVID0);
				put_char(MCU.DEVID1);
				put_char(MCU.DEVID2);
				put_char(MCU.REVID);
				break;
			
			case CMD_READ_SERIAL:
			{
				put_char(RES_OK);
				put_uint16(11);
				for (uint8_t i = 0; i < 11; i++)
					put_char(SP_ReadCalibrationByte(offsetof(NVM_PROD_SIGNATURES_t, LOTNUM0) + i));
				break;
			}
			
			case CMD_READ_BOOTLOADER_VERSION:
				put_char(RES_OK);
				put_char(BOOTLOADER_VERSION);
				break;
			
			case CMD_RESET_MCU:
				put_char(RES_OK);
				_delay_ms(10);
				CCPWrite(&RST.CTRL, RST_SWRST_bm);
				nop();
				break;
			
			case CMD_READ_FUSES:
			{
				for (uint8_t i = 0; i < 6; i++)
					put_char(SP_ReadFuseByte(i));
				break;
			}

			case CMD_READ_EEPROM:
			{
				uint16_t page;
				page = get_char() << 8;
				page |= get_char();
				if (page >= EEPROM_NUM_PAGES)
				{
					put_char(RES_FAIL);
					break;
				}
				put_char(RES_OK);
				page *= EEPROM_PAGE_SIZE;
				uint8_t *ptr = (uint8_t *)page + MAPPED_EEPROM_START;
				
				put_uint16(EEPROM_PAGE_SIZE);
				for (uint8_t i = 0; i < EEPROM_PAGE_SIZE; i++)
					put_char(*ptr++);
				break;
			}
			
			case CMD_WRITE_EEPROM:
			{
				uint16_t page;
				page = get_char() << 8;
				page |= get_char();
				if (page >= EEPROM_NUM_PAGES)
				{
					put_char(RES_FAIL);
					break;
				}
				put_char(RES_OK);
				page *= EEPROM_PAGE_SIZE;
				uint8_t *ptr = (uint8_t *)page + MAPPED_EEPROM_START;
				for (uint8_t i = 0; i < EEPROM_PAGE_SIZE; i++)
					*ptr++ = get_char();
				EEP_AtomicWritePage(page);
				put_char(RES_OK);
				break;
			}

			case CMD_ERASE_USER_SIG_ROW:
				SP_WaitForSPM();
				SP_EraseUserSignatureRow();
				put_char(RES_OK);
				break;
			
			case CMD_READ_USER_SIG_ROW:
			{
				put_char(RES_OK);
				put_uint16(USER_SIGNATURES_PAGE_SIZE);
				for (PAGE_INDEX_t i = 0; i < USER_SIGNATURES_PAGE_SIZE; i++)
					put_char(SP_ReadUserSignatureByte(i));
				break;
			}
			
			case CMD_WRITE_USER_SIG_ROW:
			{
				for (PAGE_INDEX_t i = 0; i < USER_SIGNATURES_PAGE_SIZE; i++)
					page_buffer[i] = get_char();
				SP_WaitForSPM();
				SP_LoadFlashPage(page_buffer);
				SP_WriteUserSignatureRow();
				put_char(RES_OK);
				break;
			}
			
			case CMD_READ_MEMORY_SIZES:
				put_char(RES_OK);
				put_uint32(APP_SECTION_PAGE_SIZE);
				put_uint32(APP_SECTION_SIZE);
				put_uint32(BOOT_SECTION_PAGE_SIZE);
				put_uint32(BOOT_SECTION_SIZE);
				put_uint32(EEPROM_PAGE_SIZE);
				put_uint32(EEPROM_SIZE);
				break;
		}
	}
}
