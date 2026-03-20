//////////////////////////////////////////////////////////////////////////////
//
// Acorn System 1 Memiory Emulator
//
////////////////////////////////////////////////////////////////////////////////
//
// Emulates memory
//
// Set up to emulate 60K of memory, only page 0 is not emulated.
// PROMs removed and emulated
//
//
////////////////////////////////////////////////////////////////////////////////

#include "switches.h"
#include "version.h"

#pragma GCC diagnostic ignored "-Wwrite-strings"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/flash.h"
#include "hardware/clocks.h"
#include "hardware/structs/bus_ctrl.h"

#define DEBUG_STOP_LOOP while(1) {}

// Some logic to analyse:

#include "hardware/structs/pwm.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

#include "oled.h"
#include "sdcard.h"

#include "menu.h"

////////////////////////////////////////////////////////////////////////////////

#define MAX_FILE_LINE 200

////////////////////////////////////////////////////////////////////////////////
//
// Tracing of processor accesses
//

#define TRACE_ON 1

// How many addresses are traced
#define TRACE_N   32

volatile int trace_i = 0;
volatile int trace_running = 0;

volatile uint16_t trace_addr[TRACE_N];
volatile uint8_t trace_value[TRACE_N];
volatile char trace_type[TRACE_N];

////////////////////////////////////////////////////////////////////////////////

void prompt(void);
void serial_help(void);

////////////////////////////////////////////////////////////////////////////////

// How much RAM to display 
#define RAM_SIZE 1024

// Shadow of entire memory space
#define MEM_SIZE 65536

volatile uint8_t mem_data[MEM_SIZE];

#define ADDRESS_MASK  0xFFFF
#define WINDOW_START  0x8000

////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////

// Value that can be set from the keyboard
// Can be entered as hex or decimal

#define PAR_TYPE_DEC 0

int parameter = 0;
int keypress = 0;

#define TEXT_PARAMETER_LEN 40
char text_parameter[TEXT_PARAMETER_LEN+1] = "";

////////////////////////////////////////////////////////////////////////////////
//
// GPIOs
//

const int PIN_D0        =  0;
const int PIN_D1        =  1;
const int PIN_D2        =  2;
const int PIN_D3        =  3;
const int PIN_D4        =  4;
const int PIN_D5        =  5;
const int PIN_D6        =  6;
const int PIN_D7        =  7;

const int PIN_NRST_DRV  =  8;
const int PIN_NMI_DRV   =  9;
const int PIN_IRQ_DRV   =  10;
const int PIN_NWDS      =  11;
const int PIN_NRDS      =  12;
const int PIN_SYNC      =  13;
const int PIN_PHI2      =  14;
const int PIN_RW        =  15;

#define PIN_BITNUM_NWDS    11
#define PIN_BITNUM_NRDS    12

// SD card GPIOs here
const int PIN_K3        =  27;
const int PIN_K2        =  28;
const int PIN_K1        =  29;


const int PIN_A0        =  32;
const int PIN_A1        =  33; 
const int PIN_A2        =  34; 
const int PIN_A3        =  35; 
const int PIN_A4        =  36; 
const int PIN_A5        =  37; 
const int PIN_A6        =  38; 
const int PIN_A7        =  39; 
const int PIN_A8        =  40; 
const int PIN_A9        =  41; 
const int PIN_A10       =  42; 
const int PIN_A11       =  43; 
const int PIN_A12       =  44; 
const int PIN_A13       =  45; 
const int PIN_A14       =  46; 
const int PIN_A15       =  47; 


////////////////////////////////////////////////////////////////////////////////
//
// Chris oddy display
//

uint8_t app1[] =
  {
    0xa2, 0x07, 0xbd, 0x10, 0x80, 0x95, 0x10, 0xca, 0x10, 0xf8, 0x20, 0x0c, 0xfe, 0x4c, 0xfb, 0xfe,
    0x00, 0x77, 0x58, 0x5c, 0x50, 0x54, 0x00, 0x30,
  };


//------------------------------------------------------------------------------

uint8_t *app_list[] =
  {
    app1,
  };

////////////////////////////////////////////////////////////////////////////////

void set_gpio_output(const int gpio)
{
  gpio_init(gpio);
  gpio_set_dir(gpio, GPIO_OUT);
}

void set_gpio_input(const int gpio)
{
  gpio_init(gpio);
  gpio_set_dir(gpio, GPIO_IN);
}

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

