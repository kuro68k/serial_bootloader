// sboot.c : Defines the entry point for the console application.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <windows.h>

#include "intel_hex.h"
#include "bootloader.h"
#include "getopt.h"
#include "libserialport/libserialport.h"

#define	DEFAULT_TIMEOUT_MS		1000


void WaitForBootloader(void);
void UpdateFirmware(void);
bool GetBootloaderInfo(void);


uint8_t target_mcu_id[4] = { 0, 0, 0, 0 };
uint8_t	target_mcu_fuses[6] = { 0, 0, 0, 0, 0, 0 };
char *hexfile = NULL;
char *port_name = NULL;
bool opt_list_ports = false;

struct sp_port *port;


/**************************************************************************************************
* Handle command line args
*/
int parse_args(int argc, char *argv[])
{
	int c;

	while ((c = getopt(argc, argv, "l")) != -1)
	{
		switch (c)
		{
		case 'l':
			opt_list_ports = true;
			break;

		case '?':
			printf("Unknown option -%c.\n", optopt);
			return 1;
		}
	}

	// non option arguments
	int j = 0;
	for (int i = optind; i < argc; i++)
	{
		//printf("Opt: %s\n", argv[i]);
		switch (j)
		{
		case 0:
			port_name = argv[i];
			break;

		case 1:
			hexfile = argv[i];
			break;

		default:
			printf("Too many arguments.\n");
			return 1;
		}
		j++;
	}

	if ((j < 2) && (!opt_list_ports))
	{
		printf("Usage: sboot [-l] <port> <firmware.hex>\n");
		printf("Example: sboot COM1 app.hex\n");
		printf("Options: -l    List ports\n");
		return 1;
	}

	//printf("hexfile: %s\n", hexfile);
	return 0;
}

/**************************************************************************************************
* List serial ports on machine
*/
void list_ports(void)
{
	/* A pointer to a null-terminated array of pointers to
	* struct sp_port, which will contain the ports found.*/
	struct sp_port **port_list;
 
	/* Call sp_list_ports() to get the ports. The port_list
	* pointer will be updated to refer to the array created. */
	enum sp_return result = sp_list_ports(&port_list);
 
	if (result != SP_OK) {
		printf("sp_list_ports() failed!\n");
		return;
	}
 
	/* Iterate through the ports. When port_list[i] is NULL
	* this indicates the end of the list. */
	int i;
	for (i = 0; port_list[i] != NULL; i++) {
		struct sp_port *port = port_list[i];
 
		/* Get the name of the port. */
		char *port_name = sp_get_port_name(port);
 
		printf("Found port: %s\n", port_name);
	}
 
	printf("Found %d ports.\n", i);
 
	/* Free the array created by sp_list_ports(). */
	sp_free_port_list(port_list);
}

/**************************************************************************************************
* Serial port function helper
*/
int check(enum sp_return result)
{
	/* For this example we'll just exit on any error by calling abort(). */
	char *error_message;
 
	switch (result) {
		case SP_ERR_ARG:
			printf("Error: Invalid argument.\n");
			abort();
		case SP_ERR_FAIL:
			error_message = sp_last_error_message();
			printf("Error: Failed: %s\n", error_message);
			sp_free_error_message(error_message);
			abort();
		case SP_ERR_SUPP:
			printf("Error: Not supported.\n");
			abort();
		case SP_ERR_MEM:
			printf("Error: Couldn't allocate memory.\n");
			abort();
		case SP_OK:
	default:
		return result;
	}
}

int main(int argc, char* argv[])
{
	int res;

	res = parse_args(argc, argv);
	if (res != 0)
		return res;

	if (opt_list_ports)
	{
		list_ports();
		return 0;
	}

	// load the hex file
	if (!ReadHexFile(hexfile))
		return -1;

	// open port
	if ((check(sp_get_port_by_name(port_name, &port)) != SP_OK) ||
		(check(sp_open(port, SP_MODE_READ_WRITE)) != SP_OK) ||
		(check(sp_set_baudrate(port, 19200)) != SP_OK) ||
        (check(sp_set_bits(port, 8)) != SP_OK) ||
        (check(sp_set_parity(port, SP_PARITY_NONE)) != SP_OK) ||
        (check(sp_set_stopbits(port, 1)) != SP_OK) ||
        (check(sp_set_flowcontrol(port, SP_FLOWCONTROL_NONE)) != SP_OK))
		return -1;

	// wait for bootloader to start
	WaitForBootloader();
	printf("Bootloader found.\n");

	UpdateFirmware();

#if 0
	// get bootloader info
	if (!GetBootloaderInfo(handle))
		return 1;

	// read .hex file
	if (!ReadHexFile(hexfile))
		return 1;

	// check firmware is suitable for target
	if (memcmp(&fw_info->mcu_signature, target_mcu_id, 3) != 0)
	{
		printf("Target MCU is wrong type.\n");
		return 1;
	}

	if (!UpdateFirmware(handle))
		return 1;

	printf("\nFirmware update complete.\n");
#endif
	return 0;
}

