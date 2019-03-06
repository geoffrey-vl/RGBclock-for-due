/*
This program queries an NTP time server to get the current NTP time
(seconds since 1/1/1900, UTC (GMT)), then uses the Arduino's internal
timer to keep relative time.  The clock is re-synchronized roughly
once per day.  This minimizes NTP server misuse/abuse. It displays time
and also takes a temperature reading from the ADT7410 I2C temp sensor.
Internet connection goes through the Adafruit C3300 breakout board, it 
reads the Wifi configuration from SD card on boot. Temperature samples
and according time stamps are also stored on the SD card.
Does only apply for Arduino Due devices because sketch size > 50kbytes

ADT7410 13/16-bit digital temperature sensor
RED (VDD): 2.7 ... 5.5V
BROWN (GND): 0V
Arduino due, wires:
PURPLE 1 (SCL): SCL (pin 21)
PURPLE 2 (SDA): SDA = (pin 20)
Arduino IDE 1.5 compatible
*/


#include "RGBmatrixPanelDue.h"
#include <Wire.h>
#include <Adafruit_CC3000.h>
#include <ccspi.h>
#include <SPI.h>
#include <SD.h>
#include "utility/sntp.h"

// C3300 interrupt and control pins
#define ADAFRUIT_CC3000_IRQ   3     // MUST be an interrupt pin!
#define ADAFRUIT_CC3000_VBAT  5     // These can be any two pins
#define ADAFRUIT_CC3000_CS    10    // Wifi Chip Select pin
#define ADAFRUIT_SDCARD_CS    4     // SD Card Chip Select pin
/*
Use hardware SPI for the remaining pins, on an UNO/DUE, 
SCK = 13
MISO = 12
MOSI = 11
*/

//ADT7410 config
#define ADT7410_I2C_ADDRESS 0x48
#define tempRegister 0x00
#define configRegister 0x03
#define selectCode16bitMode 0x80

//display
RGBmatrixPanelDue panel;

//WLAN config
Adafruit_CC3000 cc3000 = Adafruit_CC3000(ADAFRUIT_CC3000_CS, ADAFRUIT_CC3000_IRQ, ADAFRUIT_CC3000_VBAT,
                                         SPI_CLOCK_DIVIDER); // you can change this clock speed
                                         
//connection timeout vars
const unsigned long
  connectTimeout  = 15L * 1000L, // Max time to wait for server connection
  responseTimeout = 15L * 1000L; // Max time to wait for data from server
  
//Arguments to SNTP client constructor:
//	1 - Primary Network Time Server URL (can be NULL)
//	2 - Secondary Network Time Server URL (also can be NULL)
//	3 - Local UTC offset in minutes (US Eastern Time is UTC - 5:00
//	4 - Local UTC offset in minutes for Daylight Savings Time (US Eastern DST is UTC - 4:00
//	5 - Enable Daylight Savings Time adjustment (not implemented yet, so use false please)
sntp mysntp = sntp(NULL, "time.nist.gov", (short)(1 * 60), (short)(1 * 60), false);

//vars where we store the config data read from SD card
#define CONF_ARRAY_LENGTH    32  /* Max 32 chars */
char wlan_ssid[CONF_ARRAY_LENGTH]; 
char wlan_pass[CONF_ARRAY_LENGTH];
int wlan_security; // Security can be WLAN_SEC_UNSEC (0), WLAN_SEC_WEP (1), WLAN_SEC_WPA (2) or WLAN_SEC_WPA2 (3)



#define WLAN_CONFIG_FILE    "init.txt"
#define TEMP_LOG            "temps.log"


  
// Type SNTP_Timestamp is 64-bit NTP time. High-order 32-bits is seconds since 1/1/1900
//   Low order 32-bits is fractional seconds
SNTP_Timestamp_t now;

