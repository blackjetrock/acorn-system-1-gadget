///////////////////////////////////////////////////////////////////////////////
//
//
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// The data format used is an integer coded format
//
// registers
// A word is 8 decimal digits so can be coded in 32 bits
// A double word holds 12 decimal digits so will fit in a uint64
//
// Store
// The store uses words of 8 decimal digits. This fits in 32 bits
//
////////////////////////////////////////////////////////////////////////////////


#define DISPLAY_UPDATE         1
#define DISPLAY_NO_UPDATE      0
#define MAX_LINE               18
#define NUM_LINES              6
#define MAX_FILE_LINE          200

#define WORD_SIGN_PLUS         0xC
#define WORD_SIGN_MINUS        0xD
#define WORD_SIGN_NONE         0xF

typedef uint32_t SINGLE_WORD;
typedef uint64_t DOUBLE_WORD;
typedef uint32_t REGISTER_SINGLE_WORD;
typedef uint64_t REGISTER_DOUBLE_WORD;
typedef int BOOLEAN;
typedef uint32_t ADDRESS;

#define HX20_DIR "/HX20"
