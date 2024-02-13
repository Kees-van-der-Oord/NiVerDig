/*
  NiVerDig Versatile Digital Pulse Processor and Scope
  Kees van der Oord <Kees.van.der.Oord@inter.nl.net>
NkDigPulse:
  version 0.1: Dec 31, 2018
  version 0.16: Jan 21, 2020
  version 0.19: Jan 31, 2020:
      on 16MHz Arduino Nano (clone):
      min timer up and down times: 100 us
      trigger delay: 32 +- 4 us
      pulse jitter: +- 4 us
  version 0.20: Feb 02, 2002:
     send to Bonn for pulsed laser control
     fixed possibility to set button pin to output
     unified tick_task (timer isr still 80 us ...
     in verbose = 1 mode, interrupt functions do not use serial
  version 0.21: Feb 02, 2002:
    removed store/restore_vars
    reduced size of task and pin tasks to fit in 256 byte EEPROM of Nano Every
    model, version, revision = 3 bytes
  version 0.22:
    moved EveryTimerB.h code to .h/.cpp library
  version 0.23:
    fixed concurrency issues in MegaAvr20Mhz
    @16Mhz timer micros() is 7us -> isr  56 us
    @20Mhz timer micros() is 34us -> isr 100 us due to micros() override in MegaAvr20MHz.h
  version 0.24:
    added check when adding to many pin/tasks
    fixed bug in showing task status
    added Serial EEPROM support (I2C connected)
  version 0.25:
    stop task command stops all tasks if no task is mentioned
    stop task task accepts * to stop all tasks
    set default wiring for new box
    2: BNC1
    3: BNC2
    4: BNC3
    5: red LED
    6: green LED
    7: blue LED
    8: button
  version 0.26: 4-12-2022
   created win32 appplication 'NiVerDig'
   commands are matched over full string length
   added tab to separate command arguments
   added parameter categories <>
   added 'halt' command to stop all tasks
   keep button one second pressed to activate halt on boot
   keep button five seconds pressed  
   set BAUD rate to 500000 for Uno/Mega (1000000 for the Every)
   added protection of SDA/SCL lines when EEPROM is defined (checkpins)
   added check of pins that supported interrupt
   added 'scope' function
NiVerDig/Sketch:   
  version 1/27: 06-12-2022: changed location/name to NiVerDig/Sketch/Sketch.ino
  version 2/28: 07-12-2022: scope mode does not read pin state again
  version 3/29: 01-01-2023: for Mega2560: fixed predefined macro and added interrupt clear flag instructions (EIFR)
  version 6/30: 12-01-2023: changed baud rate to NIS supported versions
  version 7/31: 12-01-2023: changed baud rate back to 500000 (works with NIS 460800 setting)
  version 8/32: 13-01-2023: changed terminator to CR+NL to satisfy both NIS \r and Arduino serial monitor \n
  version 10/33: 13-01-2023: fixed name first BNC, set default task count to 0
  version 12/34: 20-01-2023: no change: timestamp of fast pulses is overwritten: should be detected on PC side ...
  version 13/35: 05-05-2023: fixed crash in COM port list - added ADC support
  version 14/38: 26-08-2023: added support for the Uno R4
*/

#include <limits.h>
#include <EEPROM.h>

// to save variable memory RAM space, some constant strings are the program memory (PROGMEM)
#define FS(x) (__FlashStringHelper*)(x)

#define countof(A) (sizeof(A)/sizeof((A)[0]))
#define MAX_BYTE ((byte)0xFF)
#define MAX_ULONG 0xFFFFFFFF

//#define DEBUG             // define to get debug output set with 'verbose' 
//#define DEFAULTCONFIG     // defining this will NOT restore the pins and tasks from EEPROM
#define DEFAULT_VERBOSE 0  // set to 1 to get verbose output already during boot

// for more storage, a 24LC256 can be attached to the SDA/SCL lines (A4/A5) (the nano every needs this)
// Nano Every ATMega4809
#if defined(ARDUINO_ARCH_MEGAAVR)
#define EX_EEPROM_ADDR 0x50    // the 24LC256 base address (with all address pins to GND)
#define EX_EEPROM_SIZE 0x8000  // 24LC256 is 256kbit = 32768 bytes
#endif

#if defined(ARDUINO_FSP)
#undef PROGMEM
#define PROGMEM
#undef FS
#define FS(x) x
#undef F
#define F(x) x
#endif

#if defined(EX_EEPROM_ADDR)
#include "SerialEEPROM.h"
SerialEEPROM eeprom;
#define EEPROM_SIZE EX_EEPROM_SIZE // override definition of the internal EEPROM size 
#endif

// Timer1 (AVR), TimerB0 (MegaAVR), TimerGPT0 (RENESAS_UNO)
#if defined(ARDUINO_ARCH_AVR)
#include <TimerOne.h>
#define AVR_TIMER1_LIMIT 8388480 // about 8 seconds
#elif defined(ARDUINO_ARCH_MEGAAVR)
#include "EveryTimerB.h"
#define Timer1 TimerB2
#define AVR_TIMER1_LIMIT (MAX_LONG/2)
#elif defined(ARDUINO_ARCH_RENESAS_UNO) || defined(ARDUINO_PORTENTA_C33)
#include "UnoR4Timer.h"
UnoR4Timer TimerGPT0(TIMER_MODE_ONE_SHOT, GPT_TIMER, 0, TIMER_SOURCE_DIV_16);
#define Timer1 TimerGPT0
#define AVR_TIMER1_LIMIT (MAX_LONG/6) // 3 ticks per us, half range
#endif

// if you change the PINCOUNT, struct pin, TASKCOUNT, struct task: add isrs and increment MODEL (because the EEPROM layout changes)
#define MODEL       3
#define REVISION    2
#define VERSION     38
#define BAUD_RATE   500000 // for the uno and mega
#define EOL "\r\n"
#define BUTTON_PIN  8      // press the button during boot to set halt (1 second) or reset (5 seconds)
#define RED_LED_PIN 5      // used to blink when button is hold during boot

const char title[] PROGMEM = "NiVerDig: Versatile Digital Pulse Controller and Scope ";
const char info[] PROGMEM =
  " Outputs Digital Signals optionally synchronized with Digital Input Signals." EOL;

// Uno
#if defined(ARDUINO_AVR_UNO)
// version 26:
// Sketch uses 26596 bytes (82%) of program storage space. Maximum is 32256 bytes.
// Global variables use 1412 bytes (68%) of dynamic memory, leaving 636 bytes for local variables. Maximum is 2048 bytes.
#define HWPINCOUNT 14
bool checkpin(byte pin) { return pin < 14; }
#define EEPROM_SIZE 1024 // total: 3 + 140 + 310 = 450
#define PINCOUNT  4  // 10 * 14 = 140
#define TASKCOUNT 8  // 10 * 45 = 450
#define PinStatus int
#undef RED_LED_PIN   // uno model does not have LED mounted
#define ISRCOUNT 2
byte isr_pins[ISRCOUNT] = {2, 3};
byte checkAdcPin(byte pin) { if(pin > 6) return MAX_BYTE; return pin; }
#define USEADC
byte checkPwmPin(byte pin) { return ((pin != 3) && (pin != 5) && (pin != 6) && (pin != 9) && (pin != 10) && (pin != 11)) ? MAX_BYTE : pin; }

// Nano
#elif defined(ARDUINO_AVR_NANO)
#define HWPINCOUNT 14
bool checkpin(byte pin) { return pin < 14; }
#define EEPROM_SIZE 1024
#define PINCOUNT  4
#define TASKCOUNT 4
#define PinStatus int
#undef RED_LED_PIN   // nano model does not have LED mounted
#define ISRCOUNT 2
byte isr_pins[ISRCOUNT] = {2, 3};
byte checkPwmPin(byte pin) { return ((pin != 3) && (pin != 5) && (pin != 6) && (pin != 9) && (pin != 10)) ? MAX_BYTE : pin; }

// Mega2560
// version 26:
// Sketch uses 29268 bytes (11%) of program storage space. Maximum is 253952 bytes.
// Global variables use 4576 bytes (55%) of dynamic memory, leaving 3616 bytes for local variables. Maximum is 8192 bytes.
#elif defined(ARDUINO_AVR_MEGA2560) || defined(ARDUINO_AVR_ADK)
#define HWPINCOUNT 54
bool checkpin(byte pin) { return pin < 54; }
#define PINCOUNT  52 // (Mega has 54 pins minus 0,1 that are used for serial)
#define TASKCOUNT 52 
#define EEPROM_SIZE 4096
#define PinStatus int
#define ISRCOUNT 6
byte isr_pins[ISRCOUNT] = {2, 3, 18, 19, 20, 21};
byte checkAdcPin(byte pin) { if(pin > 16) return MAX_BYTE; return pin;}
#define USEADC
byte checkPwmPin(byte pin) { return ((pin >= 2) && (pin <= 13)) || ((pin >= 44) && (pin <= 46)) ? pin : MAX_BYTE; }

// Nano Every ATMega4809
// Version 26:
// Sketch uses 31483 bytes (64%) of program storage space. Maximum is 49152 bytes.
// Global variables use 2325 bytes (37%) of dynamic memory, leaving 3819 bytes for local variables. Maximum is 6144 bytes.
#elif defined(ARDUINO_ARCH_MEGAAVR)
#if !defined(EX_EEPROM_ADDR)
#define HWPINCOUNT  22
bool checkpin(byte pin) { return pin < HWPINCOUNT; }
#define EEPROM_SIZE 256
#define PINCOUNT    6
#define TASKCOUNT   5
#else // defined(EX_EEPROM_ADDR)
#define HWPINCOUNT 22
 // 18,19: I2C pins for EEPROM
bool checkpin(byte pin) { return (pin < HWPINCOUNT) && (pin != 18) && (pin != 19); }
#define PINCOUNT  20
#define TASKCOUNT 20
#endif // defined(EX_EEPROM_ADDR)
// 4 UART (RX/TX): UART3: Serial, UART1 (D0/D1) Serial1, UART2 D3/D6, UART0 D7/D2
#define ISRCOUNT 22
byte isr_pins[ISRCOUNT] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21};
#undef BAUD_RATE
#define BAUD_RATE  1000000
byte checkPwmPin(byte pin) { return ((pin != 3) && (pin != 5) &&  (pin != 9) && (pin != 10)) ? MAX_BYTE : pin; }

// Uno R4 Minima and Wifi Renesas RA4M1
#elif defined(ARDUINO_ARCH_RENESAS_UNO)
// version 38:
// Sketch uses 86648 bytes (33%) of program storage space. Maximum is 262144 bytes.
// Global variables use 8640 bytes (26%) of dynamic memory, leaving 24128 bytes for local variables. Maximum is 32768 bytes.
#undef BAUD_RATE
#define BAUD_RATE  2000000
#define HWPINCOUNT 14 
bool checkpin(byte pin) { return pin < HWPINCOUNT; }
#define PINCOUNT  22 // 14 digital pins, 8 analog pins
#define TASKCOUNT 52
#define EEPROM_SIZE 262143  // 14 * 20 + 52 * 45 = 14940
#undef RED_LED_PIN
#define RED_LED_PIN 11
#undef BUTTON_PIN
#define BUTTON_PIN 10
#define ISRCOUNT 14
byte isr_pins[ISRCOUNT] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
byte checkAdcPin(byte pin) { if(pin >= 6) return MAX_BYTE; return pin; }
#define USEADC R4 1.0.5: async mode does not work :-( 
// with the Arduino UNO R4 Boards pacakge 1.0.5, you have to remove the 'static' keyword from the 
// definition of the variable analog_values_by_channels on line 14 of 
// C:\Users\Kees\AppData\Local\Arduino15\packages\arduino\hardware\renesas_uno\1.0.5\cores\arduino\analog.cpp
// /*static*/ uint16_t analog_values_by_channels[MAX_ADC_CHANNELS] = {0};
byte checkPwmPin(byte pin) { return ((pin == 6) || (pin ==7)) ? MAX_BYTE: pin; }
//#define USEDAC

// Portenta C33
#elif defined(ARDUINO_PORTENTA_C33)
#define HWPINCOUNT 14
bool checkpin(byte pin) { return pin < 14; }
#define EEPROM_SIZE 2048 
#define PINCOUNT  14
#define TASKCOUNT 28
#undef RED_LED_PIN   // uno model does not have LED mounted
#define ISRCOUNT 14
byte isr_pins[ISRCOUNT] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13};
#undef BAUD_RATE
#define BAUD_RATE  250000
byte checkPwmPin(byte pin) { return pin; }

#else
#define HWPINCOUNT 100
bool checkpin(byte pin) { return true; }
#define EEPROM_SIZE 1024
#define PINCOUNT  6
#define TASKCOUNT 4
#define ISRCOUNT 0
byte isr_pins[1] = {-1};
byte checkPwmPin(byte pin) { return pin; }

#endif

// redefine the IDE EEPROM_SIZE definition when there is an external EEPROM
#if defined(EX_EEPROM_ADDR)
#undef EEPROM_SIZE
#define EEPROM_SIZE EX_EEPROM_SIZE
#endif

// pin and task names are 9 characters max
#define MAX_NAME_LENGTH 9  
#define MAX_NAME_SIZE (MAX_NAME_LENGTH+1)

// all descriptive text strings are 15 characters max
#define MAX_KEY_LENGTH 15 
#define MAX_KEY_SIZE (MAX_KEY_LENGTH+1)

// when comparing a BYTE variable with -1, make sure the -1 is really a BYTE !
#define NOTASK MAX_BYTE
#define NOPIN  MAX_BYTE
#define NOISR  MAX_BYTE
#define NOVAR  MAX_BYTE

typedef const char * (*pf_get_item_name)(byte i);

// global variables set by commands
byte          in_setup  = 1;   // set during setup()
byte          halt      = 0;   // stops all tasks
unsigned long echo      = 0;   // echo serial input
const char * const es   = "";  // empty string
const char * const ss   = " "; // single space
#if defined(DEBUG)
unsigned long verbose = DEFAULT_VERBOSE; // verbose: send diagnostic output to serial port
#endif

// general functions
byte parse_index_or_name(const char * s, void * pf);
unsigned long parse_ulong(const char * s);
long parse_long(const char * s);
unsigned long parse_byte(const char * s);
void print_time(unsigned long t);
unsigned long parse_time(const char * s);
byte parse_enum(const char * s, const char * const * e);
unsigned long parse_byte(const char * s);
void print_time(unsigned long t);
void cmd_info_var(byte cmd_index);
void print_options(char key[MAX_KEY_SIZE], byte o, const char *info);
unsigned long atoul(const char * s);
#if defined(DEBUG)
void print_free_memory(int line);
#endif
const char * find_index_key(char key[MAX_KEY_SIZE], const char * keys, byte index);
byte find_key_index(const char * keys, const char * key);
void report_changes();