// Type NetTime_t contains NTP time broken out to human-oriented values:
//	uint16_t millis; ///< Milliseconds after the second (0..999)
//	uint8_t	 sec;    ///< Seconds after the minute (0..59)
//	uint8_t	 min;    ///< Minutes after the hour (0..59)
//	uint8_t	 hour;   ///< Hours since midnight (0..23)
//	uint8_t	 mday;   ///< Day of the month (1..31)
//	uint8_t	 mon;    ///< Months since January (0..11)
//	uint16_t year;   ///< Year.
//	uint8_t	 wday;	 ///< Days since Sunday (0..6)
//	uint8_t	 yday;   ///< Days since January 1 (0..365)
//	bool	 isdst;  ///< Daylight savings time flag, currently not supported	
NetTime_t timeExtract;
NetTime_t timeNow;

//var that hold the amount of milliseconds between arduino boot and last NTP request
long millisLastNTPrequest;
//time between each NTP request (in miliseconds)
const long NTP_UPDATE_TIMER = 14400L * 1000L; //(14 400 000 = 24h)
byte lastClockValueForSeconds;

const char* VERSION = "0.9";

#define TEMPDRIFT 3
float temp = 0;







///////////////////////////// SETUP //////////////////////////////////////////////////////////////////////////////////////
void setup() 
{  
  Serial.begin(115200);
  
  Serial.println(F("\nInitialising ADT7410 ..."));
  Wire.begin();        // join i2c bus (address optional for master)
  setTempSensor16bitMode();
  temp = readTemp();
  
  Serial.println(F("Starting Display driver..."));
  panel = RGBmatrixPanelDue::RGBmatrixPanelDue();
  panel.start();
  
  printInitState('I', 'N', 'I', 'T');
  
  Serial.println(F("Initializing SD card..."));
  pinMode(ADAFRUIT_SDCARD_CS, OUTPUT);
  if (!SD.begin(4)) {
    Serial.println("Card failed, or not present");
    for(;;);
  }
  
  readConfigFromSD();
  
  Serial.println(F("\nInitialising the CC3000 ..."));
  if (!cc3000.begin()) {
    Serial.println(F("Unable to initialise the CC3000! Check your wiring?"));
    for(;;);
  }
  
  uint16_t firmware = checkFirmwareVersion();
  if ((firmware != 0x113) && (firmware != 0x118)) {
    Serial.println(F("Wrong firmware version!"));
    for(;;);
  }
  
  displayMACAddress();
  
  Serial.println(F("\nDeleting old connection profiles"));
  if (!cc3000.deleteProfiles()) {
    Serial.println(F("Failed!"));
    while(1);
  }
  
  printInitState('S', 'C', 'A', 'N');

  /* Attempt to connect to an access point */
  //char *ssid = wlan_ssid;             /* Max 32 chars */
  Serial.print(F("\nAttempting to connect to ")); Serial.println(wlan_ssid);
  
  /* NOTE: Secure connections are not available in 'Tiny' mode! */
  if (!cc3000.connectToAP(wlan_ssid, wlan_pass, wlan_security)) {
    Serial.println(F("Failed!"));
    while(1);
  }
   
  Serial.println(F("Connected!"));
  
  printInitState('D', 'H', 'C', 'P');
  
  /* Wait for DHCP to complete */
  Serial.println(F("Request DHCP"));
  while (!cc3000.checkDHCP()) {
    delay(100); // ToDo: Insert a DHCP timeout!
  }

  /* Display the IP address DNS, Gateway, etc. */  
  while (!displayConnectionDetails()) {
    delay(1000);
  }
  
  //do an NTP request at boot
  ntpUpdateClock();
  mysntp.ExtractNTPTime(mysntp.NTPGetTime(&now, true), &timeExtract); 
  updateClock();
  Serial.println(F("\n\nReady!\n"));
}










//////////////////////  LOOP //////////////////////////////////////////////////////////////////////////////////////////////
uint8_t rgb[3] = {255, 0, 0};
uint8_t dimDownIndex = 0;
uint8_t dimUpIndex = dimDownIndex + 1;

