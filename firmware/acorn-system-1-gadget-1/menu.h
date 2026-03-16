
#define MAX_BUT_COUNT 6
#define NUM_BUTTONS            3

// Debounce
#define MAX_BUT_COUNT          6

extern const int PIN_K1;
extern const int PIN_K2;
extern const int PIN_K3;

#define SW0_PIN 29
#define SW1_PIN 28
#define SW2_PIN 27

#define MENU_MAX  3

//------------------------------------------------------------------------------

typedef void (*FPTR)();
typedef void (*CMD_FPTR)(char *cmd);


typedef struct _BUTTON
{
  int     count;
  bool pressed;
  bool last_pressed;   // For edge detection
  FPTR    event_fn;
} BUTTON;

extern BUTTON buttons[NUM_BUTTONS];
enum ELEMENT_TYPE
  {
   BUTTON_ELEMENT = 10,
   SUB_MENU,
   MENU_END,
  };

struct MENU_ELEMENT
{
  enum ELEMENT_TYPE type;
  char *text;
  void *submenu;
  void (*function)(struct MENU_ELEMENT *e);
};

//------------------------------------------------------------------------------

extern struct MENU_ELEMENT *current_menu;
extern const struct MENU_ELEMENT home_menu[];
extern struct MENU_ELEMENT *last_menu;
extern struct MENU_ELEMENT *the_home_menu;

//------------------------------------------------------------------------------

void init_buttons(void);
void to_home_menu(struct MENU_ELEMENT *e);