// pin functions
void store_pins();
void set_pin_mode(struct pin & p, byte mode);

// task function
void arm_task(byte ti);
void stop_task(byte ti);
void start_task(byte ti, unsigned long tick);
byte parse_options(const char * s, const char *info);
void tick_tasks();
void tick_task(byte ti);
void check_finished_tasks();
bool task_enable_interrupt(byte ti);

// interrupt functions
void handle_pin_interrupt(byte N);

// commands
void cmd_void(byte cmd_index, byte argc, char**argv);
void cmd_info(byte cmd_index, byte argc, char**argv);
void cmd_set_var(byte cmd_index, byte argc, char**argv);
void cmd_reset(byte cmd_index, byte argc, char**argv);

// next_tick (us) are relative to the start_tick
// when the next_tick is longer than MAX_ULONG/4,
// both start_tick and next_tick are adjusted
#define MAX_LONG 2147483647
#define HMAX_LONG (MAX_LONG/2)
#define MIN_PERIOD 100   // isr takes 80us to complete, so 100 us the minimal timing

int  init_input();
int  read_input();
void process_input();
void check_eeprom_usage();

/////////////////////
// setup and loop
////////////////////


void setup()
{
  // Serial port initialization
  init_input();

  // welcome text
  Serial.print(FS(title)); Serial.print(MODEL); Serial.print(F(".")); Serial.print(REVISION); Serial.print(F(".")); Serial.print(VERSION); Serial.print(F(EOL));
  Serial.print(F("enter ? to see available commands" EOL));
  
  // EERPROM 
  check_eeprom_usage();
#if defined(EX_EEPROM_ADDR)
  eeprom.init(EX_EEPROM_ADDR, EX_EEPROM_SIZE);
#endif

  // test micrros and interrupts
#if defined(DEBUG)  
  if (verbose > 0) {
    test_micros();
    test_interrupts();
  }
#endif

  // timer
  Timer1.initialize();
  Timer1.attachInterrupt(timer_interrupt_callback);
  Timer1.stop();

  init_vars();

  // button actions on boot: 1 second press: HALT, 5 second press: RESEST
#ifdef BUTTON_PIN
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  halt = !digitalRead(BUTTON_PIN);
  if (halt) {
    // wait one second if the button is still pressed
    byte pressed;
    unsigned long start_tick = millis();
    while (pressed = !digitalRead(BUTTON_PIN)) {
      if (!pressed) break;
      unsigned long period = millis() - start_tick;
      if (period > 5000) break;
#ifdef RED_LED_PIN
      if (!(period % 500)) {
        unsigned long count = period / 500;
        if (count == 1) {
          pinMode(RED_LED_PIN, OUTPUT);
        }
        digitalWrite(RED_LED_PIN, !(count % 2));
      }
#endif      
    }
    if (pressed) {
      Serial.print("pin and task definitions reset to factory default because button pin is pressed 5 seconds" EOL);
      cmd_reset(0, 0, NULL);
      halt = 0;
    }
  }
#endif

#if defined(ARDUINO_ARCH_RENESAS_UNO)
    analogReadResolution(10);
#endif

  // actions
  reset_pin_states(0, -1);
  if (!halt) {
    arm_tasks(0, -1);
  } else {
    Serial.print("task executing halted because button pin is pressed during startup" EOL);
  }
}

void loop()
{
  // check if data has been sent from the computer:
  if (Serial.available())
  {
    if (read_input())
    {
      process_input();
    }
  }
  check_input_pins();
  tick_tasks();
  check_finished_tasks();
#if defined(DEBUG)  
  if (verbose) report_changes2();
#endif
}

/////////////////////
// pin
////////////////////

struct pin
{
  char          name[MAX_NAME_SIZE];  // 10 bytes
  unsigned char pin;                  // Arduino pin (0-based)
  unsigned char startup_mode;         // startup mode: input,output,pullup,pwm,adc,dac
  unsigned char startup_state;        // initial pin state
  unsigned char toggle_state;         // alternate pin state

  // starting from the 'state' variable the information is not saved in EEPROM
  unsigned char          intr_index; // index of isr
  unsigned char          mode;       // current mode: input,output,pullup,pwm,adc,dac
  volatile unsigned int  state;      // adc delivers 10 bits
  volatile unsigned long tick;       // us time-stamp of change
  volatile unsigned char changed;    // flag to indicate that state changed
}; // 23 bytes on 16-bits AVR, 25 bytes on 32-bits Renesas

// these var are initialized in reset_pins() that is called by Setup();
byte   pin_count;
struct pin pins[PINCOUNT];

// command to show information on the 'define pin' command dpin
void cmd_info_pins()
{
  Serial.print(F("Command dpin: Configure Pins" EOL));
  Serial.print(F(" dpin ?    : show all pin definitions" EOL));
  Serial.print(F(" dpin -[*] : delete last pin (* = all)" EOL));
  Serial.print(F(" dpin <index> <name> <pin> <mode> <init> <toggle>: define pin" EOL));
  Serial.print(F(" dpin <index>|<name> <setting> [=] <value>: change one property" EOL));
  Serial.print(F(" <index>  : [1 to ")); Serial.print(PINCOUNT); Serial.print(F("]" EOL));
  Serial.print(F(" <name>   : quoted pin name [")); Serial.print(MAX_NAME_LENGTH); Serial.print(F("]" EOL));
  Serial.print(F(" <pin>    : [0 to ")); Serial.print(HWPINCOUNT - 1); Serial.print(F("]" EOL));
  #if defined(USEADC)
  #if defined(USEDAC)
  Serial.print(F(" <mode>   : (output|input|pullup|pwm|adc|dac)" EOL));
  #else // USEDAC
  Serial.print(F(" <mode>   : (output|input|pullup|pwm|adc)" EOL));
  #endif // USEDAC
  #else // USEADC
  #if defined(USEDAC)
  Serial.print(F(" <mode>   : (output|input|pullup|pwm|dac)" EOL));
  #else // USEDAC
  Serial.print(F(" <mode>   : (output|input|pullup|pwm)" EOL));
  #endif // USEDAC
  #endif // USEADC
  Serial.print(F(" <init>   : <output> [0 to 1] (low|high) <pwm> [0 to 255]" EOL));
  Serial.print(F(" <toggle> : <pwm> [0 to 255]" EOL));
}
const char pin_prop_info[] PROGMEM = "name,pin,mode,init,toggle";
const char state_info[] PROGMEM    = "low,high";
#if defined(USEADC)
#if defined(USEDAC)
const char mode_info[] PROGMEM     = "output,input,pullup,pwm,adc,dac";
#else // USEDAC
const char mode_info[] PROGMEM     = "output,input,pullup,pwm,adc";
#endif // USEDAC
#else // USEADC
#if defined(USEDAC)
const char mode_info[] PROGMEM     = "output,input,pullup,pwm,dac";
#else // USEDAC
const char mode_info[] PROGMEM     = "output,input,pullup,pwm";
#endif // USEDAC
#endif // USEADC
#if defined(USEADC)
enum mode_indices { MODOUT, MODINP, MODPUP, MODPWM, MODADC, MODDAC };
byte mode_values[]                 = {OUTPUT, INPUT, INPUT_PULLUP, OUTPUT, INPUT, OUTPUT};
const char * const mode_code[]     = {"o", "i", "u", "p", "a", "d"};
#else // USEADC
enum mode_indices { MODOUT, MODINP, MODPUP, MODPWM, MODDAC };
byte mode_values[]                 = {OUTPUT, INPUT, INPUT_PULLUP, OUTPUT, OUTPUT};
const char * const mode_code[]     = {"o", "i", "u", "p", "d"};
#endif // USEADC

// command to print out the current pin definition in the 'dpin' format
void cmd_config_pins(byte b, byte e)
{
  char text[MAX_KEY_SIZE];
  Serial.print(F("index\tname\tpin\tmode\tinit\ttoggle" EOL));
  if (!pin_count) return;
  if (e >= pin_count) e = pin_count - 1;
  for (byte pi = b; pi <= e; ++pi)
  {
    struct pin & p = pins[pi];
    Serial.print(F(" "));
    Serial.print(pi + 1);
    Serial.print(F("\t'"));
    Serial.print(p.name);
    Serial.print(F("'\t"));
    Serial.print(p.pin); Serial.print(F("\t"));
    Serial.print(find_index_key(text, mode_info, p.startup_mode));
    Serial.print(F("\t"));
    Serial.print(p.startup_state);
    Serial.print(F("\t"));
    Serial.print(p.toggle_state);
    Serial.print(F(EOL));
  }
}

// command to output the current state of the pin
void cmd_status_pin(byte b, byte e)
{
  if (!pin_count) return;
  if (e >= pin_count) e = pin_count - 1;
  for (byte pi = b; pi <= e; ++pi)
  {
    struct pin & p = pins[pi];
    Serial.print(F("pin[")); Serial.print(pi + 1); Serial.print(F("]=")); Serial.print(p.state); Serial.print(F(": ")); Serial.print(mode_code[p.mode]); Serial.print(ss);
    switch (p.mode)
    {
      case MODINP:
      case MODOUT: Serial.print(F("[0 to 1] ")); break;
#if defined(USDADC)
      case MODADC: Serial.print(F("[0 to 1023] ")); break;
#endif
      case MODPWM: Serial.print(F("[0 to 255] ")); break;
#if defined(USEDAC)
      case MODDAC: Serial.print(F("[0 to 4095")); break;
#endif
    }
    Serial.print(F("'"));
    Serial.print(p.name);
    Serial.print(F("'"));
    switch (p.mode)
    {
      case MODINP:
      case MODOUT: Serial.print(F(" (low|high)"));
    }
    Serial.print(es);
    Serial.print(F(EOL));
  }
}

// command to set the current state of the pin
void cmd_pin_status(byte cmd_index, byte argc, char **argv)
{
  if (!argc)
  {
    cmd_status_pin(0, -1);
    return;
  }
  byte pi = parse_index_or_name(argv[0], (void*)&get_pin_name);
  if (pi == NOPIN)
  {
    Serial.print(F("pin error: first argument should be pin index or name." EOL));
    return;
  }
  struct pin & p = pins[pi];
  if (argc < 2)
  {
    Serial.print(F("pin[")); Serial.print(pi + 1); Serial.print(F("]=")); Serial.print(p.state); Serial.print(F(": ")); Serial.print(mode_code[p.mode]); Serial.print(F(EOL));
    return;
  }
  if(argv[1][0] == '?')
  {
    Serial.print(p.state); Serial.print(F(EOL));
    return;
  }
  if (mode_values[p.mode] != OUTPUT)
  {
    Serial.print(F("pin error: cannot set state of input pin." EOL));
    return;
  }
  unsigned long v = parse_ulong(argv[1]);
  if (p.mode == MODOUT)
  {
    v = v ? 1 : 0;
    digitalWrite(p.pin, v);
  }
#if defined(USEDAC)
  else if(p.mode == MODDAC)
  {
    if (v > 4095) v = 4095;
    analogWrite(p.pin, (byte)v);
  }
#endif
  else // pwm
  {
    if (v > 255) v = 255;
    analogWrite(p.pin, (byte)v);
  }
  p.tick = micros();
  p.state = v;
  p.changed = true;
  Serial.print(F("pin[")); Serial.print(pi + 1); Serial.print(F("]=")); Serial.print(v); Serial.print(F(EOL));
}

const char * get_pin_name(byte i)
{
  if (i >= pin_count) return NULL;
  return pins[i].name;
}

void clear_pin(int pi)
{
    struct pin & p = pins[pi];
    detachInterrupt(p.pin);
}

void clear_pins()
{
  for(int i = 0; i < pin_count; ++i)
  {
    clear_pin(i);
  }
}

void reset_pins()
{
  /*  name, pin, startup_mode, startup_state, state, intr_index, mode */
  clear_pins();
  struct pin default_pins[] = {

// the first model (UNO R3) has two BNCs and one button
#if defined(ARDUINO_AVR_UNO)
    {"BNC in",    2, MODINP, 0},
#if PINCOUNT >= 2
    {"BNC out",   3, MODOUT, 0},
#endif
#if PINCOUNT >= 3
    {"button",    8, MODPUP, 0},
#endif
// ARDUINO_AVR_UNO

// the second model (MEGA 2560) has a VGA-BNC cable with 5 BNCs, RGB LED and a button  
#elif defined(ARDUINO_AVR_MEGA2560)
    {"black BNC",  2, MODINP, 0},
    {"grey BNC",   3, MODINP, 0},
    {"red BNC" ,  18, MODINP, 0},
    {"green BNC", 19, MODINP, 0},
    {"blue BNC",  20, MODINP, 0},
    {"red LED",   5, MODOUT, 0},
    {"green LED", 6, MODOUT, 0},
    {"blue LED",  7, MODOUT, 0},
    {"button",   8, MODPUP, 0},
// ARDUINO_AVR_MEGA2560

// the third model (UNO R4) has 6 BNCs, a button and a RGB LED
// note: pin 6 and 7 do not support PWM, so these are skipped by default
#elif defined(ARDUINO_ARCH_RENESAS_UNO)
    {"BNC 1",      2, MODINP, 0},
    {"BNC 2",      3, MODINP, 0},
    {"BNC 3",      4, MODINP, 0},
    {"BNC 4",      5, MODINP, 0},
    {"BNC 5",      8, MODINP, 0},
    {"BNC 6",      9, MODINP, 0},
    {"button",    10, MODPUP, 0},
    {"red LED",   11, MODOUT, 0},
    {"green LED", 12, MODOUT, 0},
    {"blue LED",  13, MODOUT, 0},
    {"A0",         0, MODADC, 0},
// ARDUINO_ARCH_RENESAS_UNO

#else
    {"BNC in",    2, MODINP, 0},
    {"BNC out",   3, MODOUT, 0},
    {"button",    8, MODPUP, 0},

#endif 

  };
  pin_count = countof(default_pins);
  memcpy(pins, default_pins, sizeof(default_pins));
  init_pins();
}

void init_pins()
{
  for (int pi = 0; pi < pin_count; ++pi)
  {
    struct pin & p = pins[pi];
    p.mode = p.startup_mode;
    byte pin = p.pin;
    if(p.mode == MODADC) pin += A0;
    pinMode(pin, mode_values[p.mode]);
    update_pin_supports_intr(p);
  }
  if (!in_setup)
  {
    reset_pin_states(0, -1);
  }
}

