#include "MatrixDriverDue.h"


MatrixDriverDue MatrixDriver;

//constructor
MatrixDriverDue::MatrixDriverDue()
{
  frontBuffer = &pixelBufferA;
  backBuffer = &pixelBufferB;
  frontBufferId = 1; //1 -> pixelBufferA, 0 -> pixelBufferB
  rowNr = 0; //row currently being drawn
  bitIndex = 0;
  timerDefaultPeriod = 100;
  doSwap = false;
  #ifdef DEBUG
  allowCapture = true; //debug var
  #endif
  
  
  //set pin modes
  pinMode (PIN_R1, OUTPUT);
  pinMode (PIN_G1, OUTPUT);
  pinMode (PIN_B1, OUTPUT);
  pinMode (PIN_R2, OUTPUT);
  pinMode (PIN_G2, OUTPUT);
  pinMode (PIN_B2, OUTPUT);
  pinMode (PIN_CLK, OUTPUT);
  pinMode (PIN_OE, OUTPUT);
  pinMode (PIN_A, OUTPUT);
  pinMode (PIN_B, OUTPUT);
  pinMode (PIN_C, OUTPUT);
  pinMode (PIN_LAT, OUTPUT);
  
  //driver = *this;
}


//start drawing by starting the timing timer that is used to time the LED's BCM modulation
void MatrixDriverDue::start()
{
  //Serial.println("driver started");
  //setupModulationTimer(timerDefaultPeriod * (bitIndex+1)); // FIX THIS TO MACHTEN
  setupModulationTimer(timerDefaultPeriod * pow(2, bitIndex));
    
  //create drawing interrupt / timer
  //Timer3.attachInterrupt(drawAllRowes);
  //Timer3.start(timerDefaultPeriod * (bitIndex+1)); // Calls the interrupt method with a default time of 200 micro seconds * the bit currently used in our BCM algorithm
}



//loops over all rows to draw
void MatrixDriverDue::drawAllRowes()
{
  //Timer3.start(timerDefaultPeriod * (bitIndex+1)); //restart timer (TODO: manual timer instellen)
  //setupModulationTimer(timerDefaultPeriod * (bitIndex+1));   // FIX THIS TO MACHTEN
  setupModulationTimer(timerDefaultPeriod * pow(2, bitIndex)); 
  
  #ifdef DEBUG
  beginTime=micros();  //debug
  #endif
  //drawing
  draw2Rows();  //takes about 100 micros for one line to draw
  #ifdef DEBUG
  if(allowCapture)   //debug
  {
    totalTime=micros()-beginTime;
    allowCapture = false;
    Serial.println(totalTime);
  }
  #endif

  //adjust bit index
  bitIndex++;
  if (bitIndex > 3)
  {
    bitIndex = 0;
    
    //adjust row index
    rowNr++;
    if (rowNr > 7) 
    {
      rowNr=0;
      if (doSwap)
      {
        //Serial.println("swap");
        swapBuffers(); // swap front & backbuffer to display newly painted data
      }
    }
  }
}



//draws 2 rows at once, one in the upper half of the screen, one in the bottom half
void MatrixDriverDue::draw2Rows()
{
  byte rowAddress = rowNr;
  //select rows
  digitalWriteDirect(PIN_A, rowAddress & 1); //AND with 1-bitmask to get lower address bit
  rowAddress = rowAddress >> 1;             //now shift bits one to the right
  digitalWriteDirect(PIN_B, rowAddress & 1); //and get the next bit by doing another AND
  rowAddress = rowAddress >> 1;
  digitalWriteDirect(PIN_C, rowAddress & 1);
    
  digitalWriteDirect(PIN_OE, HIGH);
  digitalWriteDirect(PIN_LAT, HIGH);
  
  //clock some bits
  uint16_t color;
  for (byte i=0; i<32; i++)
  {
    //get pixel color from membuffer for upper row in LED matrix
    color = (*frontBuffer)[rowNr][i];
    //now shift bits and set appropriate data to output pins
    color = color >> (bitIndex + 1);
    digitalWriteDirect(PIN_B1, color & 0x01);
    digitalWriteDirect(PIN_G1, (color>>6) & 0x01);
    digitalWriteDirect(PIN_R1, (color>>11) & 0x01);
    //uint32_t ioReg = 0;
    //ioReg &= (color & 0x01) << 6;
    //ioReg &= ((color>>5) & 0x01) << 4;
    //ioReg &= ((color>>11) & 0x01) << 2;
    
    //get pixel color from membuffer for lower row in LED matrix
    color = (*frontBuffer)[rowNr+8][i];
    //now shift bits and set appropriate data to output pins
    color = color >> (bitIndex + 1);
    digitalWriteDirect(PIN_B2, color & 0x01);
    digitalWriteDirect(PIN_G2, (color>>6) & 0x01);
    digitalWriteDirect(PIN_R2, (color>>11) & 0x01);
    //ioReg &= (color & 0x01) << 17;
    //ioReg &= ((color>>5) & 0x01) << 19;
    //ioReg &= ((color>>11) & 0x01) << 8;
    
    //PIOC->PIO_ODSR &= ioReg;
    
    //set a pulse in our clock line
    digitalWriteDirect(PIN_CLK, LOW);
    digitalWriteDirect(PIN_CLK, HIGH);
    //g_APinDescription[PIN_CLK].pPort -> PIO_CODR = g_APinDescription[PIN_CLK].ulPin;
    //g_APinDescription[PIN_CLK].pPort -> PIO_SODR = g_APinDescription[PIN_CLK].ulPin;
    //PIOA->PIO_CODR |= 0x8000;
    //PIOA->PIO_SODR |= 0x8000;
  }
  
  
  digitalWriteDirect(PIN_LAT, LOW);
  digitalWriteDirect(PIN_OE, LOW);
}



