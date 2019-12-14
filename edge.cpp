#include <Arduino.h>
#include <XBee.h>
#include <Timelib.h>
#include <SD.h>
#include <SPI.h>

XBee xbee;
uint8_t payload[] = {0, 1, 2, 3};
char filename[13] = "ddhhmmss.csv";
TxStatusResponse txStatus;
const int chipSelect = BUILTIN_SDCARD;
bool sdSuccessSwitch = true;
uint32_t recordingMillis = millis();
uint32_t averagingMillis = millis();
uint32_t previousHourMillis = millis();
bool writeSwitch = false;
bool sendPressureSwitch = false;
uint32_t p0 = 0;
uint32_t p1 = 0;
uint32_t p2 = 0;
int numReadings = 0;
int previousHour = 0;
bool debug = false;

float convertToPressure(uint32_t rawVal)
{
  float pressure;
  pressure = (float)rawVal - 406.0;
  pressure *= 0.092336;
  if (pressure < -5)
    pressure = 0.0;
  return pressure;
}

void serialPrintPressure()
{
  if(debug){
    Serial.print(convertToPressure(p0 / numReadings));
    Serial.print(" , ");
    Serial.print(convertToPressure(p1 / numReadings));
    Serial.print(" , ");
    Serial.println(convertToPressure(p2 / numReadings));
  }
}

void addToPayload(uint32_t value)
{
  payload[0] = (uint8_t)((value & 0xFF000000) >> 24);
  payload[1] = (uint8_t)((value & 0x00FF0000) >> 16);
  payload[2] = (uint8_t)((value & 0x0000FF00) >> 8);
  payload[3] = (uint8_t)(value & 0x000000FF);
}

uint32_t decodePayload(uint8_t data[4])
{
  return ((uint32_t)(data[0])<<24)+
         ((uint32_t)(data[1])<<16)+
         ((uint32_t)(data[2])<<8)+
         ((uint32_t)(data[3]));
}

time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

void writeData()
{
  time_t t = Teensy3Clock.get();
  // make a string for assembling the data to log:
  String dataString = "";
  dataString += String(t);
  dataString += ".";
  dataString += String(millis()%1000);
  dataString += " , ";
  dataString += String(p0/numReadings);
  dataString += " , ";
  dataString += String(p1/numReadings);
  dataString += " , ";
  dataString += String(p2/numReadings);

  File dataFile = SD.open(filename, FILE_WRITE);
  // if the file is available, write to it:
  if (dataFile)
  {
    dataFile.println(dataString);
    dataFile.close();
    if (debug){
      serialPrintPressure();
    }
  }
  else
  {
    if(debug){
      Serial.print("error opening the log file: ");//turn a red led on instead
      Serial.println(filename);
    }
    delay(10000);
  }
}

int sendData(u_int32_t t)
{
  uint16_t coordinatorAddress = 0x0000;
  addToPayload(t);
  Tx16Request tx(coordinatorAddress, payload, sizeof(payload));
  xbee.send(tx);

  return 0;
}

//sends the set time from Teensy and reads an average pressure readings and sends it for confirmation
void sendSetTimeAndPressure()
{
  time_t t = Teensy3Clock.get();
  sendData(t);
  p0 = 0;
  p1 = 0;
  p2 = 0;
  for (int i = 0; i<20;i++)
  {
    p0 += analogRead(A0);
    delay(2);// 1 may be better?
    p1 += analogRead(A1);
    delay(2);
    p2 += analogRead(A2);
    delay(2);
  }
  p0 /= 20;
  p1 /= 20;
  p2 /= 20;
  delay(250);//150 may be better?
  sendData(p0);
  delay(250);
  sendData(p1);
  delay(250);  
  sendData(p2);
  delay(250);
  if (sdSuccessSwitch)
  {
    sendData(1);
  }
  else
  {
    sendData(0);
  }
  
  p0 = 0;
  p1 = 0;
  p2 = 0;
 //delay(150);
 //flushAPI();//to clear the acknowledgements?
}

void flushAPI()
{
  //XBeeResponse discard;
  xbee.readPacket();
  while(xbee.getResponse().isAvailable())
  {
    if(debug){
      Serial.println(xbee.getResponse().getApiId());
    }
    xbee.readPacket();
    //xbee.getResponse(discard);
  }
}

void setup()
{
  // set the Time library to use Teensy 3.0's RTC to keep time
  // while(!Serial)
  //   ;
  setSyncProvider(getTeensy3Time);
  setSyncInterval(10);
  if(debug){
    Serial.begin(115200);
  }
  Serial1.begin(115200);
  //delay(5);
  analogReadResolution(12);
  pinMode(A0, INPUT);
  pinMode(A1, INPUT);
  pinMode(A2, INPUT);
  xbee.setSerial(Serial1);
  delay(15000);
  if(debug){
    Serial.print("Initializing SD card...");
  }
  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect))
  {
    sdSuccessSwitch = false;
    if(debug){
      Serial.println("card initialization failed.");
    }
  }
  else
  {
    sdSuccessSwitch = true;
    if(debug){
    Serial.println("card initialized.");
    }
  }
  
  flushAPI();
}

void loop()
{
  xbee.readPacket();
  if (xbee.getResponse().isAvailable())
  {
    Rx16Response resp;
    // should be a znet tx status
    if (xbee.getResponse().getApiId() == RX_16_RESPONSE)
    {
      xbee.getResponse().getRx16Response(resp);
      uint8_t frameData[] = {resp.getData(0),resp.getData(1),resp.getData(2),resp.getData(3)};
      uint32_t receivedTime = decodePayload(frameData);
      if(debug){
        Serial.print(receivedTime);
      }
      if(receivedTime>1000000)//then this must be the unix time
      {
        Teensy3Clock.set(receivedTime);
        setTime(receivedTime);
        if(debug){
          Serial.println(receivedTime);
        } 
        previousHour = hour(receivedTime);
        sprintf(filename, "%02d%02d%02d%02d.CSV", day(receivedTime), hour(receivedTime), minute(receivedTime), second(receivedTime));
        if(debug){
          Serial.println(filename);
        }
        writeSwitch = true;
        sendPressureSwitch = true;
        flushAPI();
      }
      else if (receivedTime == 0)
      {
        writeSwitch = false;
        //flushAPI();
      }
    } 
  }
  if (millis() - recordingMillis > 100 && writeSwitch)
  {
    writeData();
    recordingMillis = millis();
    //Serial.println(numReadings);
    numReadings = 0;//initialize after writing to file
    p0 = 0;//initialize after writing to file
    p1 = 0;//initialize after writing to file
    p2 = 0;//initialize after writing to file
  }
  else if (writeSwitch && millis()-averagingMillis>5)//averaging the readings
  {
    p0 += analogRead(A0);
    p1 += analogRead(A1);
    p2 += analogRead(A2);
    numReadings++;
    averagingMillis = millis();
  }
  if (sendPressureSwitch)//everytime time is received, send the set time and pressures
  {
    sendSetTimeAndPressure();
    sendPressureSwitch = false;
  }
  // if (millis() - previousHourMillis > 3600000)//change the name of the file every one hour
  // {
  //   previousHourMillis = millis();
  //   time_t curt = Teensy3Clock.get();
  //   if (hour(curt) != previousHour)
  //   {
  //     sprintf(filename, "%02d%02d%02d%02d.CSV", day(curt), hour(curt), minute(curt), second(curt));
  //     previousHour = hour(curt);
  //   }
  // }
}
