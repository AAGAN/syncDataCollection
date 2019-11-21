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
  }
  else
  {
    Serial.println("error opening datalog.txt");
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
  for (int i = 0; i<10;i++)
  {
    p0 += analogRead(A0);
    delay(2);
    p1 += analogRead(A1);
    delay(2);
    p2 += analogRead(A2);
    delay(2);
  }
  p0 /= 10;
  p1 /= 10;
  p2 /= 10;
  delay(250);
  sendData(p0);
  delay(250);
  sendData(p1);
  delay(250);  
  sendData(p2);
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

void setup()
{
  // set the Time library to use Teensy 3.0's RTC to keep time
  setSyncProvider(getTeensy3Time);
  Serial.begin(115200);
  Serial1.begin(115200);
  analogReadResolution(12);
  pinMode(A0, INPUT);
  pinMode(A1, INPUT);
  pinMode(A2, INPUT);
  xbee.setSerial(Serial1);
  delay(100);
  Serial.print("Initializing SD card...");
  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect))
  {
    sdSuccessSwitch = false;
    return;
  }
  Serial.println("card initialized.");
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
      if(receivedTime>1000000)//then this must be the unix time
      {
        Teensy3Clock.set(receivedTime);
        setTime(receivedTime);
        Serial.println(receivedTime);
        previousHour = hour(receivedTime);
        sprintf(filename, "%02d%02d%02d%02d.CSV", day(receivedTime), hour(receivedTime), minute(receivedTime), second(receivedTime));
        Serial.println(filename);
        writeSwitch = true;
        sendPressureSwitch = true;
        flushAPI();
      }
      else if (receivedTime == 0)
      {
        writeSwitch = false;
      }
    } 
  }
  if (millis() - recordingMillis > 100 && writeSwitch)
  {
    writeData();
    recordingMillis = millis();
    Serial.println(numReadings);
    numReadings = 0;
    p0 = 0;
    p1 = 0;
    p2 = 0;
  }
  else if (writeSwitch && millis()-averagingMillis>5)
  {
    p0 += analogRead(A0);
    p1 += analogRead(A1);
    p2 += analogRead(A2);
    numReadings++;
    averagingMillis = millis();
  }
  if (sendPressureSwitch)
  {
    sendSetTimeAndPressure();
    sendPressureSwitch = false;
  }
  if (millis() - previousHourMillis > 50000)
  {
    previousHourMillis = millis();
    time_t curt = Teensy3Clock.get();
    if (hour(curt) != previousHour)
    {
      sprintf(filename, "%02d%02d%02d%02d.CSV", day(curt), hour(curt), minute(curt), second(curt));
      previousHour = hour(curt);
    }
  }
}
