#include <Arduino.h>
#include <Timelib.h>
#include <SPI.h>
#include <Adafruit_GFX.h> // Core graphics library
#include "Adafruit_HX8357.h"
#include "TouchScreen.h"
#include <XBee.h>

/*  code to process time sync messages from the serial port   */
#define TIME_HEADER "T" // Header tag for serial time sync message

// These are the four touchscreen analog pins
#define YP A9 // must be an analog pin, use "An" notation!
#define XM A8 // must be an analog pin, use "An" notation!
#define YM 5  // can be a digital pin
#define XP 4  // can be a digital pin

// This is calibration data for the raw touch data to the screen coordinates
#define TS_MINX 110
#define TS_MINY 80
#define TS_MAXX 900
#define TS_MAXY 940

#define MINPRESSURE 10
#define MAXPRESSURE 1000

#define Coordinator 0x0013a2004193f64b
#define EndDevice1 0x0013a2004192cdf3
#define EndDevice2 0x0013a2004195ce13
#define EndDevice3 0x0013a2004192dc03

// The display uses hardware SPI, plus #9 & #10
#define TFT_RST -1 // dont use a reset pin, tie to arduino RST if you like
#define TFT_DC 2
#define TFT_CS 7

Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, TFT_RST);

// For better pressure precision, we need to know the resistance
// between X+ and X- Use any multimeter to read it
// For the one we're using, its 300 ohms across the X plate
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 321.1);

XBee xbee = XBee();
TxStatusResponse txStatus = TxStatusResponse();
uint32_t oldmillis = millis();
uint16_t buttonHeight = 96; //480 / 5;
uint16_t buttonWidth = 160; //320 / 2;
uint8_t clearScreen = 0;

void printDigits(uint32_t digits)
{
  // utility function for digital clock display: prints preceding colon and leading 0
  tft.print(":");
  if (digits < 10)
    Serial.print('0');
  tft.print(digits);
}

void digitalClockDisplay(uint32_t tt)
{
  // digital clock display of the time
  tft.print(hour(tt));
  printDigits(minute(tt));
  printDigits(second(tt));
  tft.print(" ");
  tft.print(day(tt));
  tft.print(" ");
  tft.print(month(tt));
  tft.print(" ");
  tft.print(year(tt));
  tft.println();
  tft.println();
  //tft.println(Teensy3Clock.get());
}

class nodes
{
  public:
  //class member variables
  uint16_t addr16;
  uint16_t cornerX;
  uint16_t cornerY;
  uint16_t deltaX;
  uint16_t deltaY;
  uint8_t dataPackage[4] = {0,0,0,0};
  uint8_t name;
  uint16_t color;
  uint32_t p[3] = {0,0,0};
  uint32_t timeSetOnUnit = 0;
    
  nodes();

  nodes(uint8_t nameOfTheUnit,uint16_t address16bit,uint16_t upperCornerX,uint16_t upperCornerY,uint16_t deltaXWidth,uint16_t deltaYHeight,uint16_t buttonColor){//constructor
          name = nameOfTheUnit;
          addr16 = address16bit;
          cornerX = upperCornerX;
          cornerY = upperCornerY;
          deltaX = deltaXWidth;
          deltaY = deltaYHeight;
          color = buttonColor;
  }
  
  void addToPayload(uint32_t value){//adds a 32 bit value to the payload to be sent to the unit
    dataPackage[0] = (uint8_t)((value & 0xFF000000) >> 24);
    dataPackage[1] = (uint8_t)((value & 0x00FF0000) >> 16);
    dataPackage[2] = (uint8_t)((value & 0x0000FF00) >> 8);
    dataPackage[3] = (uint8_t)(value & 0x000000FF);
  }

  uint32_t decodePayload(uint8_t data[4])
  {
    return ((uint32_t)(data[0])<<24)+((uint32_t)(data[1])<<16)+((uint32_t)(data[2])<<8)+((uint32_t)(data[3]));
  }

