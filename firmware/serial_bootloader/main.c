/*
 * serial_bootloader.c
 *
 * Created: 29/04/2016 09:26:07
 * Author : MoJo
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
#define BL_USART			USARTC0
#define BL_BSEL				11
#define BL_BSCALE			-7
#define BL_CLK2X			USART_CLK2X_bm


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
void CCPWrite(volatile uint8_t *address, uint8_t value)
{
        uint8_t	saved_sreg;

        // disable interrupts if running
		saved_sreg = SREG;
		cli();
		
		volatile uint8_t * tmpAddr = address;
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
	//CCP = CCP_IOREG_gc;				// unlock IVSEL
	//PMIC.CTRL |= PMIC_IVSEL_bm;		// set interrupt vector table to bootloader section

	// set up USART
	BL_USART.BAUDCTRLA = (uint8_t)BL_BSEL;
	BL_USART.BAUDCTRLB = (BL_BSCALE << 4) | (BL_BSEL >> 8);
	BL_USART.CTRLA = 0;
	BL_USART.CTRLB = USART_RXEN_bm | USART_TXEN_bm | BL_CLK2X;
	BL_USART.CTRLC = USART_CMODE_ASYNCHRONOUS_gc | USART_PMODE_DISABLED_gc | USART_CHSIZE_8BIT_gc;

	if (0)
	//if (!(PORTD.IN & PIN5_bm))	// jump to application
	{
		// exit bootloader
		AppPtr application_vector = (AppPtr)0x000000;
		CCP = CCP_IOREG_gc;		// unlock IVSEL
		PMIC.CTRL = 0;			// disable interrupts, set vector table to app section
		EIND = 0;				// indirect jumps go to app section
		RAMPZ = 0;				// LPM uses lower 64k of flash
		application_vector();
	}
	
	// bootloader
	for(;;)
	{
		uint8_t c = get_char();
		switch (c)
		{
			
			case CMD_ERASE_APP_SECTION:
				SP_WaitForSPM();
				SP_EraseApplicationSection();
				put_char(RES_OK);
				break;
			
			case CMD_WRITE_PAGE:
			{
				uint16_t address;
				address = get_char() << 8;
				address |= get_char();
				if (address >= APP_SECTION_NUM_PAGES)
				{
					put_char(RES_FAIL);
					break;
				}
				for (PAGE_INDEX_t i = 0; i < APP_SECTION_PAGE_SIZE; i++)
					page_buffer[i] = get_char();
				SP_WaitForSPM();
				SP_LoadFlashPage(page_buffer);
				SP_WriteApplicationPage(address);
				put_char(RES_OK);
				break;
			}
			
			case CMD_READ_PAGE:
			{
				uint16_t address;
				address = get_char() << 8;
				address |= get_char();
				if (address >= APP_SECTION_NUM_PAGES)
				{
					put_char(RES_FAIL);
					break;
				}
				address *= APP_SECTION_PAGE_SIZE;
				
				put_uint16(APP_SECTION_PAGE_SIZE);
				for (PAGE_INDEX_t i = 0; i < APP_SECTION_PAGE_SIZE; i++)
					put_char(*(uint8_t *)address++);
				break;
			}
			
			case CMD_READ_FLASH_CRCS:
				put_uint32(SP_ApplicationCRC());
				put_uint32(SP_BootCRC());
				break;

			case CMD_READ_MCU_IDS:
				put_uint16(4);
				put_char(MCU.DEVID0);
				put_char(MCU.DEVID1);
				put_char(MCU.DEVID2);
				put_char(MCU.REVID);
				break;
			
			case CMD_READ_SERIAL:
			{
				put_uint16(11);
				for (uint8_t i = 0; i < 11; i++)
					put_char(SP_ReadCalibrationByte(offsetof(NVM_PROD_SIGNATURES_t, LOTNUM0) + i));
				break;
			}
			
			case CMD_READ_BOOTLOADER_VERSION:
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
				uint16_t address;
				address = get_char() << 8;
				address |= get_char();
				if (address >= EEPROM_NUM_PAGES)
				{
					put_char(RES_FAIL);
					break;
				}
				address *= EEPROM_PAGE_SIZE;
				uint8_t *ptr = (uint8_t *)address + MAPPED_EEPROM_START;
				
				put_uint16(EEPROM_PAGE_SIZE);
				for (uint8_t i = 0; i < EEPROM_PAGE_SIZE; i++)
					put_char(*ptr++);
				break;
			}
			
			case CMD_WRITE_EEPROM:
			{
				uint16_t address;
				address = get_char() << 8;
				address |= get_char();
				if (address >= EEPROM_NUM_PAGES)
				{
					put_char(RES_FAIL);
					break;
				}
				address *= EEPROM_PAGE_SIZE;
				uint8_t *ptr = (uint8_t *)address + MAPPED_EEPROM_START;
				for (uint8_t i = 0; i < EEPROM_PAGE_SIZE; i++)
					*ptr++ = get_char();
				EEP_AtomicWritePage(address);
				put_char(RES_OK);
				break;
			}

			case CMD_ERASE_USER_SIG_ROW:
				SP_WaitForSPM();
				SP_EraseUserSignatureRow();
				break;
			
			case CMD_READ_USER_SIG_ROW:
			{
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
				put_uint16(APP_SECTION_PAGE_SIZE);
				put_uint16(APP_SECTION_SIZE);
				put_uint16(BOOT_SECTION_PAGE_SIZE);
				put_uint16(BOOT_SECTION_SIZE);
				put_uint16(EEPROM_PAGE_SIZE);
				put_uint16(EEPROM_SIZE);
				break;
		}
	}
}

