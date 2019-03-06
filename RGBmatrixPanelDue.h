#include "MatrixDriverDue.h"


//constructor
class RGBmatrixPanelDue
{
  public:
  
  //Constructor
  RGBmatrixPanelDue(void);
  
  //start the drawing driver
  void start(void);
  
  //refresh the screen with newly painted data (= swap buffers)
  void refresh(void);
  
  //set the color of a given pixel
  void setPixel(uint8_t x, uint8_t y, uint16_t color);
  
  //get the color of a given pixel
  uint16_t getPixel(uint8_t x, uint8_t y);
  
  // Draw a character
  void drawChar(uint8_t x, uint8_t y, unsigned char c, uint16_t color);
  
  //print out the bits of a given high color
  static void printHighColorBits(uint16_t color);
  
  //print out the bits of a given high color
  static void printDisplayColorBits(uint16_t color);
  
  
  
  
  private:
  //the drawing driver
  //MatrixDriverDue matrixDriver;
  uint8_t _width, _height;
};
