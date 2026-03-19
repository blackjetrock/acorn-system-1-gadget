
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "switches.h"

#include "oled.h"
#include "sdcard.h"

#include "menu.h"


////////////////////////////////////////////////////////////////////////////////


// Number of buttons used for the menu system. The 'exit polling' button is not in this
// list, it is coded as a GPIO line as we don't want to waste time processing the menu
// buttons in the fast polling loop

int menu_offset = 0;
int file_offset = 0;
//int max_filenum = 0;
int menuloop_done = 0;

BUTTON buttons[NUM_BUTTONS];

void button_display(struct MENU_ELEMENT *e);
void button_list(struct MENU_ELEMENT *e);

//void but_ev_file_up();
//void but_ev_file_down();
void but_ev_file_select();

////////////////////////////////////////////////////////////////////////////////
// Menu system
//
void button_select_file(struct MENU_ELEMENT *e);

int but_pins[NUM_BUTTONS] = {SW0_PIN, SW1_PIN, SW2_PIN};


void but_ev_up();
void but_ev_down();
void but_ev_select();
void draw_menu(I2C_SLAVE_DESC *slave, struct MENU_ELEMENT *e, bool clear);


struct MENU_ELEMENT *current_menu;
int file_menu_size = 0;

struct MENU_ELEMENT *last_menu;
struct MENU_ELEMENT *the_home_menu;
int menu_selection = 0;
//unsigned int menu_size = 0;

#define MAX_LISTFILES 200
#define MAX_NAME 20

struct MENU_ELEMENT listfiles[MAX_LISTFILES+1];
int num_listfiles;
char names[MAX_LISTFILES][MAX_NAME];
char current_file[MAX_NAME+1];
int brightness_percent = 100;

// read the file with the given name into the buffer

void core_read(I2C_SLAVE_DESC *slave, char *arg);

////////////////////////////////////////////////////////////////////////////////