bool isDimingDown = false;

void loop() 
{
  displayColor(rgb[0], rgb[1], rgb[2]); //COLOR BACKGROUDND
  //displayRainbow();
  
  printTemp();
  printClock();
  panel.refresh();
  delay(100);

  //alternate background color code
  if (isDimingDown) //changing one of the colors downwards
  {
    rgb[dimDownIndex]--;
    if (rgb[dimDownIndex] == 255) // overflow detection
    {
      rgb[dimDownIndex] = 0;
      isDimingDown = false;
      
      dimDownIndex++;
      if (dimDownIndex > 2) dimDownIndex=0;
    }
  }
  else             //changing one of the colors upwards
  {
    rgb[dimUpIndex]++;
    if (rgb[dimUpIndex] == 0) // overflow detection
    {
      rgb[dimUpIndex] = 255;
      isDimingDown = true;
      
      dimUpIndex++;
      if(dimUpIndex> 2) dimUpIndex=0;
    }
  }
}





//print time and temperature
void printClock()
{
  // To reduce load on NTP servers, time is polled once per roughly 24 hour period.
  // Otherwise use millis() to estimate time since last query.  Plenty accurate.
  if (millis() > millisLastNTPrequest + NTP_UPDATE_TIMER) //do a new NTP request each 2 minutes
  {  
    ntpUpdateClock();
  }
  else if ( (millis() + NTP_UPDATE_TIMER * 2 ) < millisLastNTPrequest) //millis() overflow fix
  {
    ntpUpdateClock();
  }
  

  //extract time from soft RTC and convert to human readable struct
  mysntp.ExtractNTPTime(mysntp.NTPGetTime(&now, true), &timeExtract); 
  
  
  //because of some weird shit with the soft RTC (sometime seconds change downwards?! )
  //only update visuals when we notice a change in seconds upwards
  if (timeExtract.sec == lastClockValueForSeconds+1)
  {
    updateClock();
  }
  else if (timeExtract.sec == 0 && lastClockValueForSeconds == 59)
  {
    updateClock();
  }
  
  
  uint16_t color = 0;//65535;
  panel.drawChar(2, 9, (timeNow.hour/10)+48, color);
  panel.drawChar(9, 9, ((int)timeNow.hour%10)+48, color);
  panel.setPixel(15, 11, color);
  panel.setPixel(15, 13, color);
  panel.drawChar(17, 9, (timeNow.min/10)+48, color);
  panel.drawChar(24, 9, ((int)timeNow.min%10)+48, color);
}


void updateClock()
{
  lastClockValueForSeconds = timeExtract.sec;
  
  timeNow.hour = timeExtract.hour;
  timeNow.min = timeExtract.min;
  timeNow.sec = timeExtract.sec;
  
  Serial.print(F("Current local time is: "));
  Serial.print(timeNow.hour); Serial.print(F(":")); Serial.print(timeNow.min); Serial.print(F(":"));Serial.println(timeNow.sec);
  
}



//update clock through NTP servers
void ntpUpdateClock()
{
  bool succes = false;
  
  while (!succes)
  {
    Serial.print(F("Synchronizing with NTP server... "));
    succes = mysntp.UpdateNTPTime(); //try update
    
    if (succes)
    {
      Serial.println(F("* Synced"));
      millisLastNTPrequest = millis();
    }
    else
    {
      Serial.println(F("* Sync failed, retrying"));
    }
  }
}


int updatecounter = 100; //only update temp each 5s