void run_mount(void) {
  //    const char *arg = chk_dflt_log_drv(argc, argv);
  const char *arg = "0:";
  if (!arg)
        return;
    sd_card_t *sd_card_p = sd_get_by_drive_prefix(arg);
    if (!sd_card_p) {
        printf("Unknown logical drive id: \"%s\"\n", arg);
        return;
    }
    FATFS *fs_p = &sd_card_p->state.fatfs;
    FRESULT fr = f_mount(fs_p, arg, 1);
    if (FR_OK != fr) {
        printf("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    sd_card_p->state.mounted = true;
}

void run_unmount(void) {
  //    const char *arg = chk_dflt_log_drv(argc, argv);
  const char *arg = "0:";
  
  if (!arg)
        return;

    sd_card_t *sd_card_p = sd_get_by_drive_prefix(arg);
    if (!sd_card_p) {
        printf("Unknown logical drive id: \"%s\"\n", arg);
        return;
    }    
    FRESULT fr = f_unmount(arg);
    if (FR_OK != fr) {
        printf("f_unmount error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    sd_card_p->state.mounted = false;
    sd_card_p->state.m_Status |= STA_NOINIT;  // in case medium is removed
}

////////////////////////////////////////////////////////////////////////////////

void display_ram_at(uint8_t *dat)
{
  printf("\n");
  
  for(int z = 0; z<RAM_SIZE; z++)
    {
      int byte = 0;
      
      if( (z % 8) == 0)
	{
	  printf("\n%03X: ", z);
	}
      
      printf("%02X ", *(dat++));
    }
  
  printf("\n");

}

////////////////////////////////////////////////////////////////////////////////
//
// Flash data
//
// We have several flash program slots.
// Slots are in the top 1Mbyte of flash
//
// The flash has to be erased in 4096 byte aligned blocks
// Writes can be 256 byte aligned but due to erase being 4096 we use that slot size.

// Program and dat aram is stored at the same time, so it is a snapshot of the entire
// RAM. RAM plkus program is 256 bytes.


#define FLASH_PROGRAM_DATA_SIZE         256
#define FLASH_PROGRAM_SLOT_SIZE         4096
#define FLASH_PROGRAM_SLOT_AREA_SIZE    (1000*1024)
#define FLASH_PROGRAM_NUM_SLOTS         (FLASH_PROGRAM_SLOT_AREA_SIZE / FLASH_PROGRAM_SLOT_SIZE)

#define FLASH_SLOT_OFFSET (1024*1024)
uint8_t *flash_slot_contents   = (uint8_t *) (XIP_BASE + FLASH_SLOT_OFFSET);

// general buffer for flash read and write
uint8_t slot_buffer[FLASH_PROGRAM_DATA_SIZE];


////////////////////////////////////////////////////////////////////////////////
//
// Flash write/erase
//
////////////////////////////////////////////////////////////////////////////////

// Erase a slot
void erase_slot(int n)
{
  flash_range_erase(FLASH_SLOT_OFFSET+n*FLASH_PROGRAM_SLOT_SIZE, FLASH_PROGRAM_SLOT_SIZE);
}

// Checksum a slot
int checksum_slot(int slot_num)
{
  int csum = 0;

  for(int i=0; i<FLASH_PROGRAM_DATA_SIZE; i++)
    {
      csum += *(flash_slot_contents+slot_num*FLASH_PROGRAM_SLOT_SIZE+i);
    }

  return(csum);
}
void cli_erase_program_slot(void)
{
  printf("\nErasing program slot %d...", parameter);
  erase_slot(parameter);
  printf("\ndone.\n");
}

void cli_display_program_slot(void)
{
  
  printf("\nSlot %d\n", parameter);

  // First dump in hex
  display_ram_at(flash_slot_contents+parameter*FLASH_PROGRAM_SLOT_SIZE);

  printf("\n\n");
}

////////////////////////////////////////////////////////////////////////////////
//
// Load app, index is parameter
//

void cli_load_app(void)
{
  uint8_t *app_ptr;
  int num_apps = (sizeof(app_list) / sizeof(uint8_t *));
  
  printf("\n%d apps", num_apps);
  
  if( (parameter >= 0) && (parameter < num_apps) )
    {
      app_ptr = app_list[parameter];
      
      memcpy((void *)&(mem_data[0x8000]), (void *)app_list[parameter], sizeof(app1));

      printf("\nLoaded app %d", parameter);
    }
  else
    {
      printf("\nParameter out of range");
    }
  

}

////////////////////////////////////////////////////////////////////////////////

void cli_boot_mass(void)
{
  reset_usb_boot(0,0);
}

void cli_do_reset(void)
{
  printf("\nResetting...");
  gpio_put(PIN_NRST_DRV, 1);
  sleep_ms(50);
  gpio_put(PIN_NRST_DRV, 0);

  printf("\nDone");
  
}
////////////////////////////////////////////////////////////////////////////////
//
// Scans the directory and works out the next file number to use
//
// Leaves max_filenum with that value
//
////////////////////////////////////////////////////////////////////////////////

#define FILE_NAME_FORMAT "as1%04d.bin"
#define INFO_FILE_NAME_FORMAT "as1%04d.inf"

#define FILE_NAME_SCAN   "as1%d.bin"
#define FILE_NAME_GLOB   "as1*.bin"

int find_next_file_number(void)
{
  int file_n = 0;
  int num_listfiles = 0;
  int i;
  
  char cwdbuf[FF_LFN_BUF] = {0};
  FRESULT fr;
  char const *p_dir;

  //DEBUG_STOP;

  printf("\nAttempting to cd to directory...");
  if( !cd_to_dir(FILES_DIR) )
    {
      unmount_sd();
      return(0);
    }

  printf("\nGetting cwd...");
  fr = f_getcwd(cwdbuf, sizeof cwdbuf);

  // printf will print to console
  if (FR_OK != fr)
    {
      printf("f_getcwd error: %s (%d)\n", FRESULT_str(fr), fr);
      return(0);
    }
  
  p_dir = cwdbuf;

  printf("\nFile num search: %s\n", p_dir);
  
  DIR dj;      /* Directory object */
  FILINFO fno; /* File information */

  memset(&dj, 0, sizeof dj);
  memset(&fno, 0, sizeof fno);

  max_filenum = 0;
  
  fr = f_findfirst(&dj, &fno, p_dir, FILE_NAME_GLOB);

  if (FR_OK != fr)
    {
      printf("f_findfirst error: %s (%d)\n", FRESULT_str(fr), fr);
      return(0);
    }
  
  while( (fr == FR_OK) && fno.fname[0])
    { 
      if (fno.fattrib & AM_DIR)
	{
	  // Directory, we ignore these
	}
      else
	{
	  int filenum;
	  sscanf(fno.fname, FILE_NAME_SCAN, &filenum);

	  if( filenum > max_filenum )
	    {
	      max_filenum = filenum;
	    }
	}
       
      fr = f_findnext(&dj, &fno); /* Search for next item */
    }
  
  f_closedir(&dj);

  printf("\nFound next file number %d", max_filenum);
  return(1);
}

////////////////////////////////////////////////////////////////////////////////
//
// Writes the buffer to a file.
// Deletes any file that exists with the same name so that the resulting
// file is the same size as the buffer
// Searches the card directory for the highest file number and uses the next one
//
////////////////////////////////////////////////////////////////////////////////

void core_writefile(int address, int length)
{
  char filename[20];
  char info_filename[20];
  int i;
  FRESULT fr;
  FIL fp;
  size_t bytes_written = 0;
  char line[40];

  //DEBUG_STOP

  printf("\nWriting file to SD card");
  mount_sd();

  if( !cd_to_dir(FILES_DIR) )
    {
      unmount_sd();
      return;
    }

  find_next_file_number();
  sprintf(filename,      FILE_NAME_FORMAT,      ++max_filenum);
  sprintf(info_filename, INFO_FILE_NAME_FORMAT,   max_filenum);

  printf("\nWriting to %s", filename);
  
  oled_clear_display(&oled0);
  oled_set_xy(&oled0, 0, 0);
  oled_display_string(&oled0, "Writing");
  oled_set_xy(&oled0, 0, 8);
  oled_display_string(&oled0, filename);
  
  // Open file for writing
  fr = f_open(&fp, filename, FA_CREATE_NEW | FA_WRITE);

  if (FR_OK != fr && FR_EXIST != fr)
    {
      printf("\nOpen error");
      //      oled_error("Open error");
      unmount_sd();
      return;
    }

  printf("\nWriting data...");

  // Split up into smaller chunks
  int length_to_go = length;
  int addr = address;

#define CHUNK_SIZE 32
  
  while( length_to_go > 0 )
    {
      if( length_to_go > CHUNK_SIZE )
        {
          printf("\nWriting %d bytes at %04X", CHUNK_SIZE, addr);
          f_write (&fp, (uint8_t *)&(mem_data[addr]), CHUNK_SIZE, &bytes_written);
          length_to_go -= CHUNK_SIZE;
          addr += CHUNK_SIZE;
          printf(" %ld bytes written");
        }
      else
        {
          printf("\nWriting %d bytes at %04X", length_to_go, addr);
          f_write (&fp, (uint8_t *)&(mem_data[addr]), length_to_go, &bytes_written);
          length_to_go = 0;
          addr += length_to_go;
          printf(" %ld bytes written");
        }
    }
  
  
  fr = f_close(&fp);
  if (FR_OK != fr)
    {
      printf("f_close error: %s (%d)\n", FRESULT_str(fr), fr);
      unmount_sd();
      return;
    }

  //------------------------------------------------------------------------------
  //
  // Now write an info file to go with the data
  // Open file for writing
  fr = f_open(&fp, info_filename, FA_CREATE_NEW | FA_WRITE);

  if (FR_OK != fr && FR_EXIST != fr)
    {
      printf("\nOpen error");
      //      oled_error("Open error");
      unmount_sd();
      return;
    }

  printf("\nWriting data...");

  // Write the entire buffer in one go
  f_printf(&fp, "\n*ADDRESS:0000");
  f_printf(&fp, "\n*LENGTH:%04X", MEM_SIZE);
  
  fr = f_close(&fp);
  if (FR_OK != fr)
    {
      printf("f_close error: %s (%d)\n", FRESULT_str(fr), fr);
      unmount_sd();
      return;
    }

  oled_set_xy(&oled0, 0, 3*8);
  sprintf(line, "%d bytes", bytes_written);
  oled_display_string(&oled0, line);

  unmount_sd();

  printf("\nDone.\n");
}
//------------------------------------------------------------------------------
//
// Save all of memory to SD card
//

void cli_snapshot_memory(void)
{
  core_writefile(0x0000, MEM_SIZE);
}

//------------------------------------------------------------------------------

void cli_ls(void)
{
  printf("\nSD Card Listing:\n");
  run_mount();

  ls(FILES_DIR);
  run_unmount();
}

//------------------------------------------------------------------------------

void cli_dump_window(void)
{
  for(int a=WINDOW_START; a<(WINDOW_START+256); a++)
    {
      if( (a % 64) == 0 )
        {
          printf("\n%04X: ", a);
        }

      printf(" %02X", mem_data[a]);
    }

  printf("\n");
  
}


////////////////////////////////////////////////////////////////////////////////

#define BYTE_WIDTH 16

void cli_dump_ram(void)
{
  char ascii[BYTE_WIDTH*3+5];
  char ascii_byte[5];

  // Bound the address to 64K

  parameter %= 64*1024;
  
  // Display memory from address
  printf("\n");

  ascii[0] = '\0';
  
  for(unsigned int z = parameter; z<parameter+512; z++)
    {
      int byte = 0;
      
      if( (z % BYTE_WIDTH) == 0)
	{
	  printf("  %s", ascii);
	  ascii[0] = '\0';
	  printf("\n%08X: ", z);
	}

      byte = mem_data[z];

      printf(" %02X", byte);
      
      if( isprint(byte) )
	{
	  sprintf(ascii_byte, "%c", byte);
	}
      else
	{
	  sprintf(ascii_byte, ".");
	}
      
      strcat(ascii, ascii_byte);
    }
  
  printf("\n");

  parameter += 512;
}

//------------------------------------------------------------------------------

void cli_dump_ram2(void)
{
  for(int a=parameter; a<parameter+256; a++)
    {
      if( (a % 16) == 0 )
        {
          printf("\n%04X: ", a);
        }
      else
        {
          if( (a % 8) == 0 )
            {
              printf(" ");
            }
        }
      
      printf(" %02X", mem_data[a]);
    }

  printf("\n");
  
}

////////////////////////////////////////////////////////////////////////////////

void prompt(void)
{
  printf("\n\n(Text Parameter:'%s'", text_parameter);
#if PAR_TYPE_DEC
  printf("\n(Parameter %d >", parameter);
#else
  printf("\n(Parameter 0x%04X >", parameter);
#endif
}


////////////////////////////////////////////////////////////////////////////////
//
//
// We allow digits to enter a new value for the parameter

void cli_enter_parameter()
{
  int  key;
  int done = 0;

  printf("\nEnter parameter: (ESC or <RETURN> to exit)");
  
  parameter = 0;
  
  while(!done)
    {
      if( ((key = getchar_timeout_us(1000)) != PICO_ERROR_TIMEOUT))
	{
#if PAR_TYPE_DEC
	  switch(key)
	    {
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
	      parameter *= 10;
	      parameter += (key - '0');
	      prompt();
	      break;

	    case 27:
	    case 13:
	    case 10:
	      done = 1;
	      break;
	      
	    default:
	      break;
	    }
#else
	  switch(key)
	    {
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
	      parameter *= 16;
	      parameter += (key - '0');
	      prompt();
	      break;
              
	    case 'a':
	    case 'b':
	    case 'c':
	    case 'd':
	    case 'e':
	    case 'f':
	      parameter *= 16;
	      parameter += (key - 'a'+10);
	      prompt();
              break;
              
	    case 27:
	    case 13:
	    case 10:
	      done = 1;
	      break;
	      
	    default:
	      break;
	    }
#endif
	}
      else
	{
	  // I have found that I need to send something if the serial USB times out
	  // otherwise I get lockups on the serial communications.
	  // So, if we get a timeout we send a space and backspace it. And
	  // flush the stdio, but that didn't fix the problem but seems like a good idea.
	  stdio_flush();
	  //printf(" \b");
	}
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// Reads and prints a file
//

int cat_file(char *fn)
{
  char line[MAX_FILE_LINE];
  char fileline[MAX_FILE_LINE];

  mount_sd();
  
  if( !cd_to_dir(FILES_DIR) )
    {
      unmount_sd();
      return(0);
    }
  
  sprintf("Reading '%s'", fn);

  FF_FILE *fp = ff_fopen(fn, "r");

  if (fp == NULL)
    {
      printf("Failed to open:%s", fn);
      unmount_sd();
      return(0);
    }
  
  // Get lines from the file
  while( ff_fgets(&(fileline[0]), sizeof(fileline)-1, fp) != NULL )
    {
      printf("%s", fileline);
    }
  
  ff_fclose(fp);
  unmount_sd();
  return(1);
}

////////////////////////////////////////////////////////////////////////////////
//
// Reads a binary file into emulation memory at the address given
//

int read_binary_file(char *fn, int address)
{
  char line[MAX_FILE_LINE];
  char fileline[MAX_FILE_LINE];
  volatile uint8_t *ptr = &(mem_data[address]);

  printf("\nLoading %s", fn);
  
  mount_sd();
  
  if( !cd_to_dir(FILES_DIR) )
    {
      unmount_sd();
      return(0);
    }
  
  sprintf("Reading '%s'", fn);

  FF_FILE *fp = ff_fopen(fn, "r");

  if (fp == NULL)
    {
      printf("Failed to open:%s", fn);
      unmount_sd();
      return(0);
    }

  while(1)
    {
      // Read binary file data
      int ni = ff_fread((void *)ptr, 1, 1, fp);

      if( f_eof(fp) || (ni == 0))
        {
          // No more file
          ff_fclose(fp);
          unmount_sd();
          return(0);
        }

      ptr++;
    }
  
  ff_fclose(fp);
  unmount_sd();
  return(1);
}

////////////////////////////////////////////////////////////////////////////////
//
// trace what the processor is doing

void cli_trace_addresses(void)
{
  trace_i = 0;
  trace_running = 1;
}

void cli_dump_trace(void)
{
  printf("\nTrace\n");
  printf("\nTrace index;%d Trace running:%d\n", trace_i, trace_running);
  
  for(int i = 0; i< TRACE_N; i++)
    {
      printf("\n%02X: %04X %c %02X", i, trace_addr[i], trace_type[i], trace_value[i]);
    }
  printf("\n");
}

//------------------------------------------------------------------------------

void cli_read_file(void)
{
  read_binary_file("retrofest.bin", 0x8000);
}

void cli_load_monitor(void)
{
  read_binary_file("monitor.bin", 0xFE00);
}

void cli_load_basic(void)
{
  read_binary_file("System BASIC.bin", 0xC000);
}

void cli_load_cos40(void)
{
  read_binary_file("COS80A.bin", 0xF800);
}


void cli_version(void)
{
  printf("\nVersion:1.1.%d Compile time:%s %s\n", VERSION_INC, __DATE__, __TIME__);
}

void cli_digit(void)
{
  printf("\n%d", keypress);
}

void cli_information(void)
{
  // Set up OLED display
  i2c_init(&i2c_bus_0);
  
  oled_setup(&oled0);

  // Overall loop, which contains the polling loop and the menu loop
  oled_clear_display(&oled0);
  
  oled_set_xy(&oled0, 0, 0);
  oled_display_string(&oled0, "Acorn System 1");

  oled_set_xy(&oled0, 0, 8);
  oled_display_string(&oled0, "Memory Emulator");

  // Sets sd_ok flag for later use
  printf("\nInitialising SD card driver...");

#define SD_CARD 1

  // Initialise SD card driver
  sd_init_driver();

  // Mount and unmount the SD card to set the sd_ok_flag up
  mount_sd();
  unmount_sd();
  
  oled_set_xy(&oled0, 0,21);
  if( sd_ok_flag )
    {
      oled_set_xy(&oled0, 0, 16);
      oled_display_string(&oled0, "SD card OK");
      printf("\nSD card OK");
    }
  else
    {
      oled_set_xy(&oled0, 0, 16);
      oled_display_string(&oled0, "SD card NOT OK");
      printf("\nSD card NOT OK");
    }

}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Serial loop command structure

typedef void (*SERIAL_FPTR)(void);

typedef struct
{
  char key;
  char *desc;
  SERIAL_FPTR fn;
} SERIAL_COMMAND;

SERIAL_COMMAND serial_cmds[] =
  {
    {
      '?',
      "Serial command help",
      serial_help,
    },
    {
      '!',
      "Boot to mass storage",
      cli_boot_mass,
    },
    {
      'i',
      "Information",
      cli_information,
    },
    {
      'L',
      "Load binary file",
      cli_read_file,
    },
    {
      'S',
      "Save binary file",
      cli_read_file,
    },
    {
      'T',
      "Snapshot emulated memory",
      cli_snapshot_memory,
    },
    {
      'M',
      "Load monitor",
      cli_load_monitor,
    },
    {
      'B',
      "Load BASIC",
      cli_load_basic,
    },
    {
      'C',
      "Load COS40",
      cli_load_cos40,
    },
    {
      't',
      "Trace",
      cli_trace_addresses,
    },
    {
      'u',
      "Dump trace",
      cli_dump_trace,
    },
    {
      'l',
      "List Files",
      cli_ls,
    },
    {
      'a',
      "Dump RAM",
      cli_dump_ram,
    },
    {
      'w',
      "Dump memory window",
      cli_dump_window,
    },
    {
      'z',
      "Enter parameter value",
      cli_enter_parameter,
    },
    {
      'v',
      "Version",
      cli_version,
    },
    {
      'R',
      "Reset",
      cli_do_reset,
    },
    {
      '0',
      "*Digit",
      cli_digit,
    },
    {
      '1',
      "*Digit",
      cli_digit,
    },
    {
      '2',
      "*Digit",
      cli_digit,
    },
    {
      '3',
      "*Digit",
      cli_digit,
    },
    {
      '4',
      "*Digit",
      cli_digit,
    },
    {
      '5',
      "*Digit",
      cli_digit,
    },
    {
      '6',
      "*Digit",
      cli_digit,
    },
    {
      '7',
      "*Digit",
      cli_digit,
    },
    {
      '8',
      "*Digit",
      cli_digit,
    },
    {
      '9',
      "*Digit",
      cli_digit,
    },
    {
      '@',
      "Load app1",
      cli_load_app,
    },
  };


////////////////////////////////////////////////////////////////////////////////
//
// Serial CLI Handling
//
////////////////////////////////////////////////////////////////////////////////

int pcount = 0;
int periodic_read = 0;

void serial_loop()
{
  int  key;
  
  if( ((key = getchar_timeout_us(1000)) != PICO_ERROR_TIMEOUT))
    {
      for(int i=0; i<sizeof(serial_cmds)/sizeof(SERIAL_COMMAND);i++)
	{
	  if( serial_cmds[i].key == key )
	    {
#if DEBUG_SERIAL
	      printf("\nKey:%d (0x%02X)", key, key);
#endif
	      keypress = key;
	      (*serial_cmds[i].fn)();
	      prompt();
	      break;
	    }
	}
    }
}

void serial_help(void)
{
  printf("\n");
  
  for(int i=0; i<sizeof(serial_cmds)/sizeof(SERIAL_COMMAND);i++)
    {
      if( *(serial_cmds[i].desc) != '*' )
	{
	  printf("\n%c:   %s", serial_cmds[i].key, serial_cmds[i].desc);
	}
    }
  printf("\n0-9: Enter parameter digit");
}


void prompt_breakpoint(void)
{
  printf("\n\n(Text Parameter:'%s'", text_parameter);
}

////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//
// Put a value on the data bus

inline void set_data(BYTE data)
{
  int states;
  int dat = data & 0xff;
  
  // Direct register access to make things faster
  sio_hw->gpio_set = (  dat  << 0);
  sio_hw->gpio_clr = ((dat ^ 0xFF) << 0);
}


inline void set_data_inputs(void)
{
  sio_hw->gpio_oe_clr = 0x000000FF;
}

inline void set_data_outputs(void)
{
  sio_hw->gpio_oe_set = 0x000000FF;
}

////////////////////////////////////////////////////////////////////////////////
//
// Emulate memory
//
// Reads come from emulated memory unles out of range for emulation.
// Writes are always written to emulation memory, so all 64K has values.
//
//
////////////////////////////////////////////////////////////////////////////////

#define MSG_IN_CORE1 0

#define USB_FLAGS 0

#define RW_MASK (( 1<< PIN_NWDS) | ( 1<< PIN_NRDS))
#define RD_MASK  ( 1<< PIN_NRDS)
#define WR_MASK  ( 1<< PIN_NWDS)


// 16K from 0x8000
#define IN_16K_ADDR_WINDOW ((gpio_hi_states & 0xC000) == 0x8000)
#define IN_60K_ADDR_WINDOW ((gpio_hi_states & 0xF000) != 0x0000)
#define IN_64K_ADDR_WINDOW (1)


void ram_emulate(void)
{
  
  irq_set_mask_enabled( 0xFFFFFFFF, false );
  
  while(1)
    {
      uint32_t gpio_states;
      uint32_t gpio_hi_states;
      
      BYTE db;
      unsigned int addr;
      
      gpio_states     = sio_hw->gpio_in;
      gpio_hi_states  = sio_hw->gpio_hi_in;
      
      // Is this a write?
      if( ((gpio_states & WR_MASK) != WR_MASK) && IN_64K_ADDR_WINDOW )
        {
          // Write
          
          // data lines inputs
          //set_data_inputs();
          
          // Wait for read and write to go high then latch data
          while( ((gpio_states = sio_hw->gpio_in) & WR_MASK) != WR_MASK )
            {
            }
          
          // Falling edge of phi2, data valid for read and write
          gpio_hi_states  = sio_hw->gpio_hi_in;
          addr = (gpio_hi_states) & ADDRESS_MASK;
          
          mem_data[addr] = gpio_states & 0xFF;

#if TRACE_ON
          if( trace_running )
            {
              trace_addr[trace_i] = addr;
              trace_value[trace_i] = mem_data[addr];
              trace_type[trace_i++] = 'W';
              if( trace_i == TRACE_N )
                {
                  trace_running = 0;
                }
            }
#endif

        }
      
      if( ((gpio_states & RD_MASK) != RD_MASK) && IN_60K_ADDR_WINDOW )
        {
          // Read
          // data lines outputs
          set_data_outputs();
	  
          // ROM emulation so always a read of us
          // get address
          gpio_hi_states  = sio_hw->gpio_hi_in;
          addr = (gpio_hi_states) & ADDRESS_MASK;
	  
          // Get data and present it on bus
          set_data(mem_data[addr]);
#if USB_FLAGS
          printf("       D:%02X", mem_data[addr]);
#endif
          
          // Wait for RD and WR to be de-asserted
          while(1)
            {
              
              gpio_states = sio_hw->gpio_in;
              
#if MSG_IN_CORE1
              printf("\nWAIT %08X", gpio_states);
              printf("  %02X", gpio_get(CE_PIN));
#endif
              // We look for CE
              if( (gpio_states & RD_MASK) == RD_MASK  )
                {
                  // CE high, we are not selected
                  // data lines inputs
                  set_data_inputs();
#if MSG_IN_CORE1
                  printf("\nDONE WAIT");
#endif
                  break;
                }
            }

#if TRACE_ON
          if( trace_running )
            {
              trace_addr[trace_i] = addr;
              trace_value[trace_i] = mem_data[addr];
              trace_type[trace_i++] = 'R';
              if( trace_i == TRACE_N )
                {
                  trace_running = 0;
                }
            }
#endif

        }
    }
}


////////////////////////////////////////////////////////////////////////////////
//
//    Core 1 
//
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
//
//


////////////////////////////////////////////////////////////////////////////////
//
// Core 1 handles the emulation of memory
//
//

#define TICK 0

uint32_t now = 0;
uint32_t last_time = 0;

void core1_main(void)
{
}

////////////////////////////////////////////////////////////////////////////////
//
//
////////////////////////////////////////////////////////////////////////////////


int main(void)
{
#if 0
  // Do nothing, GPIOs all inputs
  while(1)
    {
    }
#endif
  
  ////////////////////////////////////////////////////////////////////////////////
  // Set up GPIOs
  set_gpio_input(PIN_D0);
  set_gpio_input(PIN_D1);
  set_gpio_input(PIN_D2);
  set_gpio_input(PIN_D3);
  set_gpio_input(PIN_D4);
  set_gpio_input(PIN_D5);
  set_gpio_input(PIN_D6);
  set_gpio_input(PIN_D7);
  
  set_gpio_output(PIN_NRST_DRV);
  set_gpio_output(PIN_NMI_DRV);
  set_gpio_output(PIN_IRQ_DRV);
  set_gpio_input(PIN_NWDS);
  set_gpio_input(PIN_NRDS);
  set_gpio_input(PIN_SYNC);
  set_gpio_input(PIN_PHI2);
  set_gpio_input(PIN_RW);

  // SD card GPIOs here
  set_gpio_input(PIN_K3);
  set_gpio_input(PIN_K2);
  set_gpio_input(PIN_K1);


  set_gpio_input(PIN_A0);
  set_gpio_input(PIN_A1);
  set_gpio_input(PIN_A2);
  set_gpio_input(PIN_A3);
  set_gpio_input(PIN_A4);
  set_gpio_input(PIN_A5);
  set_gpio_input(PIN_A6);
  set_gpio_input(PIN_A7);
  set_gpio_input(PIN_A8);
  set_gpio_input(PIN_A9);
  set_gpio_input(PIN_A10);
  set_gpio_input(PIN_A11);
  set_gpio_input(PIN_A12);
  set_gpio_input(PIN_A13);
  set_gpio_input(PIN_A14);
  set_gpio_input(PIN_A15);
  
  // Set outputs to safe levels
  
  gpio_put(PIN_NRST_DRV, 0);
  gpio_put(PIN_NMI_DRV, 0);
  gpio_put(PIN_IRQ_DRV, 0);
  
  ////////////////////////////////////////////////////////////////////////////////
  //
  // Overclock as needed
  //
  ////////////////////////////////////////////////////////////////////////////////
  
  //#define OVERCLOCK 135000
  //#define OVERCLOCK 200000
#define OVERCLOCK 270000
  //#define OVERCLOCK 360000
  
#if OVERCLOCK > 270000
  /* Above this speed needs increased voltage */
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(1000);
#endif
  
  /* Overclock */
  set_sys_clock_khz( OVERCLOCK, 1 );

  //stdio_init_all();
  stdio_usb_init();

  // Run the shift register touch key scanning on the second core
  multicore_launch_core1(ram_emulate);

  //sleep_ms(3000);

  printf("\n");
  printf("\n                                  ********************************************");
  printf("\n                                  *        Acorn System 1 Gadget             *");
  printf("\n                                  ********************************************");
  printf("\n");
  printf("\nVersion:1.1.%d Compile time:%s %s", VERSION_INC, __DATE__, __TIME__);
  printf("\n");
  
#if OLED_ON
  // Set up OLED display
  i2c_init(&i2c_bus_0);
  
  oled_setup(&oled0);
#endif
  
  // Sets sd_ok flag for later use
#if SD_ON   

  printf("\nInitialising SD card driver...");

#define SD_CARD 1

  // Initialise SD card driver
  sd_init_driver();

  // Mount and unmount the SD card to set the sd_ok_flag up
  mount_sd();
  unmount_sd();
  
  oled_set_xy(&oled0, 0,21);
  if( sd_ok_flag )
    {
      oled_display_string(&oled0, "SD card OK");
      printf("\nSD card OK");
    }
  else
    {
      oled_display_string(&oled0, "SD card NOT OK");
      printf("\nSD card NOT OK");
    }

  mount_sd();
  if( !cd_to_dir(FILES_DIR) )
    {
      printf("\nFailed to cd to %s directory", FILES_DIR);
      printf("\n%s", sd_error);
    }
  else
    {
      printf("\n%s directory found", FILES_DIR);
    }
  unmount_sd();

  sleep_ms(1000);
#endif

#if OLED_ON
  // Overall loop, which contains the polling loop and the menu loop
  oled_clear_display(&oled0);
  
  oled_set_xy(&oled0, 5, 0);
  oled_display_string(&oled0, "Acorn System 1");

  oled_set_xy(&oled0, 5, 16);
  oled_display_string(&oled0, "Memory Emulator");

#endif

  // Run the shift register touch key scanning on the second core
  //  multicore_launch_core1(core1_main);

  //------------------------------------------------------------------------------
  
  // Set up the OLED menu
  
  current_menu = (struct MENU_ELEMENT *) &(home_menu[0]);
  last_menu    = (struct MENU_ELEMENT *) &(home_menu[0]);
  the_home_menu = last_menu;

  //to_home_menu(NULL);

  //  init_buttons();

  //------------------------------------------------------------------------------
  // Load the monitor
  
  cli_load_monitor();
  
  //------------------------------------------------------------------------------
  
  while(1)
    {
#if DEBUG_LOOP
      printf("\nLoop");
#endif
      
      serial_loop();

    }
}
