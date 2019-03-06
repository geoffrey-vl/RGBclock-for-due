#include "Arduino.h"

//pins
#define PIN_R1 34
#define PIN_G1 36
#define PIN_B1 38
#define PIN_R2 40
#define PIN_G2 44
#define PIN_B2 46
#define PIN_CLK 22
#define PIN_OE 42
#define PIN_A 26
#define PIN_B 30
#define PIN_C 28
#define PIN_LAT 24

//#define DEBUG


//driver only for Arduino Due, if compiler says "pmc_set_writeprotect" not defined make sure
//you have the correct hardware selected being "Arduino Due programming port"
class MatrixDriverDue
{
  /* ########### PUBLIC ################*/
  public:
  
  //Constructor
  MatrixDriverDue(void);
  
  //start drawing by starting the timing timer that is used to time the LED's BCM modulation
  void start(void); 
  
  //pointer to the backbuffer, either buffer A or B during runtime
  uint16_t (*backBuffer)[16][32]; 
  
  //bool allowing swapping front & backbuffers to show newly painted data
  bool doSwap;
  
  //loops over all rows to draw
  void drawAllRowes(void);
  
  
  
  /* ########### PRIVATE ################*/
  private:
  
  //pixel data arrays
  //one is used as frontbuffer which can be used to alter data into
  //the other one  is used as frontbuffer, this is the one that is being displayed
  //new data first has to be written in the backbuffer and then by swapping buffers
  //it can be displayed. 
  uint16_t pixelBufferA[16][32];
  uint16_t pixelBufferB[16][32];
  
  //pointer to the frontbuffer, either buffer A or B during runtime
  uint16_t (*frontBuffer)[16][32]; 

  //byte indicating which buffer is currently the frontbuffer (1 -> pixelBufferA, 0 -> pixelBufferB)
  uint8_t frontBufferId;
  
  //row currently being drawn
  byte rowNr; 
  //so each  pixel has a True Color (16-bit) as value in the backbuffer. We use BCM modulation to display those true colors.
  //Remark that the display driver is limited to 4/4/4 (R/G/B) 12-bit color, which means we have 4 bits for each primary color (R, G or B).
  //In BCM the weight of each bit is used to dim the LED, and be combining dimming of R, G, B leds we get our 12-bit color.
  //bitIndex is the bit nr that is currently being draw, and depending on the weight of this bit our drawing interrupt timer
  //is set longer or shorter. So if for example the red led has code 0010 it will light up longer than with code 0001,
  //and so appear more bright, or more red when mixig with other colors.
  byte bitIndex; 
  
  //the default interrupt timer timing, 100 microseconds. Bits with index 0 will use this default period.
  int timerDefaultPeriod;
  
  #ifdef DEBUG
  bool allowCapture; //debug var
  unsigned long beginTime; //debug var
  unsigned long totalTime; //debug var
  #endif
  
  //timer overflow interrupt method
  void TC3_Handler(void);
  
  //method that sets up the interrupt timer that is associated to the BCM modulation of the display
  void setupModulationTimer(long microseconds);
  
  //draws 2 rows at once, one in the upper half of the screen, one in the bottom half
  void draw2Rows(void);
  
  //swap front & backbuffers to show newly painted data
  void swapBuffers(void);
  
  //faster digitalWrite()
  inline void digitalWriteDirect(int pin, boolean val);
  
  //faster digitalRead()
  inline int digitalReadDirect(int pin);
  
};



// Needs to be public, because the handlers are outside class
extern MatrixDriverDue MatrixDriver;
