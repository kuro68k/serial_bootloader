/*
 * eeprom.h
 *
 * Author:	Paul Qureshi
 * Created: 26/06/2012 13:25:42
 */


#ifndef EEPROM_H
#define EEPROM_H



/**************************************************************************************************
** Macros and inline functions
*/

// get the address of data in EEPROM when memory mapping is enabled
#define	EEP_MAPPED_ADDR(page, byte)	(MAPPED_EEPROM_START + (EEPROM_PAGE_SIZE * page) + byte)

#define EEP_EnablePowerReduction()	( NVM.CTRLB |= NVM_EPRM_bm )
#define EEP_DisablePowerReduction() ( NVM.CTRLB &= ~NVM_EPRM_bm )

// Execute NVM command. Timing critical, temporarily suspends interrupts.
// Atmel did a horrible job with this code, but it works so no point fixing it.
#define NVM_EXEC()	asm("push r30"      "\n\t"	\
					    "push r31"      "\n\t"	\
    					"push r16"      "\n\t"	\
    					"push r18"      "\n\t"	\
						"ldi r30, 0xCB" "\n\t"	\
						"ldi r31, 0x01" "\n\t"	\
						"ldi r16, 0xD8" "\n\t"	\
						"ldi r18, 0x01" "\n\t"	\
						"out 0x34, r16" "\n\t"	\
						"st Z, r18"	    "\n\t"	\
    					"pop r18"       "\n\t"	\
						"pop r16"       "\n\t"	\
						"pop r31"       "\n\t"	\
						"pop r30"       "\n\t"	\
					    )


/**************************************************************************************************
** Externally accessible functions
*/
void EEP_WaitForNVM( void );
void EEP_AtomicWritePage(uint8_t page_addr);
void EEP_EraseAll( void );



#endif
