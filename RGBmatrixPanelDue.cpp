#include "RGBmatrixPanelDue.h";
#include "glcdfont.c"
#ifdef __AVR__
 #include <avr/pgmspace.h>
#else
 #define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#endif


//constructor
RGBmatrixPanelDue::RGBmatrixPanelDue()
{
  //matrixDriver = MatrixDriverDue::MatrixDriverDue();
  _width = 32;
  _height = 16;
}



//start the background driver
void RGBmatrixPanelDue::start()
{
  MatrixDriver.start();
}


//refresh the screen with newly painted data (= swap buffers)
void RGBmatrixPanelDue::refresh()
{
  while(MatrixDriver.doSwap == true) delay(1); // if refresh was already called, wait for interrupt to clear it
  MatrixDriver.doSwap = true;
}


//set the color of a given pixel
void RGBmatrixPanelDue::setPixel(uint8_t x, uint8_t y, uint16_t color)
{
  while(MatrixDriver.doSwap == true) delay(1); // if refresh was already called, wait for interrupt to clear it
  (*MatrixDriver.backBuffer)[y][x] = color;
}



//get the color of a given pixel
uint16_t RGBmatrixPanelDue::getPixel(uint8_t x, uint8_t y)
{
  return (*MatrixDriver.backBuffer)[y][x];
}





// Draw a character
void RGBmatrixPanelDue::drawChar(uint8_t x, uint8_t y, unsigned char c, uint16_t color) 
{
  if((x >= _width)            || // Clip right
     (y >= _height)           || // Clip bottom
     ((x + 6 - 1) < 0) || // Clip left
     ((y + 8 - 1) < 0))   // Clip top
    return;

  for (uint8_t i=0; i<6; i++ ) 
  {
    uint8_t line;
    if (i == 5) 
      line = 0x0;
    else 
      line = pgm_read_byte(font+(c*5)+i);
    for (uint8_t j = 0; j<8; j++) 
    {
      if (line & 0x1)
          setPixel(x+i, y+j, color);
      line >>= 1;
    }
  }
}





//print out the bits of a given high color (for debugging)
void RGBmatrixPanelDue::printHighColorBits(uint16_t color)
{
  String s = "";
  for(int i=0; i<16; i++)
  {
    if (i==5 || i==11) s = ' ' + s;
    s = (color & 0x01) + s;
    color = color>>1;
  }
  Serial.println(s);
}




//print out the bits of a given high color (for debugging)
void RGBmatrixPanelDue::printDisplayColorBits(uint16_t color)
{
  String s = "";
  for(int i=0; i<16; i++)
  {
    if (i==0 || i==5 || i==11) 
    {
      s = ' ' + s;
      color = color>>1;
      continue;
    }
    else if (i==6)
    {
      color = color>>1;
      continue;
    }
    s = (color & 0x01) + s;
    color = color>>1;
  }
  Serial.println(s);
}