void reset_pin_states(byte b, byte e)
{
  if (!pin_count) return;
  if (e >= pin_count) e = pin_count - 1;
  for (int pi = b; pi <= e; ++pi)
  {
    struct pin & p = pins[pi];
    if (mode_values[p.mode] == OUTPUT)
    {
      if (p.mode == MODOUT)
      {
        digitalWrite(p.pin, p.startup_state);
      }
      else
      {
        analogWrite(p.pin, p.startup_state);
      }
      p.state = p.startup_state;
    }
    else
    {
      word state;
#if defined(USEADC)      
      if (p.mode == MODADC)
      {
        state = analogRead(p.pin + A0);
      }
      else
#endif
      {
        state = digitalRead(p.pin);
      }
      if (state != p.state)
      {
        p.state = state;
      }
    }
  }
}

byte parse_pin_state(struct pin & p, byte default_value, const char * text)
{
  long lval = -1;
  lval = find_key_index(state_info, text);
  if (lval != 255)
  {
    // low or high found
    if ((p.mode == MODPWM) && (lval == 1))
    {
      lval = 255; // in PWM mode, high is 255
    }
#if defined(USEDAC)
    if ((p.mode == MODDAC) && (lval == 1))
    {
      lval = 4095; // in DAC mode, high is 4095
    }
#endif
  }
  else
  {
    lval = parse_long(text);
  }
  if (lval == -1)
  {
    return default_value;
  }
  if (p.mode == MODOUT)
  {
    lval = lval ? 1 : 0;
  }
#if defined(USEDAC)
  else if (p.mode == MODDAC)
  {
    lval = lval ? 1 : 0;
  }
#endif
  else // PWM
  {
    if (lval < 0) lval = 0;
    if (lval > 255) lval = 255;
  }
  return (byte) lval;
}

