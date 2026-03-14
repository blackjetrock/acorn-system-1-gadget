//////////////////////////////////////////////////////////////////////////////
//
// Epson HX-20 Cartridge Emulator
//
////////////////////////////////////////////////////////////////////////////////
//
// Emulates a microcassette
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

#include "esc.h"

////////////////////////////////////////////////////////////////////////////////

void prompt(void);
void serial_help(void);

////////////////////////////////////////////////////////////////////////////////
// Microcassette

int pwsw = 0;

volatile int clk_edge_count  = 0;
volatile int pwsw_edge_count = 0;
volatile int wd_edge_count   = 0;

volatile int command_register = 0;

// Shadow of entire memory space
volatile uint8_t rom_data[65536];

#define ADDRESS_MASK  0xFFFF
#define WINDOW_START  0xB000

////////////////////////////////////////////////////////////////////////////////

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
const int PIN_K3        =  28;
const int PIN_K2        =  29;
const int PIN_K1        =  30;


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
    0xa2, 0x07, 0xbd, 0x10, 0xb0, 0x95, 0x10, 0xca, 0x10, 0xf8, 0x20, 0x0c, 0xfe, 0x4c, 0xfb, 0xfe,
    0x00, 0x77, 0x58, 0x5c, 0x50, 0x54, 0x00, 0x30,
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

////////////////////////////////////////////////////////////////////////////////

void cli_load_app_1(void)
{
  memcpy((void *)&(rom_data[0xb000]), (void *)app1, sizeof(app1));
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

void cli_dump_rom(void)
{
  for(int a=0xFE00; a<65536; a++)
    {
      if( (a % 64) == 0 )
        {
          printf("\n%04X: ", a);
        }

      printf(" %02X", rom_data[a]);
    }

  printf("\n");
  
}

//------------------------------------------------------------------------------

void cli_incr_b000(void)
{
  rom_data[0xb000]++;  
}

void cli_dump_window(void)
{
  for(int a=WINDOW_START; a<(WINDOW_START+256); a++)
    {
      if( (a % 64) == 0 )
        {
          printf("\n%04X: ", a);
        }

      printf(" %02X", rom_data[a]);
    }

  printf("\n");
  
}

void cli_dump_ram(void)
{
  for(int a=0x0000; a<0x0800; a++)
    {
      if( (a % 64) == 0 )
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
      
      printf(" %02X", rom_data[a]);
    }

  printf("\n");
  
}

////////////////////////////////////////////////////////////////////////////////

void prompt(void)
{
  printf("\n\n(Text Parameter:'%s'", text_parameter);
  printf("\n(Parameter %d >",
	 parameter);

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
  
  if( !cd_to_dir(HX20_DIR) )
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
//------------------------------------------------------------------------------

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
      '+',
      "Increment 0xB000",
      cli_incr_b000,
    },
    {
      'i',
      "Information",
      cli_information,
    },
    {
      'o',
      "Dump ROM",
      cli_dump_rom,
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
      cli_load_app_1,
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
// Emulate a RAM chip
//
////////////////////////////////////////////////////////////////////////////////

#define MSG_IN_CORE1 0

#define USB_FLAGS 0

#define RW_MASK (( 1<< PIN_NWDS) | ( 1<< PIN_NRDS))
#define RD_MASK  ( 1<< PIN_NRDS)
#define WR_MASK  ( 1<< PIN_NWDS)

#define IN_ADDR_WINDOW ((gpio_hi_states & 0xFF00) == 0xB000)

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
      if( ((gpio_states & WR_MASK) != WR_MASK) && IN_ADDR_WINDOW )
        {
          // Write
          
#if USB_FLAGS
          printf("\nW%04X", gpio_hi_states);
#endif
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
          
          rom_data[addr] = gpio_states & 0xFF;
          
#if USB_FLAGS
          printf("            D:%02X", rom_data[addr]);
#endif
          
        }
      
      if( ((gpio_states & RD_MASK) != RD_MASK) && IN_ADDR_WINDOW )
        {
#if USB_FLAGS
          printf("\nR%04X", gpio_hi_states);
#endif
          
          // Read
          // data lines outputs
          set_data_outputs();
	  
          // ROM emulation so always a read of us
          // get address
          gpio_hi_states  = sio_hw->gpio_hi_in;
          addr = (gpio_hi_states) & ADDRESS_MASK;
	  
          // Get data and present it on bus
          set_data(rom_data[addr]);
#if USB_FLAGS
          printf("       D:%02X", rom_data[addr]);
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
              if( (gpio_states & RD_MASK) != RD_MASK  )
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
  if( !cd_to_dir("/SYS1") )
    {
      printf("\nFailed to cd to /HX20 directory");
      printf("\n%s", sd_error);
    }
  else
    {
      printf("\n/SYS1 directory found");
    }
  unmount_sd();

  sleep_ms(1000);
#endif

#if OLED_ON
  // Overall loop, which contains the polling loop and the menu loop
  oled_clear_display(&oled0);
  
  oled_set_xy(&oled0, 20, 0);
  oled_display_string(&oled0, "Acorn System 1");

  oled_set_xy(&oled0, 30, 8);
  oled_display_string(&oled0, "Memory Emulator");

#endif

  // Run the shift register touch key scanning on the second core
  //  multicore_launch_core1(core1_main);


  while(1)
    {
#if DEBUG_LOOP
      printf("\nLoop");
#endif
      
      serial_loop();

    }
}
