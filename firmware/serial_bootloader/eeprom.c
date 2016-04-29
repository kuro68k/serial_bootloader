/*
 * eeprom.c
 *
 * Author:	Paul Qureshi
 * Created: 26/06/2012 13:25:00
 */

#include <string.h>
#include <stdbool.h>
#include <avr/io.h>

#include "eeprom.h"


/**************************************************************************************************
** Wait for NVM access to finish.
*/
inline void EEP_WaitForNVM(void)
{
	while ((NVM.STATUS & NVM_NVMBUSY_bm) == NVM_NVMBUSY_bm)
		;
}

/**************************************************************************************************
** Write EEPROM page buffer to EEPROM memory. EEPROM will be erased before writing. Only page
** buffer locations that have been written will be saved, others will remain untouched.
**
** page_addr should be between 0 and EEPROM_SIZE/EEPROM_PAGE_SIZE
**
** N.B. EEPROM memory mapping must be disabled for this function to work.
*/
void EEP_AtomicWritePage(uint8_t page_addr)
{
	EEP_WaitForNVM();

	// Calculate page address
	uint16_t address = (uint16_t)(page_addr*EEPROM_PAGE_SIZE);

	// Set address
	NVM.ADDR0 = address & 0xFF;
	NVM.ADDR1 = (address >> 8) & 0x1F;
	NVM.ADDR2 = 0x00;

	// Issue EEPROM Atomic Write (Erase&Write) command
	NVM.CMD = NVM_CMD_ERASE_WRITE_EEPROM_PAGE_gc;
	NVM_EXEC();
	
	EEP_WaitForNVM();
}

/**************************************************************************************************
** Erase the entire EEPROM.
**
** N.B. EEPROM memory mapping must be disabled for this function to work.
*/
void EEP_EraseAll(void)
{
	memset((void *)MAPPED_EEPROM_START, 0xFF, EEPROM_SIZE);

	// Issue EEPROM Erase All command
	EEP_WaitForNVM();
	NVM.CMD = NVM_CMD_ERASE_EEPROM_gc;
	NVM_EXEC();
}