  uint32_t getCurrentTime(){//returns the unix time on the next second change
    uint32_t initialTime = Teensy3Clock.get();
    while (Teensy3Clock.get() == initialTime); //wait until the clock changes to the next second
    return Teensy3Clock.get();
  }

  bool checkTimeOnUnit(){//will check if the time set on this unit is within accepted delta of the coordinator
    bool timeSetCorrectly = false;
    for (int i = 0; i<4; i++)//time,p[0],p[1],p[2]
    {  
      if (xbee.readPacket(1000))
      {
        Rx16Response resp;
        if (xbee.getResponse().getApiId() == RX_16_RESPONSE)
        {
          xbee.getResponse().getRx16Response(resp);
          uint8_t frameData[] = {resp.getData(0),resp.getData(1),resp.getData(2),resp.getData(3)};
          uint32_t receivedTime = decodePayload(frameData);
          if(receivedTime>10000000)//then this must be the unix time
          {
            uint32_t deltaT = Teensy3Clock.get() - receivedTime;
            tft.print("deltaT between unit and coordinator = ");
            tft.println(deltaT);
            if (deltaT == 0)
            {
              timeSetCorrectly = true;
            }
            timeSetOnUnit = receivedTime;
          }
          else //must be pressure data
          {
            p[i-1] = receivedTime;
          }
        } 
      }
    }
    flushAPI();
    return timeSetCorrectly;
  }

  void flushAPI()
  {
    //XBeeResponse discard;
    xbee.readPacket();
    while(xbee.getResponse().isAvailable())
    {
      Serial.println(xbee.getResponse().getApiId());
      xbee.readPacket();
      //xbee.getResponse(discard);
    }
  }