/**************************************************************************************************
* Look for bootloader
*/
void WaitForBootloader(void)
{
	printf("Waiting for bootloader... CTRL-C to cancel.\n");

	//char nop[] = "\0n\0";
	char nop = 'n';
	char res = 0;

	for (;;)
	{
		sp_blocking_write(port, &nop, 1, 10);
		sp_drain(port);
		sp_flush(port, SP_BUF_BOTH);

		if (sp_blocking_read(port, &res, 1, 10) == SP_OK)
		{
			if (res == 'A')
				return;
		}
	}
}

/**************************************************************************************************
* Bootloader command
*/
bool Command(char *cmd, int len)
{
	// clear buffers
	if (check(sp_flush(port, SP_BUF_BOTH)) != SP_OK)
	{
		printf("sp_flush() failed.\n");
		return false;
	}

	// set up page write
	if (check(sp_blocking_write(port, cmd, len, DEFAULT_TIMEOUT_MS)) != len)
	{
		printf("sp_blocking_write() failed.\n");
		return false;
	}

	// check response
	char res;
	if (check(sp_blocking_read(port, &res, 1, DEFAULT_TIMEOUT_MS)) != 1)
	{
		printf("sp_blocking_read() failed.\n");
		return false;
	}
	if (res != 'A')
	{
		printf("Bad response '%c'\n", res);
		return false;
	}
}

/**************************************************************************************************
* Write loaded firmware image to target
*/
void UpdateFirmware(void)
{
	int num_pages = fw_info->flash_size_b / fw_info->page_size_b;
	printf("Total pages:\t%d\n", num_pages);

	// erase app section
	printf("Erasing application section...\n");
	if (!Command("!", 1))
		return;

	// write app section
	printf("Writing firmware image...\n");
	for (int page = 0; page < num_pages; page++)
	{
		printf("Page %u of %u (%u%%)\n", page, num_pages, (page*100)/num_pages);

		// clear buffers
		if (check(sp_flush(port, SP_BUF_BOTH)) != SP_OK)
			return;

		// set up page write
		char cmd[3];
		cmd[0] = 'W';
		cmd[1] = (page >> 8) & 0xFF;
		cmd[2] = page & 0xFF;
		if (!Command(cmd, 3))
			return;

		// send page data
		if (check(sp_blocking_write(port, &firmware_buffer[page * fw_info->page_size_b], fw_info->page_size_b, DEFAULT_TIMEOUT_MS)) != fw_info->page_size_b)
		{
			printf("sp_blocking_write() failed when writing firmware image.\n");
			return;
		}

		// check response
		char res;
		if (check(sp_blocking_read(port, &res, 1, DEFAULT_TIMEOUT_MS)) != 1)
			return;
		if (res != 'A')
		{
			printf("Bad response '%c'\n", res);
			return;
		}
	}

	Command('#', 1);	// reset MCU
	
	// todo: check CRC

	return;
}

/**************************************************************************************************
* Check device serial number, MCU ID and fuses
*/
bool GetBootloaderInfo(void)
{
#if 0
	uint8_t buffer[BUFFER_SIZE];

	// serial number
	if (!ExecuteHIDCommand(handle, CMD_READ_SERIAL, 0, DEFAULT_TIMEOUT, buffer))
		return false;
	buffer[BUFFER_SIZE - 1] = '\0';		// ensure string is null terminated
	printf("Serial:\t\t%s\n", &buffer[3]);

	// MCU ID
	if (!ExecuteHIDCommand(handle, CMD_READ_MCU_IDS, 0, DEFAULT_TIMEOUT, buffer))
		return false;
	target_mcu_id[0] = buffer[3];
	target_mcu_id[1] = buffer[4];
	target_mcu_id[2] = buffer[5];
	target_mcu_id[3] = buffer[6];
	printf("MCU ID:\t\t%02X%02X%02X-%c\n", target_mcu_id[0], target_mcu_id[1], target_mcu_id[2], target_mcu_id[3] + 'A');

	// MCU fuses
	if (!ExecuteHIDCommand(handle, CMD_READ_MCU_IDS, 0, DEFAULT_TIMEOUT, buffer))
		return false;
	target_mcu_fuses[0] = buffer[3];
	target_mcu_fuses[1] = buffer[4];
	target_mcu_fuses[2] = buffer[5];
	target_mcu_fuses[3] = buffer[6];
	target_mcu_fuses[4] = buffer[7];
	target_mcu_fuses[5] = buffer[8];
	printf("MCU fuses:\t%02X %02X %02X %02X %02X %02X\n", target_mcu_fuses[0], target_mcu_fuses[1], target_mcu_fuses[2], target_mcu_fuses[3], target_mcu_fuses[4], target_mcu_fuses[5]);

#endif
	return true;
}