void printTemp()
{
  float tempt;
  
  updatecounter++;
  if (updatecounter > 50)
  {
    updatecounter = 0;
    tempt = readTemp();
    if ((tempt > temp-TEMPDRIFT) && (tempt < temp+TEMPDRIFT)) temp = tempt;
    
    Serial.print(" *** temperature: ");
    Serial.print(temp);  //do a temperature readout
    Serial.println(" degrees Celcius");
  }
  

  uint16_t color = 0;//65535;
  panel.drawChar(1, 1, (temp/10)+48, color);
  panel.drawChar(8, 1, ((int)temp%10)+48, color);
  panel.setPixel(14, 7, color);
  tempt = temp*100;
  if(((int)tempt%10) > 4)
    tempt = (tempt+10)/10;
  else 
    tempt = tempt/10;
  panel.drawChar(16, 1, ((int)tempt%10)+48, color);
  panel.setPixel(23, 1, color);
  panel.setPixel(24, 1, color);
  panel.setPixel(23, 2, color);
  panel.setPixel(24, 2, color);
  panel.drawChar(26, 1, 'C', color);
}


//set ADT7410 in 16-bit temp value mode
void setTempSensor16bitMode() 
{
  Serial.println("Setting 16-bit mode...");
  Wire.beginTransmission(ADT7410_I2C_ADDRESS);
  Wire.write(configRegister);
  Wire.write(selectCode16bitMode);
}


//returns the temperature value read from ADT7410
float readTemp()
{
  //set read register
  Wire.beginTransmission(ADT7410_I2C_ADDRESS);
  Wire.write(tempRegister);
  Wire.endTransmission();
  //receive data
  Wire.requestFrom(ADT7410_I2C_ADDRESS, 2);
  byte MSB = Wire.read();
  byte LSB = Wire.read();
  
  //check for positive or negative sign
  boolean sign;
  if(MSB>0xA0) {
    sign=0; //negative
  } else {
    sign=1; //positive
  }
  
  //concat MSB&LSB
  float tempValue = MSB * 256;
  tempValue+=LSB;
  
  //BIN to DEC
  if(!(sign)){
    tempValue-=65536;
  }
  tempValue/=128;
  
  return tempValue;
}








///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Demote 24-bit color (8/8/8) to 16-bit-color (Adafruit_GFX 5/6/5)
uint16_t Convert24bitTo16bitColor(uint8_t r, uint8_t g, uint8_t b) 
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}



void displayRainbow()
{
  //draw top half
  uint8_t rgb[3] = {255, 0, 0};
  uint8_t rgbBackup[3] = {255, 0, 0};
  uint8_t dimUpIndex = 1;
  uint8_t dimDownIndex = 0;
  uint8_t dimUpIndexBackup = 1;
  uint8_t dimDownIndexBackup = 0;
  bool isDimingDown = false;
  bool isDimingDownBackup = false;
  for (uint8_t y=0; y < 16; y++) 
  {      
    for (uint8_t x=0; x < 32; x++) 
    {
      panel.setPixel(x, y, Convert24bitTo16bitColor(rgb[0], rgb[1], rgb[2]));
      //panel.setPixel(x, y, Convert24bitTo16bitColor(255, 255, 255));
      //print debug the bits of the color value of a given (high color) pixel
      if (x==1) //if statement used for displaying rainbow tilted
      {
        rgbBackup[0] = rgb[0];
        rgbBackup[1] = rgb[1];
        rgbBackup[2] = rgb[2];
        dimUpIndexBackup = dimUpIndex;
        dimDownIndexBackup = dimDownIndex;
        isDimingDownBackup = isDimingDown;
        //uint16_t color = panel.getPixel(x, y);
        //RGBmatrixPanelDue::printHighColorBits(color);
      }
      if (isDimingDown) //changing one of the colors downwards
      {
        rgb[dimDownIndex] -= 48;
        if (rgb[dimDownIndex] == 223) // overflow detection
        {
          rgb[dimDownIndex] = 0;
          isDimingDown = false;
          
          dimDownIndex++;
          if (dimDownIndex > 2) dimDownIndex=0;
        }
      }
      else             //changing one of the colors upwards
      {
        rgb[dimUpIndex] += 48;
        if (rgb[dimUpIndex] == 32) // overflow detection
        {
          rgb[dimUpIndex] = 255;
          isDimingDown = true;
          
          dimUpIndex++;
          if(dimUpIndex> 2) dimUpIndex=0;
        }
      }
    }
    rgb[0] = rgbBackup[0];
    rgb[1] = rgbBackup[1];
    rgb[2] = rgbBackup[2];
    dimDownIndex = dimDownIndexBackup;
    dimUpIndex = dimUpIndexBackup;
    isDimingDown = isDimingDownBackup;
  }
}