//setup interrupt timer
void MatrixDriverDue::setupModulationTimer(long microseconds)
{
    double frequency = 1000000.0 / microseconds;
    
    // Tell the Power Management Controller to disable 
    // the write protection of the (Timer/Counter) registers:
    pmc_set_writeprotect(false);
    
    // Enable clock for the timer
    pmc_enable_periph_clk((uint32_t)TC3_IRQn);


    // Find the best clock for the wanted frequency   
    //calculate best divisor
    uint32_t rc = 0;
    uint8_t clock;
    /*
	Pick the best Clock, thanks to Ogle Basil Hall!

	Timer		Definition
	TIMER_CLOCK1	MCK /  2
	TIMER_CLOCK2	MCK /  8
	TIMER_CLOCK3	MCK / 32
	TIMER_CLOCK4	MCK /128
    */
    /*struct {
      uint8_t flag;
      uint8_t divisor;
    } clockConfig[] = {
      { TC_CMR_TCCLKS_TIMER_CLOCK1,   2 },
      { TC_CMR_TCCLKS_TIMER_CLOCK2,   8 },
      { TC_CMR_TCCLKS_TIMER_CLOCK3,  32 },
      { TC_CMR_TCCLKS_TIMER_CLOCK4, 128 }
    };
    float ticks;
    float error;
    int clkId = 3;
    int bestClock = 3;
    float bestError = 1.0;
    float roundedTicks;
    
    
    do
    {
      ticks = (float) VARIANT_MCK / frequency / (float) clockConfig[clkId].divisor;
      
      //avoid using abs()
      roundedTicks = round(ticks);
      if (ticks > roundedTicks)
        error = ticks - roundedTicks;
      else
        error = roundedTicks - ticks;
	beginTime=micros();
      if (error < bestError)
      {
	bestClock = clkId;
	bestError = error;
      }
    } while (clkId-- > 0);*/
    
    //ticks = (float) VARIANT_MCK / frequency / (float) clockConfig[bestClock].divisor;
    rc = (float) VARIANT_MCK / frequency / 8;
    //rc = (uint32_t) round(ticks);
    //clock = clockConfig[bestClock].flag;
    clock = TC_CMR_TCCLKS_TIMER_CLOCK2;
    //end of calculation for best divisor
    
    
    // Set up the Timer in waveform mode which creates a PWM
    // in UP mode with automatic trigger on RC Compare
    // and sets it up with the determined internal clock as clock input.
    TC_Configure(TC1, 0, TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC | clock);
    // Reset counter and fire interrupt when RC value is matched:
    TC_SetRC(TC1, 0, rc);
    // Enable the RC Compare Interrupt...
    TC1->TC_CHANNEL[0].TC_IER=TC_IER_CPCS;
    // ... and disable all others.
    TC1->TC_CHANNEL[0].TC_IDR=~TC_IER_CPCS;
     
    //START TIMER
    NVIC_ClearPendingIRQ(TC3_IRQn);
    NVIC_EnableIRQ(TC3_IRQn);
	
    TC_Start(TC1, 0);
   
}


//timer interrupt method, note: no method of MatrixDriver class!
void TC3_Handler()
{
  TC_GetStatus(TC1, 0);
  MatrixDriver.drawAllRowes();
} 



//allow swapping front & backbuffers to show newly painted data
void MatrixDriverDue::swapBuffers()
{
  //clear old frontbuffer before reusing
  for (uint8_t y=0; y < 16; y++) 
  {      
    for (uint8_t x=0; x < 32; x++) 
    {
      (*frontBuffer)[y][x] = 0;
    }
  }
  
  //swap
  if (frontBufferId == 1)
  {
    frontBufferId = 0; //1 -> pixelBufferA, 0 -> pixelBufferB
    frontBuffer = &pixelBufferB;
    backBuffer = &pixelBufferA;
  }
  else
  {
    frontBufferId = 1;
    frontBuffer = &pixelBufferA;
    backBuffer = &pixelBufferB;
  }
  
  doSwap = false;
}




//faster digitalWrite()
inline void MatrixDriverDue::digitalWriteDirect(int pin, boolean val)
{
  if(val) g_APinDescription[pin].pPort -> PIO_SODR = g_APinDescription[pin].ulPin;
  else    g_APinDescription[pin].pPort -> PIO_CODR = g_APinDescription[pin].ulPin;
}

//faster digitalRead()
inline int MatrixDriverDue::digitalReadDirect(int pin)
{
  return !!(g_APinDescription[pin].pPort -> PIO_PDSR & g_APinDescription[pin].ulPin);
}