void loop_delay(int delay)
{
  volatile int x;
  volatile int y;

  for(x=0; x<delay; x++)
    {
      for(y=0; y<10; y++)
	{
	}
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// Puts up a list of the files on the SD card PAK directory, so one can be
// chosen.
//
// Only displays .opk files as they are the only ones that can be loaded.
//
////////////////////////////////////////////////////////////////////////////////


// returns the number of elements in a menu
int menu_size(struct MENU_ELEMENT *menu)
{
  int result = 0;
  
    while( menu->type != MENU_END )
    {
      result++;
      menu++;
    }
    
    return(result);
}

void button_list(struct MENU_ELEMENT *e)
{
  int file_n = 0;
  num_listfiles = 0;
  int i;
  
  char cwdbuf[FF_LFN_BUF] = {0};
  FRESULT fr;
  char const *p_dir;

  //DEBUG_STOP;

  mount_sd();
  
  if( cd_to_dir(FILES_DIR) )
    {
      unmount_sd();
      return;
    }

  f_chdrive("0:");
  fr = f_getcwd(cwdbuf, sizeof cwdbuf);

  if (FR_OK != fr)
    {
      printf("f_getcwd error: %s (%d)\n", FRESULT_str(fr), fr);
      unmount_sd();
      return;    f_chdrive("0:");
    }
  
  p_dir = cwdbuf;

  printf("Directory Listing: %s\n", p_dir);
  
  DIR dj;      /* Directory object */
  FILINFO fno; /* File information */

  memset(&dj, 0, sizeof dj);
  memset(&fno, 0, sizeof fno);

  fr = f_findfirst(&dj, &fno, p_dir, "*");

  if (FR_OK != fr)
    {
      printf("f_findfirst error: %s (%d)\n", FRESULT_str(fr), fr);
      unmount_sd();
      return;
    }
  
  while( (fr == FR_OK) && fno.fname[0] && (num_listfiles < MAX_LISTFILES) )
    { 
       if (fno.fattrib & AM_DIR)
	 {
	   // Directory, we gnore these
	 }
       else
	 {
	   char extension[40];
	   char name[80];
	   
	   // If the file has an extension of .opk then display it
	   // otherwise ignore.
	   extension[0] = '\0';
	   
	   if( sscanf(fno.fname, "%[^.].%s", name, extension) )
	     {
	       if( strcmp(extension, "opk") == 0 )
		 {
		   // Create a new menu element
		   // we also don't want to display anything before the offset
		   if( file_n >= file_offset )
		     {
		       // It is an opk file so display it
		       strncpy(&(names[num_listfiles][0]), fno.fname, MAX_NAME);
		       
		       //	display.println(&(names[nu);
		       listfiles[num_listfiles].text = &(names[num_listfiles][0]);
		       listfiles[num_listfiles].type = BUTTON_ELEMENT;
		       listfiles[num_listfiles].submenu = NULL;
		       listfiles[num_listfiles].function = button_select_file;
		       
		       num_listfiles++;
		     }
	   	   // Next file
		   file_n++;
		 }
	     }
	 }
       
      fr = f_findnext(&dj, &fno); /* Search for next item */
    }
  f_closedir(&dj);

  // terminate the menu
  listfiles[num_listfiles].text = "";
  listfiles[num_listfiles].type = MENU_END;
  listfiles[num_listfiles].submenu = NULL;
  listfiles[num_listfiles].function = button_select_file;


#if 1// We know how big the menu is now
  if( num_listfiles != 0 )
    {
      file_menu_size = num_listfiles;
    }
#endif

  // Button actions modified
  buttons[0].event_fn = but_ev_up;
  buttons[1].event_fn = but_ev_down;
  buttons[2].event_fn = but_ev_file_select;

  // Set up menu of file names
  current_menu = &(listfiles[0]);
  draw_menu(&oled0, current_menu, false);

  unmount_sd();
}

////////////////////////////////////////////////////////////////////////////////
// Menu system
//

// Debounce

struct MENU_ELEMENT listfiles[MAX_LISTFILES+1];
int num_listfiles;
char names[MAX_LISTFILES][MAX_NAME];
char current_file[MAX_NAME+1];


// The switch menu/OLED display system
void to_back_menu(struct MENU_ELEMENT *e)
{
  menu_selection = 0;
  menu_offset = 0;
  current_menu = last_menu;
  draw_menu(&oled0, current_menu, true);
}

void to_home_menu(struct MENU_ELEMENT *e)
{
  menu_selection = 0;
  menu_offset = 0;
  current_menu = the_home_menu;
  draw_menu(&oled0, current_menu, true);
}

void button_write(struct MENU_ELEMENT *e)
{
  //core_writefile(true);

  loop_delay(3000000);
  draw_menu(&oled0, current_menu, true);
}


// The button function puts up to the first 7 files on screen then set sup a button handler
// which will display subsequent pages.
// We use the menu structures to display the names and allow selection

// File selected
void button_select_file(struct MENU_ELEMENT *e)
{
}

//--------------------------------------------------------------------------------

// Move up in a menu

void but_ev_up()
{
  // Move up one place
  menu_selection--;

  // Keep within bounds
  if( menu_selection <= 0 )
    {
      menu_selection = 0;
    }

  // Now move offset up if it is greter than selection
  if( menu_offset > menu_selection )
    {
      menu_offset = menu_selection;
    }
  
  draw_menu(&oled0, current_menu, false);
}

void but_ev_down()
{
  // Move cursor down one entry
  menu_selection++;

  // Are we off the end of the menu?
  if( menu_selection == menu_size(current_menu) )
    {
      // At last element of menu
      menu_selection--;
    }

  // Do we need to pull the top element down?
  if( (menu_selection - menu_offset) >= MENU_MAX )
    {
      menu_offset = menu_selection - MENU_MAX+1;
      if( menu_offset < 0 )
	{
	  menu_offset = 0;
	}
    }
  
  draw_menu(&oled0, current_menu, false);
  //  button_list(NULL);
}

// Store file name and exit menu
// File can be read later

void but_ev_file_select()
{
  char line[40];
  
  strcpy(current_file, listfiles[menu_selection].text);
  file_offset = 0;

  oled_clear_display(&oled0);
  oled_set_xy(&oled0, 0,0);
  sprintf(line, "Selected File:");
  oled_display_string(&oled0, line);

  oled_set_xy(&oled0, 0,14);
  sprintf(line, "%s", current_file);
  oled_display_string(&oled0, line);

  loop_delay(3000000);

  // As well as selecting the file, read in in to the buffer as well
  //  core_read(&oled0, current_file);

  menu_selection = 0;
  to_home_menu(NULL);
  
  buttons[0].event_fn = but_ev_up;
  buttons[1].event_fn = but_ev_down;
  buttons[2].event_fn = but_ev_select;
}

void button_to_home(struct MENU_ELEMENT *e)
{
  to_home_menu(NULL);
}

#define COLUMNS 5
#define PAGE_LENGTH 30

// Display the buffer

int display_offset = 0;

void but_page_up()
{
  if( display_offset > PAGE_LENGTH )
    {
      display_offset -= PAGE_LENGTH;
    }
  else
    {
      display_offset = 0;
    }
  button_display(NULL);
}

void but_page_down()
{
#if 0
  display_offset += PAGE_LENGTH;
  
  if( display_offset >= bytecount )
    {
      display_offset = bytecount-PAGE_LENGTH;
    }

  if( display_offset < 0 )
    {
      display_offset = 0;
    }
  
  button_display(NULL);
#endif
}

void but_page_exit()
{
  display_offset = 0;
  draw_menu(&oled0,current_menu, true);

  buttons[0].event_fn = but_ev_up;
  buttons[1].event_fn = but_ev_down;
  buttons[2].event_fn = but_ev_select;

}

void button_display(struct MENU_ELEMENT *e)
{
#if 0
  int i;
  char ascii[17];
  char c[5];
  
  int ascii_i = 0;

  oled_clear_display(&oled0);
  
  //display.clearDisplay();
  
  for(i=0; (i<bytecount) && (i<PAGE_LENGTH); i++)
    {
      if( isprint(stored_bytes[i]) )
	{
	  sprintf(ascii, "%c", stored_bytes[i+display_offset]);
	}
      else
	{
	  sprintf(ascii, ".");
	}
      
      sprintf(c,     "%02X",  stored_bytes[i+display_offset]);

#if 0
      display.setCursor(6*15+(i%COLUMNS)*1*6, 8*(i/COLUMNS+1));
      display.println(ascii);
      display.setCursor(10*0+(i%COLUMNS)*2*8, 8*(i/COLUMNS+1));
      display.print(c);
#endif
    }

  // Drop into page up and down and exit buttoin handlers
  buttons[0].event_fn = but_page_up;
  buttons[1].event_fn = but_page_down;
  buttons[2].event_fn = but_page_exit;

  //display.display();
#endif
}


void button_exit(struct MENU_ELEMENT *e)
{
  menuloop_done = 1;
}

void button_send(struct MENU_ELEMENT *e)
{
  //send_databytes(true);
  draw_menu(&oled0, current_menu, true);
}

////////////////////////////////////////////////////////////////////////////////
//
//  Read the current file from SD card into the pak memory

void button_read(struct MENU_ELEMENT *e)
{
  //  core_read(&oled0, current_file);

  draw_menu(&oled0, current_menu, true);
}

////////////////////////////////////////////////////////////////////////////////
//
// Blanks the pak memory which will enable it to be sized
//
// Probably need an 'Are you sure?'
//
////////////////////////////////////////////////////////////////////////////////

extern uint8_t mem_data[];

void button_blank(struct MENU_ELEMENT *e)
{
  char line[40];
  
  for(int k=0; k< 100; k++)
    {
      mem_data[k] = 0xff;
    }

  oled_clear_display(&oled0);
  oled_set_xy(&oled0, 0, 0);
  sprintf(line, "Buffer Cleared");
  oled_display_string(&oled0, line);

  loop_delay(3000000);
  
  draw_menu(&oled0, current_menu, true);
}


const struct MENU_ELEMENT home_menu[] =
  {
   {BUTTON_ELEMENT, "List",                       NULL,     button_list},
   {BUTTON_ELEMENT, "Write",                      NULL,     button_write},
#if 0
   {BUTTON_ELEMENT, "Display",                    NULL,     button_display},
#endif   
   {BUTTON_ELEMENT, "Blank",                      NULL,     button_blank},
#if 0   
   {BUTTON_ELEMENT, "Read",                       NULL,     button_read},
#endif
   {BUTTON_ELEMENT, "Exit",                       NULL,     button_exit},
   {MENU_END,       "",                           NULL,     NULL},
  };

// Clear flag indicates whether we redraw the menu text and clear the screen. Or not.
//
// Menus are displayed starting at menu_offset and will only put
// MENU_MAX lines on the screen
// This allows eithee size of display to be used

void draw_menu(I2C_SLAVE_DESC *slave, struct MENU_ELEMENT *e, bool clear)
{
  int i = 0;
  char etext[50];

  e = current_menu;

  //DEBUG_STOP
  
  // Clear screen
  if(clear,1)
    {
      oled_clear_display(slave);
      //display.clearDisplay();
    }

  int entry_number = -1;
  
  while( e->type != MENU_END )
    {
      entry_number++;
      
      // Skip the first menu_offset entries
      if( entry_number < menu_offset )
	{
	  e++;
	  continue;
	}

      // Don't ever display more than MENU_MAX elements
      if( (entry_number - menu_offset) > MENU_MAX )
	{
	  break;
	}

      if( entry_number == menu_selection )
	{
	  sprintf(etext, ">%-19s", e->text);
	}
      else
	{
	  sprintf(etext, " %-19s", e->text);
	}
      
      switch(e->type)
	{
	case BUTTON_ELEMENT:
	  oled_set_xy(slave, 0, i*8);
	  //display.printChar(curs);
	  if( clear,1 )
	    {
	      oled_display_string(slave, etext);
	    }
	  break;

	case SUB_MENU:
	  oled_set_xy(slave, 0, i*8);
	  //	  display.setCursor(0, i*8);
	  //display.printChar(curs);
	  if ( clear,1 )
	    {
	      oled_display_string(slave, etext);
	      //	      display.println(etext);
	    }
	  break;
	}

      e++;
      i++;
    }
  
  //menu_size = i;

  // Blank the other entries
  //make sure menu_selection isn't outside the menu
  if( menu_selection >= menu_size(current_menu) )
    {
      menu_selection = menu_size(current_menu)-1;
    }

  for(; i<MENU_MAX; i++)
    {
      oled_set_xy(slave, 0, i*8);
      oled_display_string(slave, "               ");
      //      display.setCursor(0, i*8);
      //display.println("               ");
    }

#if 0  
  for(i=menu_offset; i<menu_offset+MENU_MAX-1;i++)
    {
      if( i == menu_selection )
	{
	  curs = ">";	  
	}
      else
	{
	  curs = " ";
	}

      oled_set_xy(slave, 0, i*8);
      oled_display_string(slave, curs);

      //display.setCursor(0, i*8);
      //display.print(curs);
    }
#endif  
#if 0
  char line[40];
  oled_set_xy(slave, 0, 8);
  sprintf(line, "%d %d %d %d  ", menu_selection, menu_offset, menu_size(current_menu), file_menu_size);
  oled_display_string(slave, line);
#endif
}

// Null button event function
void but_ev_null()
{ 
}

void but_ev_select()
{
  struct MENU_ELEMENT *e;
  int i = 0;
  
  // Do what the current selection says to do
  for(e=current_menu, i=0; (e->type != MENU_END); i++, e++)
    {
      if( i == menu_selection )
	{
	  switch(e->type)
	    {
	    case SUB_MENU:
	      current_menu = (struct MENU_ELEMENT *)e->submenu;
	      draw_menu(&oled0, current_menu, true);
	      break;
	      
	    default:
	      // Do action
	      (e->function)(e);
	      break;
	    }
	}
    }
}


void init_buttons()
{
  for(int i=0; i<NUM_BUTTONS; i++)
    {
      buttons[i].count = 0;
      buttons[i].pressed = false;
      buttons[i].last_pressed = false;
      buttons[i].event_fn = but_ev_null;
    }

  buttons[0].event_fn = but_ev_up;
  buttons[1].event_fn = but_ev_down;
  buttons[2].event_fn = but_ev_select;
}

void update_buttons()
{
  for(int i=0; i<NUM_BUTTONS; i++)
    {
      if( gpio_get(but_pins[i]) == 0 )
	{
	  if( buttons[i].count < MAX_BUT_COUNT )
	    {
	      buttons[i].count++;
	      if( buttons[i].count == MAX_BUT_COUNT )
		{
		  // Just got to MAX_COUNT
		  buttons[i].pressed = true;
		}
	    }
	}
      else
	{
	  if( buttons[i].count > 0 )
	    {
	      buttons[i].count--;
	      
	      if( buttons[i].count == 0 )
		{
		  // Just got to zero
		  buttons[i].pressed = false;
		}
	    }
	}
      
      // If button has gone from pressed to not pressed then we treat that as a key event
      if( (buttons[i].last_pressed == true) && (buttons[i].pressed == false) )
	{
	  (buttons[i].event_fn)();
	}

      buttons[i].last_pressed = buttons[i].pressed;
    }
}