void displayColor(uint8_t r, uint8_t g, uint8_t b)
{
  for (uint8_t y=0; y < 16; y++) 
  {      
    for (uint8_t x=0; x < 32; x++) 
    {
      panel.setPixel(x, y, Convert24bitTo16bitColor(r, g, b));
    }
  }
}



// read the wlan configuration from SD card
void readConfigFromSD()
{
  Serial.println(F("Reading config file"));
  // open the config file. note that only one file can be open at a time,

  if (SD.exists(WLAN_CONFIG_FILE)) //read 
  {
    Serial.println(F("Reading user profile"));
    // read from the file until there's nothing else in it:
    File file = SD.open(WLAN_CONFIG_FILE);
    if (file)
    {
      int ctr = 0;
      while (file.available()) 
      {
        if (ctr == 0)
        {
          SD_readString(wlan_ssid, CONF_ARRAY_LENGTH, &file);
          Serial.print(F("SSID: ")); Serial.println(wlan_ssid);
          ctr++;
          continue;
        }
        else if (ctr == 1)
        {
          SD_readString(wlan_pass, CONF_ARRAY_LENGTH, &file);
          //Serial.print(F("Password: ")); Serial.println(wlan_pass);
          ctr++;
          continue;
        }
        //else
        char sec[2];
        SD_readString(sec, 2, &file);
        switch(sec[0])
        {
          case '3':
            wlan_security = WLAN_SEC_WPA2;
            Serial.print(F("Security: WPA2"));
            break;
          case '2': 
            wlan_security = WLAN_SEC_WPA;
            Serial.print(F("Security: WPA"));
            break;
          case '1': 
            wlan_security = WLAN_SEC_WEP;
            Serial.print(F("Security: WEP"));
            break;
          case '0': 
          default:
            wlan_security = WLAN_SEC_UNSEC;
            Serial.print(F("Security: Unsecured"));
            break;
        }
        break;
      }
      // close the file:
      file.close();
    }
    else
      Serial.println("error opening file");
  }
  else //create new data and store it
  {
    Serial.println(F("No user profile found, creating new profile"));
    boolean userInputDone = false;
    while (!userInputDone)
    {
      Serial.print(F("\nEnter SSID:\n# "));
      Serial_readString(wlan_ssid, CONF_ARRAY_LENGTH); 
      Serial.print(F("\nEnter password:\n# "));
      Serial_readString(wlan_pass, CONF_ARRAY_LENGTH);
      Serial.print(F("\nChoose WLAN security: (0 = unsecured, 1 = WEP, 2 = WPA, 3 = WPA2)\n# "));
      char sec[2]; //2 bytes, one for the input and one for the string-end char
      Serial_readString(sec, 2); 
      switch(sec[0])
      {
        case '3':
          wlan_security = WLAN_SEC_WPA2;
          break;
        case '2': 
          wlan_security = WLAN_SEC_WPA;
          break;
        case '1': 
          wlan_security = WLAN_SEC_WEP;
          break;
        case '0': 
        default:
          wlan_security = WLAN_SEC_UNSEC;
          break;
      }
     
      Serial.print(F("\nAre you shure you want to connect to ")); Serial.print(wlan_ssid); Serial.print(F("? (y/n)\n# "));
      char yn[2];
      Serial_readString(yn, 2); 
      if (yn[0] == 'y' || yn[0] == 'Y') //if user is sure, save settings...
      {
        //just to be sure delete previous config file anyways
        SD.remove(WLAN_CONFIG_FILE);
        //save settings to disk
        File file = SD.open(WLAN_CONFIG_FILE, FILE_WRITE);
        if (file)
        {
          file.println(wlan_ssid);
          file.println(wlan_pass);
          file.println(wlan_security);
          file.close();
          break;
        }
        else
          Serial.println("error opening file");
      }
    }
  }
  
}




