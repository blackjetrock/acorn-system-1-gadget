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
const int PIN_NRDS      =  11;
const int PIN_SYNC      =  11;
const int PIN_PHI2      =  11;
const int PIN_RW        =  11;

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
  
////////////////////////////////////////////////////////////////////////////////

void cli_boot_mass(void)
{
  reset_usb_boot(0,0);
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
      'i',
      "Information",
      cli_information,
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

  set_gpio_input(PIN_NRST_DRV);
  set_gpio_input(PIN_NMI_DRV);
  set_gpio_input(PIN_IRQ_DRV);
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

  ////////////////////////////////////////////////////////////////////////////////
  //
  // Overclock as needed
  //
  ////////////////////////////////////////////////////////////////////////////////
  
#define OVERCLOCK 135000
  //#define OVERCLOCK 200000
  //#define OVERCLOCK 270000
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
  multicore_launch_core1(core1_main);

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
  if( !cd_to_dir("/HX20") )
    {
      printf("\nFailed to cd to /HX20 directory");
      printf("\n%s", sd_error);
    }
  else
    {
      printf("\n/HX20 directory found");
    }
  unmount_sd();

  sleep_ms(1000);
#endif

#if OLED_ON
  // Overall loop, which contains the polling loop and the menu loop
  oled_clear_display(&oled0);
  
  oled_set_xy(&oled0, 20, 0);
  oled_display_string(&oled0, "HX20");

  oled_set_xy(&oled0, 30, 8);
  oled_display_string(&oled0, "Cartridge");

  oled_set_xy(&oled0, 30, 16);
  oled_display_string(&oled0, "Emulator");
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