  int updateTime(){//updates the time on this unit and returns 0 if successful and 1 if not successful
    bool updateTimeSuccess = false;
    uint8_t numTries = 0;
    tft.fillScreen(HX8357_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(HX8357_WHITE);
    tft.setTextSize(2);
    tft.print("Updating Unit ");
    tft.println(name);
    tft.setTextSize(1);
    while(!updateTimeSuccess && numTries < 5)
    {
      numTries++;
      flushAPI();
      uint32_t ttime = getCurrentTime();
      addToPayload(ttime);
      Tx16Request tx = Tx16Request(addr16, dataPackage, sizeof(dataPackage));
      uint32_t startOfTransmission = millis();
      xbee.send(tx);
      if (xbee.readPacket(1000))
      {
        // got a response!
        // should be a znet tx status
        if (xbee.getResponse().getApiId() == TX_STATUS_RESPONSE)
        {
          xbee.getResponse().getTxStatusResponse(txStatus);
          // get the delivery status, the fifth byte
          if (txStatus.getStatus() == SUCCESS)
          {
            // success.  time to celebrate
            uint32_t deltaT = millis() - startOfTransmission;
            if(deltaT < 20)
            {
              if (checkTimeOnUnit())//check to see if the time set on the unit is almost the same as the time on the coordinator
              {
                updateTimeSuccess = true;
                tft.println("Successfully updated the time on this unit");
                tft.print("updateTimeSuccess = ");
                tft.println(deltaT);
                tft.print("time is updated to : ");
                tft.println(ttime);
                digitalClockDisplay(ttime);
                color = HX8357_GREEN;
                delay(10000);
              }
              else
              {
                tft.println("got the acknowledgement in time but the time is not set on the remote unit!");
                tft.print("number of retries: ");
                tft.println(numTries);
                delay(5000);
                color = HX8357_RED;
              }
            }
            else
            {
              tft.print("received the response, delay (millisecond): ");
              tft.println(deltaT);
              tft.println("Attemped to update the time but the response time was too long. will retry. get closer to the unit.");
              tft.print("number of attempts: ");
              tft.println(numTries);
              delay(10000);
              color = HX8357_RED;
              flushAPI();
            }
          }
          else
          {
            tft.println("the remote unit did not receive our packet. is it powered on?");
            tft.println("If it is turned on, Try moving closer to the unit");
            tft.println();
            delay(5000);
            color = HX8357_RED;
            flushAPI();
          }
        }
      } 
      else if (xbee.getResponse().isError())
      {
        tft.print("Error reading the ACK packet. Error Code:");
        tft.println(xbee.getResponse().getErrorCode());
        delay(5000);
        color = HX8357_RED;
        flushAPI();
      }
      else
      {
        tft.println("local XBee did not provide a timely TX Status Response.  Radio is not configured properly or connected");
        delay(5000);
        color = HX8357_RED;
        flushAPI();
      }
    }
    if (updateTimeSuccess == false){
      return 1;
    }
    else
    {
      return 0;
    }
    
  }

  bool checkRecordingStatusOnUnit()
  {
    return true;
  }

  int stopRecording()
  {//sends a signal to the unit to stop recording
    bool stopMessageSuccess = false;
    uint8_t numTries = 0;
    tft.fillScreen(HX8357_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(HX8357_WHITE);
    tft.setTextSize(2);
    tft.print("Stopping Unit ");
    tft.println(name);
    tft.setTextSize(1);
    while(!stopMessageSuccess && numTries < 5)
    {
      numTries++;
      addToPayload(0x0000);
      Tx16Request tx = Tx16Request(addr16, dataPackage, sizeof(dataPackage));
      flushAPI();
      uint32_t startOfTransmission = millis();
      xbee.send(tx);
      if (xbee.readPacket(1000))
      {
        // got a response!
        // should be a znet tx status
        if (xbee.getResponse().getApiId() == TX_STATUS_RESPONSE)
        {
          xbee.getResponse().getTxStatusResponse(txStatus);
          // get the delivery status, the fifth byte
          if (txStatus.getStatus() == SUCCESS)
          {
            // success.  time to celebrate
            uint32_t deltaT = millis() - startOfTransmission;
              if (checkRecordingStatusOnUnit())//check to see if the time set on the unit is almost the same as the time on the coordinator
              {
                stopMessageSuccess = true;
                tft.println("Successfully stopped recording on this unit");
                tft.print("round message duration = ");
                tft.println(deltaT);
                color = HX8357_CYAN;
                delay(10000);
              }
              else
              {
                tft.println("got the acknowledgement in time but the unit didn't acknowledge it!");
                tft.print("number of retries: ");
                tft.println(numTries);
                delay(5000);
                color = HX8357_RED;
              }
          }
          else
          {
            tft.println("the remote unit did not receive our packet. is it powered on?");
            tft.println("If it is turned on, Try moving closer to the unit");
            tft.println();
            delay(5000);
            color = HX8357_RED;
          }
        }
      } 
      else if (xbee.getResponse().isError())
      {
        tft.print("Error reading the ACK packet. Error Code:");
        tft.println(xbee.getResponse().getErrorCode());
        delay(5000);
        color = HX8357_RED;
      }
      else
      {
        tft.println("local XBee did not provide a timely TX Status Response.  Radio is not configured properly or connected");
        delay(5000);
        color = HX8357_RED;
      }
    }
    if (stopMessageSuccess == false){
      return 1;
    }
    else
    {
      return 0;
    }
  }

  void drawButton()
  { 
    int margin = 10;
    int gap = 5;
    int height3 = 20;
    int height1 = 10;
    tft.fillRoundRect(cornerX+margin, cornerY+margin,deltaX-margin,deltaY-margin,10,color);
    tft.setCursor(cornerX+deltaX/2-gap,cornerY+ margin + gap);
    tft.setTextColor(HX8357_BLACK);
    tft.setTextSize(3);
    tft.println(name);
    tft.setCursor(cornerX+margin+gap,cornerY+2* margin+height3);
    tft.setTextSize(1);
    tft.print("P0 = ");
    tft.println(p[0]);
    tft.setCursor(cornerX+margin+gap,cornerY+2 * margin+height3+height1);
    tft.print("P1 = ");
    tft.println(p[1]);
    tft.setCursor(cornerX+margin+gap,cornerY+2 * margin+height3+2 * height1);
    tft.print("P2 = ");
    tft.println(p[2]);
    tft.setCursor(cornerX+margin+gap,cornerY+2 * margin+height3+3 * height1);
    tft.print("t: ");
    digitalClockDisplay(timeSetOnUnit);
  }
};

time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

unsigned long processSyncMessage()
{
  unsigned long pctime = 0L;
  const unsigned long DEFAULT_TIME = 1357041600; // Jan 1 2013

  if (Serial.find(TIME_HEADER))
  {
    pctime = Serial.parseInt();
    return pctime;
    if (pctime < DEFAULT_TIME)
    {              // check the value is a valid time (greater than Jan 1 2013)
      pctime = 0L; // return 0 to indicate that the time is not valid
    }
  }
  return pctime;
}

nodes unit[] = 
{
nodes(0, 0x00E0, 0          , 0               , buttonWidth, buttonHeight, HX8357_BLUE),
nodes(1, 0x00E1, buttonWidth, 0               , buttonWidth, buttonHeight, HX8357_BLUE),
nodes(2, 0x00E2, 0          ,     buttonHeight, buttonWidth, buttonHeight, HX8357_BLUE),
nodes(3, 0x00E3, buttonWidth,     buttonHeight, buttonWidth, buttonHeight, HX8357_BLUE),
nodes(4, 0x00E4, 0          , 2 * buttonHeight, buttonWidth, buttonHeight, HX8357_BLUE),
nodes(5, 0x00E5, buttonWidth, 2 * buttonHeight, buttonWidth, buttonHeight, HX8357_BLUE),
nodes(6, 0x00E6, 0          , 3 * buttonHeight, buttonWidth, buttonHeight, HX8357_BLUE),
nodes(7, 0x00E7, buttonWidth, 3 * buttonHeight, buttonWidth, buttonHeight, HX8357_BLUE),
nodes(8, 0x00E8, 0          , 4 * buttonHeight, buttonWidth, buttonHeight, HX8357_BLUE),
nodes(9, 0x00E9, buttonWidth, 4 * buttonHeight, buttonWidth, buttonHeight, HX8357_BLUE)
};

void drawUnits()
{
  tft.fillScreen(HX8357_BLACK);
  for (int i = 0; i<10;i++)
  {
    unit[i].drawButton();
  }
}


void setup()
{
  
  // set the Time library to use Teensy 3.0's RTC to keep time
  setSyncProvider(getTeensy3Time);
  //Serial.begin(115200);
  Serial1.begin(115200);

  xbee.setSerial(Serial1);
  tft.begin();
  tft.fillScreen(HX8357_BLACK);
  delay(100);
  if (timeStatus() != timeSet)
  {
    tft.println("Unable to sync with the RTC");
  }
  else
  {
    tft.println("RTC has set the system time");
  }

drawUnits();
}

void loop()
{
  if (Serial.available()) {
    time_t t = processSyncMessage();
    if (t != 0) {
      Teensy3Clock.set(t); // set the RTC
      setTime(t);
    }
    digitalClockDisplay(t); 
  }
  
  if(millis() - oldmillis > 2000){
    oldmillis = millis();
    // Retrieve a point
    TSPoint p = ts.getPoint();
    p.x = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
    p.y = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());
    if (p.z > MINPRESSURE && p.z < MAXPRESSURE && p.x>0)
    {
      for(int i = 0; i <10 ; i++)
      {
        if(unit[i].cornerX<p.x && unit[i].cornerY<p.y && unit[i].cornerX+buttonWidth>p.x && unit[i].cornerY+buttonHeight>p.y)
        {
          if(unit[i].color != HX8357_GREEN) // it is either not initialized or we didn't get the response that it is recording
          {  
            unit[i].updateTime();
          }
          else //it is recording (it is green)
          {
            unit[i].stopRecording();
          }
          delay(500);
        }
      }
      drawUnits();
    } 
  }
}
