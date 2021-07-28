/*
 * protocol.h
 *
 * Created: 29/03/2015 21:41:15
 *  Author: Paul Qureshi
 */ 


#ifndef PROTOCOL_H_
#define PROTOCOL_H_


#define RES_OK						'A'
#define RES_FAIL					'F'

#define	CMD_NOP						'n'
#define CMD_ERASE_APP_SECTION		'!'
#define CMD_WRITE_PAGE				'W'
#define CMD_READ_PAGE				'r'
#define CMD_READ_FLASH_CRCS			'c'
#define CMD_READ_MCU_IDS			'i'
#define CMD_READ_SERIAL				's'
#define CMD_READ_BOOTLOADER_VERSION	'v'
#define CMD_RESET_MCU				'#'
#define CMD_READ_FUSES				'f'
#define CMD_READ_EEPROM				'e'
#define CMD_WRITE_EEPROM			'E'
#define CMD_ERASE_USER_SIG_ROW		'*'
#define CMD_READ_USER_SIG_ROW		'u'
#define CMD_WRITE_USER_SIG_ROW		'U'
#define CMD_READ_MEMORY_SIZES		'm'


#endif /* PROTOCOL_H_ */