void cmd_pins(byte cmd_index, byte argc, char **argv)
{
  if (!argc || (argv[0][0] == '?'))
  {
    cmd_info_pins();
    cmd_config_pins(0, -1);
    return;
  }
  if (argc && (argv[0][0] == '-'))
  {
    if (argv[0][1] == '*') {
      clear_pins();
      pin_count = 0;
      Serial.print(F("all pins deleted." EOL));
      // to do: remove tasks that use these pins ?
    }
    else
    {
      if (pin_count)
      {
        --pin_count;
        clear_pin(pin_count);
        Serial.print(F("pin ")); Serial.print(pin_count + 1); Serial.print(F(" deleted." EOL));
        // to do: remove tasks that use this pin ?
      }
    }
    store_pins();
    return;
  }
  byte pi = parse_index_or_name(argv[0], (void*)&get_pin_name);
  if (pi == NOPIN)
  {
    // first argument is pin index
    pi = parse_ulong(argv[0]);
    if (pi != NOPIN) --pi;
    if ((pi == NOPIN) || (argc < 2))
    {
      Serial.print(F("pin argument error: first argument should be pin index or name. type 'pin' to see defined pins." EOL));
      return;
    }
    if (pi != pin_count)
    {
      Serial.print(F("pin argument error: please define pin ")); Serial.print(pin_count + 1); Serial.print(F(" first." EOL));
      return;
    }
    if (pi >= PINCOUNT)
    {
      Serial.print(F("error: no more new pins can be defined. type 'pin' to see defined pins." EOL));
      return;
    }
    // set a good standard pin
    byte p;
    for(p = 0; p < HWPINCOUNT; ++p)
    {
      byte pi2;
      for(pi2; pi2 < pi; ++pi2)
      {
        if(pins[pi2].pin == p)
        {
          break;
        }
      }
      if((pi2 == pi) && checkpin(p))
      {
        pins[pi].pin = p;
      }
      break;
    }
  }
  if (argc < 2)
  {
    // show info of this pin
    cmd_config_pins(pi, pi);
    return;
  }
  // configure the pin
  struct pin & p = pins[pi];
  byte val;
  byte prop_mode = 0; // set in <prop> = <value> syntax
  // argument 1 is new task name or the property name
  byte propi = 1; // property index: increments when prop_mode == 0
  byte argi = 1; // argument index: increments when prop_mode == 0
  // try to read a propery value
  if (argc > argi)
  {
    val = find_key_index(pin_prop_info, argv[argi]);
    if (val != (byte) - 1) {
      prop_mode = 1;
      propi = val + 1;
      argi = 2;
    }
  }
  // first argument is name
  if ((argc > argi) && (propi == 1))
  {
    strncpy(p.name, argv[argi], sizeof(p.name) - 1);
    p.name[sizeof(p.name) - 1] = 0;
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // second argument is pin number
  if ((argc > argi) && (propi == 2))
  {
    val = parse_byte(argv[argi]);
#if defined(BUTTON_PIN)
    if (val == BUTTON_PIN) p.mode = MODPUP; // button pin mode should always be pullup
#endif
    if(!checkpin(val)) val = NOPIN;
    if ((val != p.pin) && (val != NOPIN))
    {
      clear_pin(pi);
      p.pin = val;
    } 
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // third argument is pin mode
  if ((argc > argi) && (propi == 3))
  {
    val = find_key_index(mode_info, argv[argi]);
    if(val == MODPWM)
    {
      if(checkPwmPin(p.pin) == MAX_BYTE) {
        val = MODOUT;
      }
    }
#if defined(ADC)
    if(val == MODADC) 
    {
      if(checkAdcPin(p.pin) == MAX_BYTE) {
        p.pin = 0;
      }
    } 
    else 
#endif
    {
#if defined(BUTTON_PIN)
      if (p.pin == BUTTON_PIN) val = MODPUP; // button pin mode should always be pullup
#endif
    }
    if (val != MAX_BYTE) {
      p.startup_mode = val;
      p.mode = val;
    }
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // fourth argument is startup state
  if ((argc > argi) && (propi == 4))
  {
    p.startup_state = parse_pin_state(p, p.startup_state, argv[argi]);
    if (p.mode == MODOUT) p.toggle_state = !p.startup_state;
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // fifth argument is toggle state
  if ((argc > argi) && (propi == 5))
  {
    p.toggle_state = parse_pin_state(p, p.toggle_state, argv[argi]);
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  if (pi == pin_count) pin_count = pi + 1;
  store_pins();
  set_pin_mode(p, p.startup_mode);
  cmd_config_pins(pi, pi);
  reset_pin_states(pi, pi);
}

/////////////////////
// task
////////////////////

enum triggers
{
  TRGNO,       // no trigger: start immediately
  TRGSOFTWARE, // start from software only (serial or startup)
  TRGUP,       // start on up edge on pin
  TRGDOWN,     // start on down edge on pin
  TRGANY,      // start on any edge on pin
  TRGHIGH,     // start when pin is high
  TRGLOW,      // start when pin is low
  TRGSTART,    // start when another task starts
  TRGSTOP,     // start when another task stops
};
const char * const trigger_info[] = {"auto", "manual", "up", "down", "any", "high", "low", "start", "stop", NULL};
bool trigger_src_is_pin(byte trigger) {
  return (trigger >= TRGUP) && (trigger <= TRGDOWN);
}
enum actions
{
  ACTNONE,    // no action
  ACTNEGPULS, // negative pulse (start low)
  ACTPOSPULS, // positive pulse (start high)
  ACTTOGLPIN, // toggle pin low/high
#if defined(USEADC)  
  ACTSTAADC,  // start ADC conversion
  ACTLASTPIN = ACTSTAADC,
#else
  ACTLASTPIN = ACTTOGLPIN,
#endif
  ACTSTOTASK, // stop task
  ACTSTATASK, // start task
  ACTRESTASK, // restart task
  ACTARMTASK, // arm task
  ACTKICTASK, // kick task (stop if busy, start if idle)
};

const char action_info[] PROGMEM =
  "none,"
  "low,high,toggle,"
#if defined(USEADC)  
  "adc,"
#endif
  "stop,start,restart,arm,kick"
  ;
  
enum options
{
  OPTAUTOARM   = 0x01,   // arm-on-finish
  OPTSTARTARM  = 0x02,   // arm-on-startup
  OPTINTERRUPT = 0x04,   // interrupts
  OPTSTOP      = 0x80,   // stop !
};

const char option_info[] PROGMEM = "arm-on-finish,arm-on-startup,interrupts";
// counter counts down to 0 (finished), -1 is armed, -2 is idle
#define COUNT2STAT(A) ((A) + 2)
#define STAT2COUNT(A) ((A) - 2)
enum counters
{
  CURIDLE = -2,
  CURARMED = -1,
  CURFINISHED = 0,
  CURFIRED
};

// triggered state:
enum { NOT_TRIGGERED, TICK_TRIGGERED, START_TRIGGERED, START_TICK_TRIGGERED};
// count number used for continuous (max long / 2 - 1)
#define CNTCONTINUOUS 1073741822  // must be even 

// if you change the definition of task, increment MODEL
struct task {
  char          name[MAX_NAME_SIZE]; // name: 10 bytes
  byte          trigger;             // software, auto, up, down, any
  byte          srcpin;              // source pin:
  byte          dstpin;              // destination pin
  byte          action;              // up, down, pulse up, pulse down
  long          count;               // number of repeats:  0: set once; 1-n: repeat
  unsigned long waittime;            // delay (us)
  unsigned long uptime;              // up-time (us)
  unsigned long downtime;            // down-time (us)
  byte          options;             // OPTAUTOSTART, OPTSTARTARM, OPT_INTERRUPTS
  // starting from counter, the members are not saved in EEPROM !
  long          counter;             // counting down !
  unsigned long starttick;           // time of start (us)
  unsigned long nexttick;            // next moment of action (us)
  byte          triggered;           // NOT_TRIGGERED, TICK_TRIGGERED, START_TRIGGERED, START_TICK_TRIGGERED
  byte          changed;             // status has changed during interrupt
}; // 45 bytes on 16-bits AVR and on 32-bits Renesas

// these vars are initialized in reset_tasks() called from Setup()
byte task_count;
struct task tasks[TASKCOUNT];

void reset_tasks()
{
  task_count = 0;
/*    
  struct task default_tasks[TASKCOUNT] = {
#if (PINCOUNT >= 7)
    {"hallo RED", TRGNO,   0, 3, ACTPOSPULS, 1, 0, 500000, 0,     OPTSTARTARM,                        CURIDLE}, // blink red LED once on boot
    {"blink GRE", TRGUP,   6, 4, ACTPOSPULS, 2, 0, 200000, 200000, OPTSTARTARM | OPTAUTOARM | OPTINTERRUPT, CURIDLE}, // blink green LED twice when button is pressed
    {"blink BLU", TRGSTOP, 1, 5, ACTPOSPULS, 2, 0, 200000, 200000, OPTSTARTARM | OPTAUTOARM | OPTINTERRUPT, CURIDLE} // blink blue LED twice next
  };
  task_count = 3;
#endif
  memcpy(tasks, default_tasks, sizeof(default_tasks));
*/
  init_tasks(0, -1);
}

void init_tasks(byte b, byte e)
{
  if (b >= task_count) return;
  if (e >= task_count) e = task_count - 1;
  for (byte ti = b; ti <= e; ++ti)
  {
    struct task & t = tasks[ti];
    t.counter = CURIDLE;
    t.triggered = (t.options & OPTINTERRUPT) ? TICK_TRIGGERED : NOT_TRIGGERED;
  }
  if (!in_setup && !halt)
  {
    arm_tasks(b, e);
  }
}

void arm_tasks(byte b, byte e)
{
  if (b >= task_count) return;
  if (e >= task_count) e = task_count - 1;
  for (byte ti = b; ti <= e; ++ti)
  {
    struct task & t = tasks[ti];
    if (t.options & OPTSTARTARM)
    {
      arm_task(ti);
    }
  }
}

void cmd_info_tasks()
{
  Serial.print(F("Command task: define/delete task" EOL));
  Serial.print(F(" dtask ?    : show all task definitions" EOL));
  Serial.print(F(" dtask -[*] : delete last task (* = all)" EOL));
  Serial.print(F(" dtask <index> <name> <trigger> <source> <action> <target> <count> <delay> <up> <down> <options>" EOL));
  Serial.print(F(" dtask <index>|<name> <property> [=] <value>" EOL));
  Serial.print  (F(" <index>  : [1 to ")); Serial.print(TASKCOUNT); Serial.print(F("]" EOL));
  Serial.print(F(" <name>   : quoted task name [")); Serial.print(MAX_NAME_LENGTH); Serial.print(F("]" EOL));
  Serial.print(F(" <trigger>: <software> (auto|manual) <input-pin> (up|down|any|high|low) <in-task> (start|stop)" EOL));
  Serial.print(F(" <source> : <input-pin> (input pin-index or pin-name) <in-task> (task-index or task-name)" EOL));
#if defined(USEADC)
  Serial.print(F(" <action> : <output-pin> (low|high|toggle) <out-task> (arm|start|restart|stop|kick) <adc> (adc) <none> (none)" EOL));
  Serial.print(F(" <target> : <output-pin> (output pin-index or pin-name) <out-task> (task-index or task-name) <adc> (adc pin-index or pin-name) <none> ()" EOL));
#else
  Serial.print(F(" <action> : <output-pin> (low|high|toggle) <out-task> (arm|start|restart|stop|kick) <none> (none)" EOL));
  Serial.print(F(" <target> : <output-pin> (output pin-index or pin-name) <out-task> (task-index or task-name) <none> ()" EOL));
#endif
  Serial.print(F(" <count>  : [-1 to 1073741820] repeat count: -1 for continuous, 0 for single action" EOL));
  Serial.print(F(" <delay>  : n[s|ms|us] delay: 0 or between 100 us and 17:53" EOL));
  Serial.print(F(" <up>     : n[s|ms|us] up time: 0 or between 100 us and 17:53" EOL));
  Serial.print(F(" <down>   : n[s|ms|us] down time: 0 or between 100 us and 17:53" EOL));
  Serial.print(F(" <options>: (arm-on-finish arm-on-startup interrupts)" EOL));
}

const char * const task_prop_info[] = {"name", "trigger", "source", "action", "target", "count", "delay", "up", "down", "options", NULL};
const char * const task_stat_info[] = {"stopped", "armed", "finished", "started"};

const char * get_stat_name(byte s) {
  if ((s < 0) || (s >= countof(task_stat_info))) return task_stat_info[3];
  return task_stat_info[s];
}

byte get_task_state(byte ti) {
  byte stat;
  if (tasks[ti].counter > 0) stat = 3;
  else stat = (byte)(COUNT2STAT(tasks[ti].counter));
  if (stat > 3) stat = 0;
  return stat;
}

const char * get_src_name(byte ti) {
  const char * p = "";
  if (ti < task_count) {
    struct task & t = tasks[ti];
    if (t.trigger >= TRGUP) {
      if (t.trigger <= TRGLOW) p = get_pin_name(t.srcpin);
      else p = get_task_name(t.srcpin);
    }
  } if (!p) p = "";
  return p;
}

const char * get_dst_name(byte ti) {
  const char * p = "";
  if (ti < task_count) {
    struct task & t = tasks[ti];
    if (t.action <= ACTLASTPIN) p = get_pin_name(t.dstpin);
    else p = get_task_name(t.dstpin);
  } if (!p) p = "";
  return p;
}

void cmd_status_tasks(byte b, byte e)
{
  char text[MAX_KEY_SIZE];
  if (e >= task_count) e = task_count - 1;

  Serial.print(F("index\tname\ttrigger\tsource\taction\ttarget\tcount\tdelay\tup\tdown\toptions" EOL));
  if (!task_count) return;
  for (int ti = b; ti <= e; ++ti)
  {
    struct task & t = tasks[ti];
    Serial.write(" ");
    Serial.print(ti + 1); Serial.print(F("\t"));
    Serial.print(t.name); Serial.print(F("\t"));
    Serial.print(trigger_info[t.trigger]); Serial.print(F("\t"));
    Serial.print(get_src_name(ti)); Serial.print(F("\t"));
    Serial.print(find_index_key(text, action_info, t.action)); Serial.print(F("\t"));
    Serial.print(get_dst_name(ti)); Serial.print(F("\t"));
    long cnt = t.count;
    if (cnt == CNTCONTINUOUS) cnt = -1;
    Serial.print(cnt); Serial.print(F("\t"));
    print_time(t.waittime); Serial.print(F("\t"));
    print_time(t.uptime); Serial.print(F("\t"));
    print_time(t.downtime); Serial.print(F("\t"));
    Serial.print(F("'")); print_options(text, t.options, option_info); Serial.print(F("'" EOL));
  }
}

void print_task_status(byte ti)
{
  struct task & t = tasks[ti];
  byte state = get_task_state(ti);
  Serial.print(F("task[")); Serial.print(ti+1); Serial.print(F("]=")); Serial.print(state);
  //Serial.print(F("task[")); Serial.print(t.name); Serial.print(F("]=")); Serial.print(get_stat_name(state));
  if (t.triggered & TICK_TRIGGERED) Serial.print(F("*"));
  if (t.triggered & START_TRIGGERED) Serial.print(F("+"));
  Serial.print(F(EOL));
}

void cmd_status_task(byte b, byte e)
{
  if (b >= task_count) return;
  if (e >= task_count) e = task_count - 1;
  for (byte ti = b; ti <= e; ++ti)
  {
    struct task & t = tasks[ti];
    byte state = get_task_state(ti);
    Serial.print(F("task[")); Serial.print(ti + 1); Serial.print(F("]=")); Serial.print(state);
    if (t.triggered & TICK_TRIGGERED) Serial.print(F("*"));
    if (t.triggered & START_TRIGGERED) Serial.print(F("+"));
    Serial.print(F(": [0 to 3][*][+] '"));
    Serial.print(t.name);
    Serial.print(F("' (idle|armed|finished|fired) *:triggered, +:start triggered" EOL));
  }
}

void cmd_task_status(byte cmd_index, byte argc, char **argv)
{
  if (!argc)
  {
    cmd_status_task(0, -1);
    return;
  }
  byte ti = parse_index_or_name(argv[0], (void*)&get_task_name);
  if (ti == (byte) - 1)
  {
    Serial.print(F("error: first argument should be task index or name." EOL));
    return;
  }
  if (argc < 2)
  {
    print_task_status(ti);
    return;
  }
  struct task & t = tasks[ti];
  long state_index;
  if (isdigit(argv[1][0])) state_index = atol(argv[1]);
  else state_index = parse_index_or_name(argv[1], (void*)&get_stat_name);
  if (state_index == (byte) - 1)
  {
    Serial.print(F("error: second argument should be task state." EOL));
    return;
  }
  long state = STAT2COUNT((long)state_index);
  switch (state) {
    case CURIDLE:
    case CURFINISHED:
      if (t.counter > CURIDLE)
      {
        tasks[ti].options |= OPTSTOP;
        stop_task(ti);
        tasks[ti].options &= ~OPTSTOP;
        return;
      }
      break;
    case CURARMED:
      if (t.counter == CURIDLE)
      {
        arm_task(ti);
        return;
      }
      break;
    case CURFIRED:
      if ((t.counter == CURIDLE) || (t.counter == CURARMED))
      {
        start_task(ti, micros());
        return;
      }
      break;
  }
  Serial.print(F("error: task status could not be set to ")); Serial.print(task_stat_info[state_index]); Serial.print(F(EOL));
}

void cmd_tasks(byte cmd_index, byte argc, char**argv)
{
  if (!argc || (argv[0][0] == '?'))
  {
    cmd_info_tasks();
    cmd_status_tasks(0, -1);
    return;
  }
  if (argc && (argv[0][0] == '-'))
  {
    if (argv[0][1] == '*') {
      task_count = 0;
      Serial.print(F("all tasks deleted." EOL));
    }
    else
    {
      if (task_count)
      {
        Serial.print(F("task ")); Serial.print(task_count); Serial.print(F(" deleted." EOL));
        --task_count;
      }
    }
    return;
  }
  // argument 0 is task index or name
  byte ti = parse_index_or_name(argv[0], (void*)&get_task_name);
  if (ti == (byte) - 1)
  {
    ti = parse_ulong(argv[0]);
    if (ti != (byte) - 1) --ti;
    if ((ti == (byte) - 1) || (argc < 2))
    {
      Serial.print(F("task argument error: first argument should be task index or name. type 'task' to see defined tasks." EOL));
      return;
    }
    if (ti != task_count)
    {
      Serial.print(F("task argument error: please define task ")); Serial.print(pin_count + 1); Serial.print(F(" first." EOL));
      return;
    }
    if (ti >= TASKCOUNT)
    {
      Serial.print(F("error: no more new tasks can be created." EOL));
      return;
    }
  }
  if (argc < 2)
  {
    cmd_status_tasks(ti, ti);
    return;
  }
  struct task & t = tasks[ti];
  unsigned long ul;
  byte val;
  long l;
  byte prop_mode = 0; // set in <prop> = <value> syntax
  byte propi = 1; // property index: increments when prop_mode == 0
  byte argi = 1; // argument index: increments when prop_mode == 0
  // argument 1 is task name or the property name
  if (argc > argi)
  {
    val = parse_enum(argv[argi], task_prop_info);
    if (val != (byte) - 1) {
      prop_mode = 1;
      propi = val + 1;
      argi = 2;
    }
  }
  if ((argc > argi) && (propi == 1))
  {
    strncpy(t.name, argv[argi], sizeof(t.name) - 1);
    t.name[sizeof(t.name) - 1] = 0;
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // argument 2 is the trigger
  if ((argc > argi) && (propi == 2))
  {
    val = parse_enum(argv[argi], trigger_info);
    if (val != (byte) - 1) t.trigger = val;
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // argument 3 is the srcpin
  if ((argc > argi) && (propi == 3))
  {
    if (t.trigger >= TRGUP)
    {
      if (t.trigger <= TRGLOW)
      {
        val = parse_index_or_name(argv[argi], (void*)&get_pin_name);
      }
      else
      {
        val = parse_index_or_name(argv[argi], (void*)&get_task_name);
      }
    }
    if (val != (byte) - 1) t.srcpin = val;
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // argument 4 is the action
  if ((argc > argi) && (propi == 4))
  {
    val = find_key_index(action_info, argv[argi]);
    if (val != (byte) - 1) t.action = val;
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // argument 5 is the dstpin
  if ((argc > argi) && (propi == 5))
  {
    if (t.action <= ACTLASTPIN)
    {
      val = parse_index_or_name(argv[argi], (void*)&get_pin_name);
#if defined(BUTTON_PIN)
      if ((val != MAX_BYTE) && (pins[val].pin == BUTTON_PIN) && (pins[val].mode != MODADC)) val = t.dstpin; // don't allow changing the button pin !
#endif
    }
    else
    {
      val = parse_index_or_name(argv[argi], (void*)&get_task_name);
    }
    if (val != MAX_BYTE) t.dstpin = val;
    else if (t.action == ACTSTOTASK) t.dstpin = NOPIN; // stop all tasks
#if defined(USEADC)
    if((t.action == ACTSTAADC) && (pins[t.dstpin].mode != MODADC)) {
      for(byte p = 0; p < pin_count; ++p) if(pins[p].mode == MODADC) { t.dstpin = p; break;}
    }
#endif
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // argument 6 is count
  if ((argc > argi) && (propi == 6))
  {
    l = parse_long(argv[argi]); if (l < 0) {
      l = CNTCONTINUOUS;
    } else if (l > (CNTCONTINUOUS - 1)) {
      l = (CNTCONTINUOUS - 1);
    } t.count = l;
    if (t.count)
    {
      if (t.downtime < MIN_PERIOD) t.downtime = MIN_PERIOD;
      if (t.uptime < MIN_PERIOD) t.uptime = MIN_PERIOD;
    }
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // argument 7 is delay
  if ((argc > argi) && (propi == 7))
  {
    ul = parse_time(argv[argi]); if (ul != -1) {
      t.waittime = ul;
    }
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // argument 8 is uptime
  if ((argc > argi) && (propi == 8))
  {
    ul = parse_time(argv[argi]); if (ul != -1) {
      if(t.count) {
        if(ul < MIN_PERIOD) ul = MIN_PERIOD;
      }
      t.uptime = ul;
    }
    if (t.count && (t.uptime < MIN_PERIOD)) t.uptime = MIN_PERIOD;
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // argument 9 is downtime
  if ((argc > argi) && (propi == 9))
  {
    ul = parse_time(argv[argi]); if (ul != -1) {
      if(t.count) {
        if(ul < MIN_PERIOD) ul = MIN_PERIOD;
      }
      t.downtime = ul;
    }
    if (t.count && (t.downtime < MIN_PERIOD)) t.downtime = MIN_PERIOD;
  }
  if (!prop_mode) {
    ++argi;
    ++propi;
  }
  // argument 10 are the options
  if ((argc > argi) && (propi == 10))
  {
    val = parse_options(argv[argi], option_info); if (val != (byte) - 1) t.options = val;
  }

  if (ti == task_count) task_count = ti + 1;
  cmd_status_tasks(ti, ti);
  init_tasks(ti, ti);
}

const char * get_task_name(byte i)
{
  if (i == (byte) - 1) return "*";
  if (i >= task_count) return NULL;
  return tasks[i].name;
}

inline void arm_task(byte ti)
{
  struct task & t = tasks[ti];
  t.counter = CURARMED;
  t.triggered = (t.options & OPTINTERRUPT) ? TICK_TRIGGERED : NOT_TRIGGERED;
  t.changed = true;
  if (((t.trigger == TRGHIGH) && pins[t.srcpin].state) ||
      ((t.trigger == TRGLOW)  && !pins[t.srcpin].state) ||
      ((t.trigger == TRGNO)))
  {
    start_task(ti, micros());
  }
  else if (task_enable_interrupt(ti))
  {
    t.triggered = START_TICK_TRIGGERED;
  }
}

void cmd_arm(byte cmd_index, byte argc, char**argv)
{
  byte ti = NOTASK;
  if (argc >= 1) ti = parse_index_or_name(argv[0], (void*)&get_task_name);
  if (ti == NOTASK)
  {
    Serial.print(F("arm argument error: first argument should be task index or name. type 'task' to see defined tasks." EOL));
    return;
  }
  struct task & t = tasks[ti];
  if (t.counter != CURIDLE)
  {
    Serial.print(F("arm error: task ")); Serial.print(ti + 1); Serial.print(F(" is not idle." EOL));
    return;
  }
  arm_task(ti);
#if defined(DEBUG)
  if (!verbose)
#endif
  {
    Serial.print(F("task ")); Serial.print(t.name); Serial.print(F(" armed." EOL));
  }
}

#if defined(ARDUINO_ARCH_RENESAS_UNO) || defined(ARDUINO_PORTENTA_C33)

bool interrupts_are_disabled() {
  return __get_PRIMASK();
}

struct disable_interrupts
{
  uint32_t interrupt_mask;

  disable_interrupts()
  {
    interrupt_mask = __get_PRIMASK();
    noInterrupts();
  }

  ~disable_interrupts()
  {
    __set_PRIMASK(interrupt_mask);
  }

  bool interrupts_were_disabled()
  {
    return interrupt_mask;
  }
};

#else //AVR

bool interrupts_are_disabled() {
  return !(SREG & 0x80);
}

struct disable_interrupts
{
  uint8_t status;
  disable_interrupts()
  {
    status = SREG;
    cli();
  }
  ~disable_interrupts()
  {
    SREG = status;
  }

  bool interrupts_were_disabled() {
    return !(status & 0x80);
  }
};
#endif

inline void task_disable_interrupt(byte ti)
{
  struct task & t = tasks[ti];
  t.triggered = NOT_TRIGGERED;
  if ((t.trigger != TRGUP) && (t.trigger != TRGDOWN) && (t.trigger != TRGANY)) return;
  // search another task that is triggered by this pin
  byte i;
  for (i = 0; i < task_count; ++i)
  {
    if ((tasks[i].triggered == START_TICK_TRIGGERED) && (i != ti) && (t.srcpin == tasks[i].srcpin))
    {
      break;
    }
  }
  if (i == task_count)
  {
    // no other task is triggered by this pin
    detachInterrupt(digitalPinToInterrupt(pins[t.srcpin].pin));
  }
}

void cmd_disarm(byte cmd_index, byte argc, char**argv)
{
  byte ti = NOTASK;
  if (argc >= 1) ti = parse_index_or_name(argv[0], (void*)&get_task_name);
  if (ti == NOTASK)
  {
    Serial.print(F("disarm argument error: first argument should be task index or name. type 'task' to see defined tasks." EOL));
    return;
  }
  struct task & t = tasks[ti];
  if (t.counter != CURARMED)
  {
    Serial.print(F("disarm error: task ")); Serial.print(ti + 1); Serial.print(F(" is not armed." EOL));
    return;
  }
  disarm_task(ti);
}

void disarm_task(byte ti)
{
  struct task & t = tasks[ti];
  if (t.triggered == START_TICK_TRIGGERED)
  {
    task_disable_interrupt(ti);
  }
  {
    disable_interrupts di;
    t.counter = CURIDLE;
  }
  t.changed = true;
#if defined(DEBUG)
  if (verbose)
  {
    print_task_status(ti);
  }
#endif
}

void cmd_fire(byte cmd_index, byte argc, char**argv)
{
  byte ti = NOTASK;
  if (argc >= 1) ti = parse_index_or_name(argv[0], (void*)&get_task_name);
  if (ti == NOTASK)
  {
    Serial.print(F("fire argument error: first argument should be task index or name. type 't' to see defined tasks." EOL));
    return;
  }
  struct task & t = tasks[ti];
  if ((t.counter != CURARMED) && (t.counter != CURIDLE))
  {
    Serial.print(F("fire error: task ")); Serial.print(ti + 1); Serial.print(F(" is not idle or armed." EOL));
    return;
  }
#if defined(DEBUG)
  if(verbose >= 3) { Serial.print(__LINE__); Serial.print(" "); Serial.print(ti); Serial.print(EOL); }
#endif
  start_task(ti, micros());
#if defined(DEBUG)
  if (!verbose)
#endif
  { Serial.print(F("task ")); Serial.print(t.name); Serial.print(F(" started." EOL)); }
}

void cmd_halt(byte cmd_index, byte argc, char**argv)
{
  byte h = NOTASK;
  if (argc >= 1) h = parse_byte(argv[0]);
  if (h == NOTASK)
  {
    Serial.print("halt="); Serial.print(halt); Serial.print(F(EOL));
    return;
  }
  halt = h;
  if (h)
  {
    stop_tasks();
  }
  else
  {
    init_tasks(0, -1);
  }
}

void cmd_stop(byte cmd_index, byte argc, char**argv)
{
  byte ti = NOTASK;
  if (argc >= 1)
  {
    ti = parse_index_or_name(argv[0], (void*)&get_task_name);
  }
  if (ti == NOTASK)
  {
    stop_tasks();
#if defined(DEBUG)
    if (!verbose)
#endif
    { Serial.print(F("all tasks stopped." EOL));}
    return;
  }
  stop_task(ti);
#if defined(DEBUG)
  if (!verbose)
#endif
  { Serial.print(F("task ")); Serial.print(tasks[ti].name); Serial.print(F(" stopped." EOL)); }
}

inline void stop_tasks()
{
  disable_interrupts di;

  for (int ti = 0; ti < task_count; ++ti) tasks[ti].options |= OPTSTOP;

  // execute the stop sequence for all tasks
  for (int ti = 0; ti < task_count; ++ti)
  {
    struct task & t = tasks[ti];
    t.counter = 1;
    t.nexttick = micros() - tasks[ti].starttick;
    tick_task(ti);
  }

  for (int ti = 0; ti < task_count; ++ti) tasks[ti].options |= OPTSTOP;
  check_finished_tasks();
  for (int ti = 0; ti < task_count; ++ti) disarm_task(ti);

  // clear the OPTSTOP option
  for (int ti = 0; ti < task_count; ++ti) tasks[ti].options &= ~OPTSTOP;
}

inline void stop_task(byte ti)
{
  struct task & t = tasks[ti];
  {
    disable_interrupts di;
    if (t.counter <= CURIDLE)
    {
#if defined(DEBUG)
      if (!di.interrupts_were_disabled() && verbose)
        {Serial.print(F("error: task ")); Serial.print(ti + 1); Serial.print(F(" is not active." EOL)); }
#endif      
      return;
    }
    t.options |= OPTSTOP; // to prevent auto-arm and auto-trigger tasks to be unstoppeble, set the OPTSTOP option ..
    t.counter = 1;
    t.nexttick = micros() - tasks[ti].starttick;
    // execute stop sequence here before next serial command is executed
    tick_task(ti);
    t.options &= ~OPTSTOP;
  }
#if defined(DEBUG)
  if (verbose) print_task_status(ti);
#endif
  for (int ti2 = 0; ti2 < task_count; ++ti2)
  {
    if (ti2 == ti) continue;
    struct task & t2 = tasks[ti2];
    if ((t2.counter == CURARMED) &&
        (t2.trigger == TRGSTOP) &&
        (t2.srcpin == ti))
    {
#if defined(DEBUG)
      if(verbose >= 3) { Serial.print(__LINE__); Serial.print(" "); Serial.print(ti2); Serial.print(EOL); }
#endif
      start_task(ti2, micros());
    }
  }
}

void cmd_report(byte cmd_index, byte argc, char**argv)
{
  report_changes();
  Serial.print(F("." EOL));
}

byte next_timer_task = NOTASK;

void set_next_timer()
{
  unsigned long period;             // shortest period to next task tick
  unsigned long task_tick; // time of next tick
  unsigned long now;       // compared to now
  long task_period;        // period till next tick of this task

  struct disable_interrupts di;
  next_timer_task = NOTASK;
  period = MAX_ULONG;
  now = micros();
  byte ti;
  for (ti = 0; ti < task_count; ++ti)
  {
    struct task & t = tasks[ti];
    if ((t.triggered == NOT_TRIGGERED) || (t.counter < CURFIRED)) continue;
    task_tick = t.starttick + t.nexttick;
    task_period = task_tick - now;
    // tick the task if it is pending
    while (task_period < (MIN_PERIOD/2))
    {
      tick_task(ti);
      task_tick = t.starttick + t.nexttick;
      task_period = task_tick - now;
      if (t.counter < CURFIRED) break;
    }
    if (t.counter < CURFIRED) continue;
    if (task_period < period)
    {
      next_timer_task = ti;
      period = task_period;
    }
  }
  if(next_timer_task != NOTASK)
  {
    if(period > AVR_TIMER1_LIMIT)
    {
      next_timer_task = NOTASK;
      period = AVR_TIMER1_LIMIT;
    }
    Timer1.setPeriod(period);
  }
}

#if defined(ARDUINO_ARCH_RENESAS_UNO) || defined(ARDUINO_PORTENTA_C33)
void timer_interrupt_callback(timer_callback_args_t *) {
  disable_interrupts di;
#else
void timer_interrupt_callback() {
#endif
  Timer1.stop();
  if (next_timer_task != NOTASK) tick_task(next_timer_task);
  set_next_timer();
}

inline void start_task(byte ti, unsigned long tick)
{
#if defined(DEBUG)
  if (verbose >= 3) print_free_memory(__LINE__);
  if (verbose >= 3) {Serial.print("start_task "); Serial.print(ti);Serial.print(EOL);}
#endif
  struct task & t = tasks[ti];
  if (t.options & OPTSTOP) return;
  {
    disable_interrupts di;
    t.starttick = tick;
    t.nexttick = t.waittime;
    t.counter = t.count * 2 + 1;
    t.changed = true;
    if (!t.waittime) tick_task(ti);
    if (!di.interrupts_were_disabled())
    {
      set_next_timer();
    }
  }
  for (int ti2 = 0; ti2 < task_count; ++ti2)
  {
    if (ti2 == ti) continue;
    struct task & t2 = tasks[ti2];
    if ((t2.counter == CURARMED) &&
        (t2.trigger == TRGSTART) &&
        (t2.srcpin == ti))
    {
#if defined(DEBUG)
      if(verbose >= 3) { Serial.print(__LINE__); Serial.print(" "); Serial.print(ti2); Serial.print(EOL); }
#endif
      start_task(ti2, tick);
    }
  }
}

////////////////////////////
// interrupts
///////////////////////////

struct s_isr_task_mode
{
  byte task;
  byte mode;
};

struct s_isr_task_mode isr_task_mode[ISRCOUNT] = {{0}};

inline void handle_pin_interrupt(byte N)
{
  unsigned long tick = micros();
  byte ti = isr_task_mode[N].task;
  byte mode = isr_task_mode[N].mode;
  byte pin = tasks[ti].srcpin;
  struct pin & p = pins[pin];
  // reading the state from the pin will fail to detect very short pulses
  //p.state = digitalRead(p.pin);
  // instead rely on the internal book keeping of the state ...
  if(mode == TRGUP) p.state = 1;
  else if(mode == TRGDOWN) p.state = 0;
  else p.state = !p.state;
  p.tick = tick;
  p.changed = true;
  start_task(ti,tick);
}

byte find_isr(byte pin)
{
  for (int i = 0; i < ISRCOUNT; ++i)
  {
    if (isr_pins[i] == pin)
    {
      return i;
    }
  }
  return NOISR;
}

void update_pin_supports_intr(struct pin & p)
{
#if ISRCOUNT == 0
  p.intr_index = MAX_BYTE;
#else
  byte intr = digitalPinToInterrupt(p.pin);
  if(
#if defined(USEADC)
    (p.mode == MODADC) || 
#endif
    (mode_values[p.mode] == OUTPUT) || (intr == MAX_BYTE))
  {
    p.intr_index = MAX_BYTE;
    return;
  }
  p.intr_index = find_isr(p.pin);
#endif
}

void test_interrupts()
{
  {
    disable_interrupts di1;
    disable_interrupts di2;
    if (di1.interrupts_were_disabled())
    {
      Serial.print(F("interupt test failed: SREG interrupt flag is off." EOL));
    }
    if (!di2.interrupts_were_disabled())
    {
      Serial.print(F("interupt test failed: SREG interrupt flag is on." EOL));
    }
  }
  if (interrupts_are_disabled())
  {
    Serial.print(F("interupt test failed: SREG interrupt flag is off." EOL));
  }
}

#if defined(ARDUINO_FSP)
#define disable_interrupts_during_isr();  disable_interrupts di;
#else
#define disable_interrupts_during_isr();
#endif

#define PINISR(N) void isr##N(void) { \
  disable_interrupts_during_isr();\
  detachInterrupt(digitalPinToInterrupt(isr_pins[N])); \
  handle_pin_interrupt(N); \
  set_next_timer(); \
}
#if ISRCOUNT > 0
PINISR(0)
#endif
#if ISRCOUNT > 1
PINISR(1)
#endif
#if ISRCOUNT > 2
PINISR(2)
#endif
#if ISRCOUNT > 3
PINISR(3)
#endif
#if ISRCOUNT > 4
PINISR(4)
#endif
#if ISRCOUNT > 5
PINISR(5)
#endif
#if ISRCOUNT > 6
PINISR(6)
#endif
#if ISRCOUNT > 7
PINISR(7)
#endif
#if ISRCOUNT > 8
PINISR(8)
#endif
#if ISRCOUNT > 9
PINISR(9)
#endif
#if ISRCOUNT > 10
PINISR(10)
#endif
#if ISRCOUNT > 11
PINISR(11)
#endif
#if ISRCOUNT > 12
PINISR(12)
#endif
#if ISRCOUNT > 13
PINISR(13)
#endif
#if ISRCOUNT > 14
PINISR(14)
#endif
#if ISRCOUNT > 15
PINISR(15)
#endif
#if ISRCOUNT > 16
PINISR(16)
#endif
#if ISRCOUNT > 17
PINISR(17)
#endif
#if ISRCOUNT > 18
PINISR(18)
#endif
#if ISRCOUNT > 19
PINISR(19)
#endif
#if ISRCOUNT > 20
PINISR(20)
#endif
#if ISRCOUNT > 21
PINISR(21)
#endif

//void pin_callback() {}

inline void set_task_isr(byte ti)
{
#if ISRCOUNT == 0
  return;
#else
  struct task & t = tasks[ti];
  byte pi = t.srcpin;
  struct pin & p = pins[pi];
  byte intr = digitalPinToInterrupt(p.pin);
  if (intr == MAX_BYTE) return;
  byte intr_index = p.intr_index;
  isr_task_mode[intr_index].task = ti;
  isr_task_mode[intr_index].mode = t.trigger;
  PinStatus mode = (t.trigger == TRGUP) ? RISING : ((t.trigger == TRGDOWN) ? FALLING : CHANGE);
  switch (intr_index)
  {
    case  0: 
#if defined(ARDUINO_AVR_UNO)
       EIFR = (1 << INTF0);
#endif
#if defined(ARDUINO_AVR_MEGA2560) || defined(ARDUINO_AVR_ADK)
       EIFR = (1 << 4); // mega pin 2 ~ chip pin 6 ~ INT4
#endif
      attachInterrupt(intr,isr0,mode);
    break;
#if ISRCOUNT > 1
    case  1: 
#if defined(ARDUINO_AVR_UNO)
       EIFR = (1 << INTF1);
#endif      
#if defined(ARDUINO_AVR_MEGA2560) || defined(ARDUINO_AVR_ADK)
       EIFR = (1 << 5); // mega pin 3 ~ chip pin 7 ~ INT5
#endif
      attachInterrupt(intr, isr1, mode); 
    break;
#endif
#if ISRCOUNT > 2
    case  2: 
#if defined(ARDUINO_AVR_MEGA2560) || defined(ARDUINO_AVR_ADK)
       EIFR = (1 << 3); // mega pin 18 ~ chip pin 46 ~ INT3
#endif
    attachInterrupt(intr, isr2, mode); break;
#endif
#if ISRCOUNT > 3
    case  3: 
#if defined(ARDUINO_AVR_MEGA2560) || defined(ARDUINO_AVR_ADK)
       EIFR = (1 << 2); // mega pin 19 ~ chip pin 45 ~ INT2
#endif
    attachInterrupt(intr, isr3, mode); break;
#endif
#if ISRCOUNT > 4
    case  4: 
#if defined(ARDUINO_AVR_MEGA2560) || defined(ARDUINO_AVR_ADK)
       EIFR = (1 << 1); // mega pin 20 ~ chip pin 44 ~ INT1
#endif
    attachInterrupt(intr, isr4, mode); break;
#endif
#if ISRCOUNT > 5
    case  5: 
#if defined(ARDUINO_AVR_MEGA2560) || defined(ARDUINO_AVR_ADK)
       EIFR = (1 << 0); // mega pin 21 ~ chip pin 43 ~ INT0
#endif
    attachInterrupt(intr, isr5, mode); break;
#endif
#if ISRCOUNT > 6
    case  6: attachInterrupt(intr, isr6, mode); break;
#endif
#if ISRCOUNT > 7
    case  7: attachInterrupt(intr, isr7, mode); break;
#endif
#if ISRCOUNT > 8
    case  8: attachInterrupt(intr, isr8, mode); break;
#endif
#if ISRCOUNT > 9
    case  9: attachInterrupt(intr, isr9, mode); break;
#endif
#if ISRCOUNT > 10
    case 10: attachInterrupt(intr, isr10, mode); break;
#endif
#if ISRCOUNT > 11
    case 11: attachInterrupt(intr, isr11, mode); break;
#endif
#if ISRCOUNT > 12
    case 12: attachInterrupt(intr, isr12, mode); break;
#endif
#if ISRCOUNT > 13
    case 13: attachInterrupt(intr, isr13, mode); break;
#endif
#if ISRCOUNT > 14
    case 14: attachInterrupt(intr, isr14, mode); break;
#endif
#if ISRCOUNT > 15
    case 15: attachInterrupt(intr, isr15, mode); break;
#endif
#if ISRCOUNT > 16
    case 16: attachInterrupt(intr, isr16, mode); break;
#endif
#if ISRCOUNT > 17
    case 17: attachInterrupt(intr, isr17, mode); break;
#endif
#if ISRCOUNT > 18
    case 18: attachInterrupt(intr, isr18, mode); break;
#endif
#if ISRCOUNT > 19
    case 19: attachInterrupt(intr, isr19, mode); break;
#endif
#if ISRCOUNT > 20
    case 20: attachInterrupt(intr, isr20, mode); break;
#endif
#if ISRCOUNT > 21
    case 21: attachInterrupt(intr, isr21, mode); break;
#endif
  }
#endif
}

inline bool task_enable_interrupt(byte ti)
{
  struct task & t = tasks[ti];
  if (!(t.options & OPTINTERRUPT)) return false;
  if ((t.trigger == TRGUP) || (t.trigger == TRGDOWN) || (t.trigger == TRGANY))
  {
    struct pin & p = pins[t.srcpin];
    if (p.intr_index != MAX_BYTE)
    {
      set_task_isr(ti);
      return true;
    }
    return false;
  }
  if ((t.trigger != TRGSTART) && (t.trigger != TRGSTOP)) return false;
  struct task & tt = tasks[t.srcpin];
  if (!(tt.options & OPTINTERRUPT)) return false;
  return true;
}

inline void check_input_pin(byte pi)
{
  struct pin & p = pins[pi];
  byte level = digitalRead(p.pin);
  if (level != p.state)
  {
    p.state = level;
    p.tick = micros();
    p.changed = true;
  // start tasks
    for (int ti = 0; ti < task_count; ++ti)
    {
      struct task & t = tasks[ti];
      if (t.trigger <= TRGSOFTWARE) continue;
      if (t.srcpin != pi) continue;
      if (t.counter != CURARMED) continue;
      //if (t.triggered == START_TICK_TRIGGERED) continue;
      //if (t.triggered & START_TICK_TRIGGERED) continue;
      switch (t.trigger)
      {
        case TRGUP:
        case TRGHIGH:
          if (!level) continue;
          break;
        case TRGDOWN:
        case TRGLOW:
          if (level) continue;
          break;
        case TRGANY:
          break;
      }
      start_task(ti, p.tick);
    }
  }
}

inline void check_input_pins()
{
  // check all pins
  for (int pi = 0; pi < pin_count; ++pi)
  {
    struct pin & p = pins[pi];
    if ((mode_values[p.mode] == OUTPUT) 
#if defined (USEADC)
      || (p.mode == MODADC)
#endif
      ) continue;
    // todo: check if there are only trigger started tasks ?
    check_input_pin(pi);
  }
}

inline void check_finished_tasks()
{
  if (halt) return;
  for (byte ti = 0; ti < task_count; ++ti)
  {
    struct task & t = tasks[ti];
    if (t.counter != CURFINISHED) continue;

    // task has ended.. check on auto restart without pin level change
    if (t.options & OPTAUTOARM)
    {
      arm_task(ti);
    }
    else
    {
      if (t.triggered == START_TICK_TRIGGERED)
      {
        task_disable_interrupt(ti);
      }
      t.counter = CURIDLE;
    }
    // check if any other task should be triggered ...
    for (int ti2 = 0; ti2 < task_count; ++ti2)
    {
      if (ti2 == ti) continue;
      struct task & t2 = tasks[ti2];
      if ((t2.counter == CURARMED) &&
          (t2.trigger == TRGSTOP) &&
          (t2.srcpin == ti))
      {
        start_task(ti2, t.starttick + t.nexttick);
      }
    }
  }
}

inline void tick_tasks()
{
  for (int ti = 0; ti < task_count; ++ti)
  {
    struct task & t = tasks[ti];
    if (t.triggered != NOT_TRIGGERED) continue;
    unsigned long ticks = micros() - t.starttick;
    if (ticks < t.nexttick) continue;
    tick_task(ti);
  }
}

inline void set_pin_mode(struct pin & p, byte mode)
{
  p.mode = mode;
  byte pin = p.pin;
  if(p.mode == MODADC) pin += A0;
  pinMode(pin, mode_values[mode]);
  if (mode_values[mode] == OUTPUT)
  {
    if (mode == MODOUT)
    {
      digitalWrite(p.pin, p.startup_state);
    }
    else
    {
      analogWrite(p.pin, p.startup_state);
    }
    p.state = p.startup_state;
  }
  else
  {
#if defined(USEADC)
    if (mode == MODADC)
    {
      p.state = analogRead(p.pin + A0);
    }
    else
#endif
    {
      p.state = digitalRead(p.pin);
    }
  }
  update_pin_supports_intr(p);
  p.tick = micros();
  p.changed = true;
}

inline void set_pin_state(struct pin & p, byte val)
{
  switch (p.mode)
  {
    case MODOUT:
      digitalWrite(p.pin, val);
      break;
    case MODPWM:
      val = val ? p.toggle_state : p.startup_state;
      analogWrite(p.pin, val);
      break;
  }
  p.tick = micros();
  p.state = val;
  p.changed = true;
}

inline void toggle_pin_state(struct pin & p)
{
  byte val;
  switch (p.mode)
  {
    case MODOUT:
      val = !p.state;
      digitalWrite(p.pin, val);
      break;
    case MODPWM:
      val = (p.state == p.startup_state) ? p.toggle_state : p.startup_state;
      analogWrite(p.pin, val);
      break;
  }
  p.tick = micros();
  p.state = val;
  p.changed = true;
}

#if defined(USEADC)

byte adc_pin = MAX_BYTE;

#if defined(ARDUINO_ARCH_AVR)

inline void start_adc(struct pin & p)
{
  adc_pin = &p - pins;
  byte pin = p.pin;

 // from analogRead()
#if defined(analogPinToChannel)
#if defined(__AVR_ATmega32U4__)
	if (pin >= 18) pin -= 18; // allow for channel or pin numbers
#endif
	pin = analogPinToChannel(pin);
#elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
	if (pin >= 54) pin -= 54; // allow for channel or pin numbers
#elif defined(__AVR_ATmega32U4__)
	if (pin >= 18) pin -= 18; // allow for channel or pin numbers
#elif defined(__AVR_ATmega1284__) || defined(__AVR_ATmega1284P__) || defined(__AVR_ATmega644__) || defined(__AVR_ATmega644A__) || defined(__AVR_ATmega644P__) || defined(__AVR_ATmega644PA__)
	if (pin >= 24) pin -= 24; // allow for channel or pin numbers
#else
	if (pin >= 14) pin -= 14; // allow for channel or pin numbers
#endif

#if defined(DEBUG)
  if(verbose >= 3) { Serial.print(__LINE__); Serial.print(" start adc"); Serial.print(pin); Serial.print(EOL); }
#endif

#if defined(ADCSRA) && defined(ADCSRB) && defined(ADMUX) &&defined(ADC)

	// set the analog reference (high two bits of ADMUX) and select the
	// channel (low 4 bits).  this also sets ADLAR (left-adjust result)
	// to 0 (the default).
	ADMUX = bit(REFS0) | (pin & 0x07); // REFSO: AVCC with external capacitor at the AREF pin is used as VRef

#if defined(MUX5)
	// the MUX5 bit of ADCSRB selects whether we're reading from channels
	// 0 to 7 (MUX5 low) or 8 to 15 (MUX5 high).
	//ADCSRB = (ADCSRB & ~(1 << MUX5)) | (((pin >> 3) & 0x01) << MUX5);
	ADCSRB = (((pin >> 3) & 0x01) << MUX5) | 0; // free running mode
#endif

	//sbi(ADCSRA, ADSC);
  ADCSRA =  bit(ADEN)  // Turn ADC on
          | bit(ADSC)  // start single conversion
          | bit(ADIE)  // Enable interrupt
          | bit(ADPS0) | bit(ADPS1) | bit(ADPS2); // Prescaler of 128

 /* 
  ADCSRA =   bit(ADEN)  // Turn ADC on
           | bit(ADATE) // ADC Auto Trigger Enable
           | bit(ADIE)  // Enable interrupt
           | bit(ADPS0) | bit(ADPS1) | bit(ADPS2); // Prescaler of 128
*/
#endif
}

ISR(ADC_vect) {
  if(adc_pin >= pin_count) return;
  struct pin & p = pins[adc_pin];
  
  // Must read low first
  p.state = ADCL | (ADCH << 8);
  p.tick = micros();
  p.changed = true;
   
#if defined(DEBUG)
  if(verbose >= 3) { Serial.print(__LINE__); Serial.print(" isr adc"); Serial.print(p.state); Serial.print(EOL); }
#endif
  // if free-running mode is not enabled,
  // set ADSC in ADCSRA (0x7A) to start another ADC conversion
  // ADCSRA |= B01000000;
}

#endif

#if defined(ARDUINO_ARCH_RENESAS_UNO)

// with the Arduino UNO R4 Boards pacakge 1.0.5, you have to remove the 'static' keyword from the 
// definition of the variable analog_values_by_channels on line 14 of 
// C:\Users\Kees\AppData\Local\Arduino15\packages\arduino\hardware\renesas_uno\1.0.5\cores\arduino\analog.cpp
// /*static*/ uint16_t analog_values_by_channels[MAX_ADC_CHANNELS] = {0};
/*
extern uint16_t analog_values_by_channels[29];

void adc_callback(uint8_t unit)
{
  if(adc_pin >= pin_count) return;
  struct pin & p = pins[adc_pin];

  uint16_t cfg;
  p.state = analog_values_by_channels[0];
  p.tick = micros();
  p.changed = true;
   
#if defined(DEBUG)
  if(verbose >= 3) { Serial.print(__LINE__); Serial.print(" isr adc "); Serial.print(p.state); Serial.print(EOL); }
#endif
}
*/

inline void start_adc(struct pin & p)
{
/*
  adc_pin = &p - pins;
  byte pin = p.pin;
  analogAddPinToGroup(pin, ADC_SCAN_GROUP_A);
  attachScanEndIrq(adc_callback, ADC_MODE_SINGLE_SCAN, 12);
*/
  for(byte pi = 0; pi < pin_count; ++pi)
  {
      struct pin & p = pins[pi];
      if(p.mode == MODADC)
      {
        uint16_t state = analogRead(p.pin + A0);
        if(state != p.state)
        {
          p.state = state;
          p.tick = micros();
          p.changed = true;
        }
      }
  }
}

#endif

#endif // USEADC

inline void tick_task(byte ti)
{
  struct task & t = tasks[ti];
  if (t.counter <= CURFINISHED) return; // not fired
  byte val = t.counter & 1; // counting starts at 2*N+1, so this is 1 for the first tick
   --t.counter;
  if (t.counter == CURFINISHED)
  {
    if (t.count == CNTCONTINUOUS)
    {
      if (!(t.options & OPTSTOP))
      {
        t.counter = CNTCONTINUOUS;
      }
    }
  }
  if (t.counter == CURFINISHED)
  {
    t.changed = true;
    // count == 0 means 'set': this action must be done anyway
    if (t.count)
    {
      // but for count <> 0, the last tick is really the end of the
      // last down phase: instead of starting the next up phase, set it low again
      // for when the task was stopped by setting the counter to 1
      val = !val;
    }
  }
  if (t.action <= ACTLASTPIN)
  {
    if (t.dstpin < pin_count)
    {
      struct pin & p = pins[t.dstpin];
      switch (t.action)
      {
        case ACTPOSPULS: set_pin_state(p, val); break;
        case ACTNEGPULS: set_pin_state(p, !val); break;
        case ACTTOGLPIN: if (val) toggle_pin_state(p); break;
#if defined(USEADC)
        case ACTSTAADC: start_adc(p);
#endif
      }
    }
  }
  else
  {
    // act on task
    if (val && !(t.options & OPTSTOP))
    {
      if (t.dstpin < task_count)
      {
        struct task & t2 = tasks[t.dstpin];
        switch (t.action)
        {
          case ACTARMTASK: arm_task(t.dstpin); break;
          //case ACTDISTASK: disarm_task(t.dstpin); break;
          case ACTSTATASK:
            if(t.counter <= CURFINISHED)
            {
              start_task(t.dstpin, t.starttick + t.nexttick); 
            }
            break;
          case ACTRESTASK: 
            start_task(t.dstpin, t.starttick + t.nexttick); 
            break;
          case ACTSTOTASK: stop_task(t.dstpin); break;
          case ACTKICTASK: if (t2.counter == CURIDLE) break; if (t2.counter <= CURFINISHED) {
              start_task(t.dstpin, t.starttick + t.nexttick);
            } else {
              stop_task(t.dstpin);
            } break;
        }
        t2.starttick = t.starttick + t.nexttick;
      }
      else if (t.dstpin == (byte) - 1)
      {
        if (t.action == ACTSTOTASK)
        {
          stop_tasks();
        }
      }
    }
  }
  if (t.counter > CURFINISHED)
  {
    t.nexttick += !(t.counter & 1) ? t.uptime : t.downtime;
    if (t.nexttick > HMAX_LONG) {
      t.nexttick -= HMAX_LONG;
      t.starttick += HMAX_LONG;
    }
  }
}

typedef void (*t_cmd_func) (byte cmd_index, byte argc, char**argv);
typedef struct s_cmd_text {
  const char     name[8];
  const char     description[32];
} s_cmd_text;

typedef struct s_cmd_info {
  t_cmd_func      func;
  byte            flags;
} s_cmd_info;

typedef struct s_cmd_var {
  unsigned long * address;
  unsigned long   min_value;
  unsigned long   max_value;
  byte            cmd_index;
} s_cmd_var;

const s_cmd_text cmd_text_table[] PROGMEM =
{
  // name       description
  {"",          "Commands"                      },
  {"?",         "[gui] Show status and commands"},
  {"pin",       "Set and Query Pin Status"      },
  {"task",      "Set and Query Task Status"     },
  {"arm",       "<task>: Arm Task"              },
  {"disarm",    "<task>: Disarm Tasks"          },
  {"start",     "<task>: Start Task"            },
  {"stop",      "[<task> | !]: Stop Task"       },
  {".",         "Report state changes"          },
  {"",          "Config"                        },
  {"dpin",      "Define Pins []"                },
  {"dtask",     "Define Tasks []"               },
  {"write",     "Save Tasks"                    },
  {"scope",     "Oscilloscope Mode"             },
  {"echo",      "Echo (off,on)"                 },
  {"reset",     "Factory Reset"                 },
  {"halt",      "Halt all Tasks (0,1)"          }
#if defined(DEBUG)
  ,{"verbose",   "Verbose (silent,verbose,all)"  }
#endif
};

enum {FLAG_EEPROM = 1, FLAG_GROUP = 2, FLAG_NOGUI = 4, FLAG_SIGNED = 8, FLAG_READONLY = 16, FLAG_STATUS_INFO = 32 };
void cmd_void(byte cmd_index, byte argc, char**argv) {}

const s_cmd_info cmd_info_table[] =
{
  // func          options
  {cmd_void,       FLAG_GROUP},
  {cmd_info,       FLAG_NOGUI},
  {cmd_pin_status, FLAG_STATUS_INFO},
  {cmd_task_status, FLAG_STATUS_INFO},
  {cmd_arm,        FLAG_NOGUI},
  {cmd_disarm,     FLAG_NOGUI},
  {cmd_fire,       FLAG_NOGUI},
  {cmd_stop,       FLAG_NOGUI},
  {cmd_report,     FLAG_NOGUI},
  {cmd_void,       FLAG_GROUP},
  {cmd_pins                  },
  {cmd_tasks                 },
  {cmd_write                 },
  {cmd_scope                 },
  {cmd_set_var,    FLAG_NOGUI}, // set echo variable (index 14 is hard-coded in the table below
  {cmd_reset                 },
  {cmd_halt                  }
#if defined(DEBUG)
  ,{cmd_set_var               } // set verbose variable (index 17 is hard-coded in the table below
#endif
};

const s_cmd_var cmd_var_table[] = {
  // address, lower, higher, cmd_index
  {&echo,      0, 1, 14},
#if defined(DEBUG)
  {&verbose, 0, 3, 17}
#endif
};

byte find_var(byte ci)
{
  for (byte i = 0; i < countof(cmd_var_table); ++i)
  {
    if (cmd_var_table[i].cmd_index == ci)
    {
      return i;
    }
  }
  return NOVAR;
}

void reset_vars()
{
  halt              = 0;
  echo              = 0;
#if defined(DEBUG)
  verbose           = DEFAULT_VERBOSE;    // verbose: send diagnostic output to serial port
#endif
}

//#define VARADDRESS (2*sizeof(unsigned short))
#define PINADDRESS 2
#define PINSIZE ((byte)(int)(&((struct pin *)NULL)->state))
#define TASKADDRESS (PINADDRESS + sizeof(pin_count) + PINCOUNT * PINSIZE)
#define TASKSIZE ((byte)(int)(&((struct task *)NULL)->counter))
#define EEPROM_USAGE (TASKADDRESS + sizeof(task_count) + TASKCOUNT * TASKSIZE)

void check_eeprom_usage()
{
  if (EEPROM_USAGE > EEPROM_SIZE) {
    Serial.print(F("EEPROM usage ")); Serial.print(EEPROM_USAGE); Serial.print(F(" exceeds size ")); Serial.print(EEPROM_SIZE); Serial.print(F(EOL));
    byte len = PINSIZE;
    Serial.print(F("pin  memory size: ")); Serial.print(len);
    len = TASKSIZE;
    Serial.print(F("task memory size: ")); Serial.print(len); Serial.print(F(EOL));
  }
}

bool check_model_and_revision()
{
  byte model;
  byte revision;
  EEPROM.get(0, model);
  if (model != MODEL)
  {
    EEPROM.put(0, (byte)MODEL);
  }
  EEPROM.get(sizeof(byte), revision);
  if (revision != REVISION)
  {
    EEPROM.put(sizeof(byte), (byte)REVISION);
  }
  return (model == MODEL) && (revision == REVISION);
}

/*
  void store_vars()
  {
  int address = VARADDRESS; // model and revision
  long tmp;
  for(byte ci = 0; ci < countof(cmd_info_table); ++ci)
  {
    const s_cmd_info * info = &cmd_info_table[ci];
    if(info->flags & FLAG_EEPROM)
    {
      byte vi = find_var(ci);
      if(vi != NOVAR)
      {
        const s_cmd_var * var = &cmd_var_table[vi];
        EEPROM.get(address,tmp);
        if(tmp != *var->address)
        {
          EEPROM.put(address,*var->address);
        }
        address += sizeof(*var->address);
      }
    }
  }
  }

  void restore_vars()
  {
  int address = VARADDRESS; // model and revision
  for(byte ci = 0; ci < countof(cmd_info_table); ++ci)
  {
    const s_cmd_info * info = &cmd_info_table[ci];
    if(info->flags & FLAG_EEPROM)
    {
      byte vi = find_var(ci);
      if(vi != NOVAR)
      {
        const s_cmd_var * var = &cmd_var_table[vi];
        EEPROM.get(address,*var->address);
        address += sizeof(*var->address);
      }
    }
  }
  }
*/

#if !defined(EX_EEPROM_ADDR)

void store_bytes(int & address, void * vmemory, int count)
{
  byte * memory = (byte*)vmemory;
  for (; count; --count, ++memory, ++address)
  {
    EEPROM.update(address, *memory);
  }
}

void restore_bytes(int & address, void * vmemory, int count)
{
  byte * memory = (byte*)vmemory;
  for (; count; --count, ++memory, ++address)
  {
    *memory = EEPROM.read(address);
  }
}

void store_pins()
{
  int address = PINADDRESS;
  store_bytes(address, &pin_count, sizeof(pin_count));
  byte len = PINSIZE;
  for (byte pi = 0; pi < pin_count; ++pi)
  {
    store_bytes(address, pins + pi, len);
  }
}

void restore_pins()
{
  int address = PINADDRESS;
  byte tmp = EEPROM.read(address);
  address += sizeof(tmp);
  if (tmp >= PINCOUNT) tmp = PINCOUNT;
  pin_count = tmp;
  byte len = PINSIZE;
  for (byte pi = 0; pi < pin_count; ++pi)
  {
    restore_bytes(address, pins + pi, len);
  }
  init_pins();
}

void store_tasks()
{
  int address = TASKADDRESS;
  store_bytes(address, &task_count, sizeof(task_count));
  byte len = TASKSIZE;
  for (byte ti = 0; ti < task_count; ++ti)
  {
    store_bytes(address, tasks + ti, len);
  }
}

void restore_tasks()
{
  int address = TASKADDRESS;
  byte tmp = EEPROM.read(address);
  address += sizeof(tmp);
  if (tmp >= TASKCOUNT) return;
  task_count = tmp;
  byte len = TASKSIZE;
  for (byte ti = 0; ti < task_count; ++ti)
  {
    restore_bytes(address, tasks + ti, len);
  }
  init_tasks(0, -1);
}

#else

void store_pins()
{
  uint16_t address = PINADDRESS;
  eeprom.update(address, &pin_count, sizeof(pin_count));
  byte len = PINSIZE;
  for (byte pi = 0; pi < pin_count; ++pi)
  {
    eeprom.update(address, pins + pi, len);
  }
}

void restore_pins()
{
  uint16_t address = PINADDRESS;
  byte tmp = 0;
  if (!eeprom.read(address, &tmp, sizeof(tmp)))
  {
    Serial.print(tmp);
    Serial.print(F("error reading pin count from external EEPROM" EOL));
    return;
  }
  if (tmp >= PINCOUNT) tmp = PINCOUNT;
  pin_count = tmp;
  byte len = PINSIZE;
  for (byte pi = 0; pi < pin_count; ++pi)
  {
    if (eeprom.read(address, pins + pi, len) != len)
    {
      Serial.print(F("error reading pin definition from external EEPROM" EOL));
    }
  }
  init_pins();
}

void store_tasks()
{
  uint16_t address = TASKADDRESS;
  eeprom.update(address, &task_count, sizeof(task_count));
  byte len = TASKSIZE;
  for (byte ti = 0; ti < task_count; ++ti)
  {
    eeprom.update(address, tasks + ti, len);
  }
}

void restore_tasks()
{
  uint16_t address = TASKADDRESS;
  byte tmp = 0;
  if (!eeprom.read(address, &tmp, sizeof(tmp)))
  {
    Serial.print(F("error reading task count from external EEPROM" EOL));
    return;
  }
  if (tmp > TASKCOUNT) tmp = TASKCOUNT;
  task_count = tmp;
  byte len = TASKSIZE;
  for (byte ti = 0; ti < task_count; ++ti)
  {
    eeprom.read(address, tasks + ti, len);
  }
  init_tasks(0, -1);
}

#endif

void cmd_write(byte cmd_index, byte argc, char**argv)
{
  store_tasks();
  Serial.print(F("tasks written." EOL));
}

inline void SerialWriteULong(unsigned long ul)
{
  byte * p = (byte*)&ul;
  Serial.write(p[0]);
  Serial.write(p[1]);
  Serial.write(p[2]);
  Serial.write(p[3]);
}

void send_scope_data()
{
  for(byte pi = 0; pi < pin_count; ++pi)
  {
    struct pin & p = pins[pi];
    if(!p.changed) continue;
    p.changed = false;
#if defined(DEBUG)
    if(!verbose)
#endif
#if defined(USEADC)
    if(p.mode == MODADC) 
    {
      Serial.write(pi); Serial.write(byte(p.state>>2)); SerialWriteULong(p.tick);
    } 
    else    
#endif
    {
    // read the pin state from the device because the isr() could have changed it ?
      Serial.write(pi); Serial.write(digitalRead(p.pin)); SerialWriteULong(p.tick);
    }
#if defined(DEBUG)
    else
    {Serial.print(F("*"));Serial.print(pi); Serial.print(" "); Serial.print(p.state); Serial.print(" "); Serial.print(p.tick); Serial.print(EOL);}
#endif
  }
}

void cmd_scope(byte cmd_index, byte argc, char**argv)
{
  Serial.print(F("Entering oscilloscope mode. Send any character to end." EOL));
  unsigned long ts = micros();
#if defined(DEBUG)
  if(!verbose)
#endif
  {Serial.write(NOPIN);Serial.write((byte)-2); SerialWriteULong(ts);} // current tick count
#if defined(DEBUG)
  else
  {Serial.print("time "); Serial.print(micros()); Serial.print(EOL);}
#endif
  
  // send current values of the pins
  for(byte pi = 0; pi < pin_count; ++pi)
  {
    struct pin & p = pins[pi];
    p.tick = ts;
    p.changed = true;
  }
  send_scope_data();
  
  while(1)
  {
    if(task_count)
    {
      check_input_pins();
      tick_tasks();
      check_finished_tasks();
      send_scope_data();
    }
    else
    {
      for(byte pi = 0; pi < pin_count; ++pi)
      {
        struct pin & p = pins[pi];
        byte state = digitalRead(p.pin);
        if(state != p.state)
        {
#if defined(DEBUG)
          if(!verbose)
#endif
            {Serial.write(pi); Serial.write(state); SerialWriteULong(micros());}
#if defined(DEBUG)
          else
            {Serial.print(F("*"));Serial.print(pi); Serial.print(" "); Serial.print(digitalRead(p.pin)); Serial.print(" "); Serial.print(micros()); Serial.print(EOL);}
#endif
          p.state = state;
        }
      }
    }
    if(Serial.available())
    {
      char c = Serial.read();
      if(c != '?') break;
#if defined(DEBUG)
      if(!verbose)
#endif
      {Serial.write(NOPIN);Serial.write((byte)-2); SerialWriteULong(micros());}
    }
  }
#if defined(DEBUG)
  if(!verbose)
#endif
  {Serial.write(NOPIN);Serial.write((byte)-1); SerialWriteULong(micros());}
#if defined(DEBUG)
  else
  {Serial.print(F("end of scope mode" EOL));}
#endif
}

void init_vars()
{
  in_setup = 1;
  reset_vars();
  reset_pins();
  reset_tasks();
  if (check_model_and_revision())
  {
#if !defined(DEFAULTCONFIG)
    //restore_vars();
    restore_pins();
    restore_tasks();
  }
  else
  {
#endif
    Serial.print(F("resetting factory defaults." EOL));
    //store_vars();
    store_pins();
    store_tasks();
  }
  in_setup = 0;
}

void test_micros()
{
  int i;
  unsigned long t = micros();
  volatile unsigned long dummy;
  for (int i = 0; i < 100; i = i + 1)
  {
    dummy = micros();
  }
  t = micros() - t;
  Serial.print(F("micros : ")); Serial.print(t / 100); Serial.print(F(EOL));
}

// setup

void cmd_reset(byte cmd_index, byte argc, char**argv)
{
  reset_vars();
  //store_vars();
  reset_pins();
  store_pins();
  reset_tasks();
  store_tasks();
  Serial.print(F("reset done." EOL));
}

void cmd_set_var(byte cmd_index, byte argc, char**argv)
{
  const s_cmd_info * info = &cmd_info_table[cmd_index];
  byte vi = find_var(cmd_index);
  if ((!argc || (argv[0][0] == '?')) || (info->flags & FLAG_READONLY) || (vi == (byte) - 1))
  {
    cmd_info_var(cmd_index);
    return;
  }
  const s_cmd_text * text = &cmd_text_table[cmd_index];
  const s_cmd_var  * var  = &cmd_var_table[vi];
  Serial.print(FS(text->name));
  Serial.print(F("="));
  if (info->flags & FLAG_SIGNED)
  {
    long value;
    value = atol(argv[0]);
    if (value < (long) var->min_value) value = (long) var->min_value;
    if (value > (long) var->max_value) value = (long) var->max_value;
    *var->address = value;
    Serial.print((long)(*var->address)); Serial.print(F(EOL));
  }
  else
  {
    unsigned long value;
    value = atoul(argv[0]);
    if (value < var->min_value) value = var->min_value;
    if (value > var->max_value) value = var->max_value;
    *var->address = value;
    Serial.print(*var->address); Serial.print(F(EOL));
  }
  //store_vars();
}

// serial input
#define INPUT_SIZE 128
char input[INPUT_SIZE] = "";
int  input_cursor = 0;
int  input_complete = 0;
char * input_command;
#define MAX_ARG_COUNT 16
char * argv[MAX_ARG_COUNT];
byte   argc = 0;
//char buf[INPUT_SIZE];
char prev_c = -1;

int init_input()
{
  Serial.begin(BAUD_RATE);
  while (!Serial && (millis() < 2100)) {}
  return 0;
}

int read_input()
{
  char c;
  while (Serial.available()) {
    c = Serial.read();
    if (echo) {
      Serial.write(c);
    }
    if (c == '\n')
    {
      if (prev_c == '\r') 
      {
        prev_c = c;
        continue; // already processed
      }
      prev_c = c;
      c = '\r'; // seems to newline only: process as carriage return
    }
    else
    {
      prev_c = c;
    }
    if (c == '\r')
    {
      return 1;
    }
    input[input_cursor] = c;
    ++input_cursor;
    input[input_cursor] = 0;
    if (input_cursor >= (INPUT_SIZE - 1)) return 1;
  }
  return 0;
}

void reset_input()
{
  input_cursor = 0;
  input[0] = 0;
}

void parse_input()
{
  int i;
  byte equal_eaten;
  byte bracket_eaten;
  byte tab_eaten;
  memcpy(argv, 0, sizeof(argv));
  argc = 0;
  // read away all spaces
  for (i = 0; i < input_cursor; ++i) if (!isspace(input[i])) break;
  input_command = input + i;
  // proceed to '=' or space
  for (; i < input_cursor; ++i) if (isspace(input[i]) || (input[i] == '=') || (input[i] == '[')) break;
  if (input[i] == 0)
  {
    return;
  }
  // terminate the command and read away the '=' or the space
  equal_eaten = (input[i] == '=');
  bracket_eaten = (input[i] == '[');
  tab_eaten = (input[i] == '\t');
  input[i] = 0;
  ++i;
  if (tab_eaten)
  {
    // just tab separated fields without quotes
    for (argc = 0; argc < MAX_ARG_COUNT;)
    {
      if (input[0] == 0) break;
      argv[argc] = input + i;
      ++argc;
      for (; i < input_cursor; ++i) if (input[i] == '\t') break;
      if (input[i] == '\t') {
        input[i] = 0;
        ++i;
      }
    }
    return;
  }
  if (!bracket_eaten)
  {
    // read away the spaces
    for (; i < input_cursor; ++i) if (!isspace(input[i])) break;
    if (!equal_eaten)
    {
      if (input[i] == '=') {
        equal_eaten = 1;
        ++i;
      }
    }
  }
  for (argc = 0; argc < MAX_ARG_COUNT;)
  {
    for (; i < input_cursor; ++i) if (!isspace(input[i]) && (input[i] != '=')) break;
    if (!input[i]) break;
    char t = ' ';
    if (input[i] == '\'') {
      t = '\'';
      ++i;
    }
    if (input[i] == '[') {
      t = ']';
      ++i;
    }
    argv[argc] = input + i;
    ++argc;
    for (; i < input_cursor; ++i) if (!input[i] || (input[i] == t) || (input[i] == '=')) break;
    if (!input[i]) break;
    input[i] = 0; ++i;
  }
  /*
    serial.print(input_command);Serial.print(F(EOL));
    for(byte i = 0; i < argc; ++i)
    {
      Serial.print(i); Serial.print(": "); Serial.print(argv[i]);Serial.print(F(EOL));
    }
  */
}

byte find_command(char * command)
{
  for (byte ci = 0; ci < countof(cmd_text_table); ++ci)
  {
    if (!strcmp_P(command, cmd_text_table[ci].name))
    {
      return ci;
    }
  }
  return 0;
}

void process_input()
{
  parse_input();
  byte cmd_index = find_command(input_command);
  (*cmd_info_table[cmd_index].func)(cmd_index, argc, argv);
  reset_input();
}

void cmd_info_var(byte cmd_index)
{
  const s_cmd_text * text = &cmd_text_table[cmd_index];
  const s_cmd_info * info = &cmd_info_table[cmd_index];
  byte vi = find_var(cmd_index);
  if (info->flags & FLAG_GROUP)
  {
    Serial.print(FS(text->description));
    Serial.print(F(":" EOL));
    return;
  }
  Serial.print(ss);
  Serial.print(FS(text->name));
  if (vi != (byte) - 1)
  {
    const s_cmd_var * var = &cmd_var_table[vi];
    if (info->flags & FLAG_SIGNED)
    {
      Serial.print(F("="));
      Serial.print((long)(*var->address));
      Serial.print(F(": "));
      if ((long)var->max_value > (long)var->min_value)
      {
        Serial.print(F("["));
        Serial.print((long)var->min_value);
        Serial.print(F(" to "));
        Serial.print((long)var->max_value);
        Serial.print(F("] "));
      }
    }
    else
    {
      Serial.print(F("="));
      Serial.print(*var->address);
      Serial.print(F(": "));
      if (var->max_value > var->min_value)
      {
        Serial.print(F("["));
        Serial.print(var->min_value);
        Serial.print(F(" to "));
        Serial.print(var->max_value);
        Serial.print(F("] "));
      }
    }
  }
  else
  {
    Serial.print(F(": "));
  }
  Serial.print(FS(text->description)); Serial.print(F(EOL));
}

void cmd_info(byte cmd_index, byte argc, char**argv)
{
  byte hide_no_gui = argc && !strcmp(argv[0], "gui");
  Serial.print(FS(title)); Serial.print(MODEL); Serial.print(F(".")); Serial.print(REVISION); Serial.print(F(".")); Serial.print(VERSION); Serial.print(F(EOL));
  Serial.print(FS(info));
  for (byte ci = 0; ci < countof(cmd_info_table); ++ci)
  {
    const s_cmd_info & info = cmd_info_table[ci];
    if (hide_no_gui && (info.flags & FLAG_NOGUI)) continue;
    cmd_info_var(ci);
    if (info.flags & FLAG_STATUS_INFO) (*info.func)(ci, 0, NULL);
  }
}

void report_changes()
{
  for (byte pi = 0; pi < pin_count; ++pi)
  {
    struct pin & p = pins[pi];
    if (p.changed)
    {
      p.changed = false;
      Serial.print(F("p ")); Serial.print(pi + 1); Serial.print(F(" ")); Serial.print(p.state); Serial.print(F(EOL)); //Serial.print(F(": "));Serial.print(mode_code[p.mode]);Serial.print(F(EOL));
    }
  }
  for (byte ti = 0; ti < task_count; ++ti)
  {
    struct task & t = tasks[ti];
    if (t.changed)
    {
      t.changed = false;
      Serial.print(F("t ")); Serial.print(ti + 1); Serial.print(F(" ")); Serial.print(get_task_state(ti)); Serial.print(F(EOL));
    }
  }
}

void report_changes2()
{
  for (byte pi = 0; pi < pin_count; ++pi)
  {
    struct pin & p = pins[pi];
    if (p.changed)
    {
      p.changed = false;
      Serial.print(F("p[")); Serial.print(p.name); Serial.print(F("]=")); Serial.print(p.state); Serial.print(F(" ")); Serial.print(p.tick); Serial.print(F(EOL));
    }
  }
  for (byte ti = 0; ti < task_count; ++ti)
  {
    struct task & t = tasks[ti];
    if (t.changed)
    {
      t.changed = false;
      Serial.print(F("t[")); Serial.print(t.name); Serial.print(F("]=")); Serial.print(get_stat_name(get_task_state(ti))); Serial.print(F(EOL));
    }
  }
}

// loop

unsigned long atoul(char * s)
{
  unsigned long r = 0;
  for (; *s; ++s)
  {
    int i = *s - '0';
    if ((i < 0) || (i > 9)) break;
    unsigned long new_r = r * 10 + i;
    if (new_r < r) return r;
    r = new_r;
  }
  return r;
}

byte parse_index_or_name(char * s, void * pfv)
{
  pf_get_item_name pf = (pf_get_item_name) pfv;
  byte ti = NOTASK;
  byte k = 0;
  for (k = 0; isspace(s[k]); ++k) {}
  if (isdigit(s[k]))
  {
    ti = parse_ulong(s + k);
    if (ti != (byte) - 1) --ti;
    if (!(*pf)(ti)) ti = NOTASK;
    return ti;
  }
  // task name
  for (k = 0; ; ++k)
  {
    const char * name = (*pf)(k);
    if (!name) break;
    if (!strcmp(s, name))
    {
      ti = k;
      break;
    }
  }
  return ti;
}

unsigned long parse_ulong(const char * s)
{
  unsigned long v = 0;
  byte i = 0;
  while (isdigit(s[i]))
  {
    unsigned long t = v * 10 + (s[i] - '0');
    if (t < v) return -1;
    ++i;
    v = t;
  }
  return v;
}

long parse_long(const char * s)
{
  bool m = false;
  long v = 0;
  byte i = 0;
  if (*s == '-') {
    m = true;
    ++s;
  }
  if (!isdigit(s[i])) return -1;
  while (isdigit(s[i]))
  {
    long t = v * 10 + (s[i] - '0');
    if (t < v) return -1;
    ++i;
    v = t;
  }
  return m ? -v : v;
}

unsigned long parse_time(const char *s)
{
  unsigned long v = 1;
  const char * p = s + strlen(s);
  if (p == s) return -1;
  while ((p > s) && isspace(p[-1])) --p;
  const char * q = p;
  while ((q > s) && isalpha(q[-1])) --q;
  byte len = p - q;
  if (!strncmp(q, "min", len)) v = 60000000;
  if (!strncmp(q, "s", len)) v = 1000000;
  if (!strncmp(q, "ms", len)) v = 1000;
  if (!strncmp(q, "us", len)) v = 1;
  if (strchr(s, '.')) v *= atof(s);
  else v *= parse_ulong(s);
  if (v == -1) return -1;
  if (v == 0) return 0;
  if (v < MIN_PERIOD) v = MIN_PERIOD;
  if (v > HMAX_LONG) v = HMAX_LONG;
  return v;
}

void print_time(unsigned long t)
{
  if (t % 1000) {
    Serial.print(t);
    Serial.print(F("us"));
    return;
  }
  t /= 1000;
  if (t % 1000) {
    Serial.print(t);
    Serial.print(F("ms"));
    return;
  }
  t /= 1000;
  Serial.print(t); Serial.print(F("s"));
}

unsigned long parse_byte(const char * s)
{
  unsigned long v = parse_ulong(s);
  if (v == -1) return -1;
  if (v > 255) return -1;
  return v;
}

byte parse_enum(const char *s, const char * const * e)
{
  for (byte i = 0; e[i] ; ++i)
  {
    if (!strcmp(s, e[i]))
    {
      return i;
    }
  }
  return -1;
}

void print_options(char text[MAX_KEY_SIZE], byte o, const char *info)
{
  byte space = 0;
  byte m = 1;
  for (byte i = 0; o; ++i, m <<= 1)
  {
    if (o & m)
    {
      if (space) Serial.print(ss);
      Serial.print(find_index_key(text, info, i));
      space = 1;
      o &= ~m;
    }
  }
}

byte parse_options(const char * s, const char *info)
{
  byte v = 0;
  char text[MAX_KEY_SIZE];
  while (*s)
  {
    while (*s && isspace(*s)) ++s;
    if (!*s) break;
    const char * e = s + 1;
    while (*e && !isspace(*e)) ++e;
    int l = e - s;
    if (l > MAX_KEY_LENGTH) l = MAX_KEY_LENGTH;
    strncpy(text, s, l);
    text[l] = 0;
    byte i = find_key_index(info, text);
    if (i != (byte) - 1)
    {
      v |= (1 << i);
    }
    s = e;
  }
  return v;
}

#if defined(DEBUG)
#if defined(ARDUINO_FSP)
void print_free_memory(int line)
{
}
#else // AVR
extern void *__brkval;
extern void *__bss_end;
void print_free_memory(int line)
{
  int free_memory;
  if ((int)__brkval == 0)
    free_memory = ((int)&free_memory) - ((int)&__bss_end);
  else
    free_memory = ((int)&free_memory) - ((int)__brkval);
  Serial.print(F("free memory on line ")); Serial.print(line); Serial.print(F(" : ")); Serial.print(free_memory); Serial.print(F(EOL));
}
#endif
#endif

const char * find_index_key(char key[MAX_KEY_SIZE], const char * keys, byte index)
{
  char text[MAX_KEY_SIZE];
  text[MAX_KEY_LENGTH] = 0;
  key[0] = 0;
  byte i = 0; // index
  byte b = 0; // begin
  byte e = 0; // end
  byte o = 0; // offset
  while (1)
  {
    // loop that reads partial fragments and processes them
    // move remaining text forward
    if (b && (e == MAX_KEY_LENGTH))
    {
      strncpy(text, text + b, e - b);
      e -= b;
      b = 0;
    }
    byte cnt = MAX_KEY_LENGTH - e;
    strncpy_P(text + e, keys + o, cnt);
    o += cnt;
    while (1)
    {
      // loop that parse the text for the key
      while (text[e] && (text[e] != ',')) ++e;
      if (e == MAX_KEY_LENGTH) break; // need more text
      if ((text[e] == 0) || (text[e] == ','))
      {
        // full key
        if (index == i)
        {
          byte len = e - b;
          strncpy(key, text + b, len);
          key[len] = 0;
          return key;
        }
        ++i;
        if (text[e] == 0)
        {
          return es;
        }
        b = e + 1;
        e = b;
      }
    }
  }
  return es;
}

byte find_key_index(const char * keys, const char * key)
{
  char text[MAX_KEY_SIZE];
  text[MAX_KEY_LENGTH] = 0;
  byte i = 0; // index
  byte b = 0; // begin
  byte e = 0; // end
  byte o = 0; // offset
  byte l = strlen(key);
  while (1)
  {
    // loop that reads partial fragments and processes them
    // move remaining text forward
    if (b && (e == 15))
    {
      strncpy(text, text + b, e - b);
      e -= b;
      b = 0;
      //Serial.print("text moved: ");Serial.print(text);Serial.print(F(EOL));
    }
    byte cnt = MAX_KEY_LENGTH - e;
    strncpy_P(text + e, keys + o, cnt);
    o += cnt;
    //Serial.print("text read: ");Serial.print(text);Serial.print(F(EOL));
    while (1)
    {
      // loop that parse the text for the key
      while (text[e] && (text[e] != ',')) ++e;
      if (e == MAX_KEY_LENGTH) break; // need more text
      if ((text[e] == 0) || (text[e] == ','))
      {
        // full key
        if (((e-b) == l) && !strncmp(key, text + b, l))
        {
          return i;
        }
        ++i;
        if (text[e] == 0)
        {
          return -1;
        }
        b = e + 1;
        e = b;
      }
    }
  }
  return -1;
}
