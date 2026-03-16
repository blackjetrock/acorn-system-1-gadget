////////////////////////////////////////////////////////////////////////////////
//
// Switches
//
////////////////////////////////////////////////////////////////////////////////



//------------------------------------------------------------------------------
//

#define FILES_DIR  "/SYS1"

////////////////////////////////////////////////////////////////////////////////
//
// Main types of firmware
//
// SMALL:
// This firmware for a small PCB that has a 0.96" OLED display, a keyboard
// made form tactile switches and an SD catrd slot. There are some headers
// for unused GPIOs.
//
// DESKTOP:
// This is a PCB which has a Pico, a voltage regulator and circuitry to
// drive a capacitive touch keyboard and a large LCD screen. There is no
// keyboard or display on this PCB.

#define ESC_TYPE_SMALL         1
#define ESC_TYPE_DESKTOP       0

#define ESC_KBD_VOLT_TOUCH     0
#define ESC_KBD_QT             0

// Implement the extracode framework as instruction sin 100..199
// Otherwise coded instructions.
#define EXTRACODE_FRAMEWORK    0

// Suppressed output has no leading zeros and no + sign.
// This is closer to the original machine.

#define SUPPRESSED_OUTPUT      1
#define OLED_ON                1
#define SD_ON                  1
#define ERRORS_ON              0
#define WARNINGS_ON            0

// Original type display layout. Wider character areas, and decimal point
// squashed between characters
#define ORIGINAL_CHARACTERS    1

// Modern font is stabdard 5x7 font. Not modern is the original font.
#define MODERN_FONT            0
#define FONT_WIDTH             8

#define PIXEL_SCALE  3
#define PIXEL_SCALE2 2

//------------------------------------------------------------------------------
//
// Execution flow debug
//


#if DEBUG_ENTRY
#define FN_ENTRY         printf("\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>%s\n", __FUNCTION__)
#define FN_ENTRY_DISPLAY printf("\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>%s display:%d\n", __FUNCTION__, display)
#define FN_EXIT          printf("\n<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<%s\n", __FUNCTION__)
#else
#define FN_ENTRY_DISPLAY
#define FN_ENTRY
#define FN_EXIT
#endif