// read from a file into a char array, the position where it reads depends on where the the previous read() ended
// text = char array to rad into
// ln = amount of chars to read
// fileptr = pointer to the file on SD card
void SD_readString(char* text, int ln, File* fileptr)
{
  File file = *fileptr;
  
  for(int i=0; i< ln; i++)
  {
    char c = file.read();
    
    if (c == '\r') c = '\0';
    
    text[i] = c;
    
    if (c == '\n') return;
  }
}



//read chars from serial, also display each char when a valid char
//text = text to read
//ln = amount of chars to read
void Serial_readString(char* text, int ln)
{
  int index = 0;
  
  while (true)
  {
    char c = Serial.read();
    
    if (c < '0' || c > 'z') 
      if (c != '\n') 
        continue; //skip reading invalid chars
        
    
    if (index == ln-1) break;

    if (c == '\n') 
    {
      text[index] = '\0'; //null terminate the string earlier if user used enter btn
      break;
    }
    else
    {
      Serial.print(c);
      text[index] = c; //story char in array
    }
    
    index++;
  }
  
  text[ln-1]= '\0'; //make sure there is an end to the string
  
  //now, if we red until the maxsize of our array, clean any other chars that
  //that have not been red yet
  while (Serial.available() > 0)
    Serial.read();
}



// Tries to read the IP address and other connection details
bool displayConnectionDetails(void)
{
  uint32_t ipAddress, netmask, gateway, dhcpserv, dnsserv;
  
  if(!cc3000.getIPAddress(&ipAddress, &netmask, &gateway, &dhcpserv, &dnsserv))
  {
    Serial.println(F("Unable to retrieve the IP Address!\r\n"));
    return false;
  }
  else
  {
    Serial.print(F("\nIP Addr: ")); cc3000.printIPdotsRev(ipAddress);
    Serial.print(F("\nNetmask: ")); cc3000.printIPdotsRev(netmask);
    Serial.print(F("\nGateway: ")); cc3000.printIPdotsRev(gateway);
    Serial.print(F("\nDHCPsrv: ")); cc3000.printIPdotsRev(dhcpserv);
    Serial.print(F("\nDNSserv: ")); cc3000.printIPdotsRev(dnsserv);
    Serial.println();
    return true;
  }
}


uint16_t checkFirmwareVersion(void)
{
  uint8_t major, minor;
  uint16_t version;
  
  #ifndef CC3000_TINY_DRIVER  
  if(!cc3000.getFirmwareVersion(&major, &minor))
  {
    Serial.println(F("Unable to retrieve the firmware version!\r\n"));
    version = 0;
  }
  else
  {
    Serial.print(F("C3300 Firmware V. : "));
    Serial.print(major); Serial.print(F(".")); Serial.println(minor);
    version = major; version <<= 8; version |= minor;
  }
  #endif
  return version;
}





// Tries to read the 6-byte MAC address of the CC3000 module
void displayMACAddress(void)
{
  uint8_t macAddress[6];
  
  if(!cc3000.getMacAddress(macAddress))
  {
    Serial.println(F("Unable to retrieve MAC Address!\r\n"));
  }
  else
  {
    Serial.print(F("MAC Address : "));
    cc3000.printHex((byte*)&macAddress, 6);
  }
}



void printInitState(unsigned char a, unsigned char b, unsigned char c, unsigned char d)
{
  displayRainbow();
  printTemp();
  panel.drawChar(3, 9, a, 0);
  panel.drawChar(10, 9, b, 0);
  panel.drawChar(17, 9, c, 0);
  panel.drawChar(24, 9, d, 0);
  panel.refresh();
}
