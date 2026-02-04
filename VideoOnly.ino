#include <AmebaFatFS.h>
#include <inttypes.h>
#include <MP4Recording.h>
#include <rtc.h>
#include <SPI.h>
#include <stdio.h>
#include <StreamIO.h>
#include <time.h>
#include <VideoStream.h>
#include <Wire.h>

#include  "VideoOnly.h"

#define CHANNEL 0
#define USE_H264  1

#ifdef USE_H264
// Default preset configurations for each video channel:
// Channel 0 : 1920 x 1080 30FPS H264
// Channel 1 : 1280 x 720  30FPS H264
VideoSetting config(CHANNEL);
#else
// Alternate channel config for 1920x1080 30FPS and H265 compression
VideoSetting config(1920, 1080, 30, VIDEO_HEVC, 0);
#endif

MP4Recording mp4;
StreamIO videoStreamer(1, 1);    // 1 Input Video -> 1 Output RTSP
AmebaFatFS  fileSystem;

uint64_t seconds;

void setup()
{
  // Initialize the hardware
    Serial.begin(115200);    
    rtc.Init();
    Wire.begin();

    setRTCTime();    

    fileSystem.begin();

    // Configure camera video channel with video format information
    Camera.configVideoChannel(CHANNEL, config);
    Camera.videoInit();

    // Configure MP4 with identical video format information
    // Configure MP4 recording settings
    mp4.configVideo(config);
    mp4.setRecordingDuration(maxDuration);
    mp4.setRecordingDataType(STORAGE_VIDEO);    // Set MP4 to record video only

    // Configure StreamIO object to stream data from video channel to MP4 recording
    videoStreamer.registerInput(Camera.getStream(CHANNEL));
    videoStreamer.registerOutput(mp4);
    
    if (videoStreamer.begin() != 0) 
    {
        Serial.println("StreamIO link start failed");
    }

    // Start data stream from video channel
    Camera.channelBegin(CHANNEL);

    pinMode(LED_G, OUTPUT);    
}

void loop()
{
  uint16_t temp;
  char     fileName[128]; // Maximum file name lenght allowed by file system
  static bool   setModified = false;
  static bool   cleanupRequired = false;
  static uint64_t videoStartTime;
  static uint16_t minutes = 0;
  static String   oldFileName;
  static TimeStamp   timeStamp;

  seconds = rtc.Read();

  digitalWrite(LED_G, (seconds & 0x01));

  // Check if a video is currently being recorded.
  if (mp4.getRecordingState() == false)
  {
    oldFileName = mp4.getRecordingFileName().c_str();

    if (getNextVideoFileName(fileName))
    {
      Serial.print("Starting recording video file: ");
      Serial.println(fileName);
      
      mp4.setRecordingFileName(fileName);
      mp4.begin();

      getCurrentTimestamp(timeStamp);

      videoStartTime = seconds;
      minutes = 0;
      setModified = true;
      cleanupRequired = true;
    }    
  }
  else
  {
      temp = (seconds - videoStartTime);

      if (setModified &&  temp >= 5)
      {
        printf("Setting time stamp for %s\n", oldFileName.c_str());
        setFileTimeStamp(oldFileName.c_str(), timeStamp);
        setModified = false;
      }

      if (cleanupRequired && (seconds - videoStartTime) > 10)
      {
        printf("Cleaning up old files\n");
        cleanupOldFiles(mp4.getRecordingFileName());
        cleanupRequired = false;
      }
      
      // Convert seconds to minutes.
      temp /= 60;

      if (temp >= 1 && temp > minutes)
      {
        minutes = temp;
        Serial.print("Recording length = ");
        Serial.print(minutes, DEC);
        Serial.println(" minutes.");
      }
  }
}

void getCurrentTimestamp(TimeStamp& timeStamp)
{
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;

  readDS3231time(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);

  timeStamp.second = second;
  timeStamp.minute = minute;
  timeStamp.hour = hour;
  timeStamp.date = dayOfMonth;
  timeStamp.month = month;
  timeStamp.year = year;
}

bool getNextVideoFileName(char* fileName)
{
  TimeStamp currTime;

  getCurrentTimestamp(currTime);

  sprintf(fileName, "%02d%02d%02d-%02d%02d%02d", currTime.year, currTime.month, currTime.date, currTime.hour, currTime.minute, currTime.second);

  return true;
}

// Note, this Function works if we are starting from < the max allowed, and
// checking each time a new file is created.  
void cleanupOldFiles(const String& mp4FileName)
{
  char    nameBuffer[2000];
  char    buffer[128];
  char*   temp;
  String  nameStr;
  String  currentFile;
  int     fileCount = 0;

  sprintf(buffer, "%s%s.mp4", fileSystem.getRootPath(), mp4FileName.c_str());
  currentFile = buffer;

  fileSystem.readDir(fileSystem.getRootPath(), nameBuffer, sizeof(nameBuffer));

  // First, determine how many .mp4 files there are.
  for (temp = nameBuffer; strlen(temp) > 0; temp += strlen(temp) + 1)
  {
    nameStr = temp;
    nameStr.toLowerCase();
    
    if(nameStr.endsWith(".mp4"))
    {
      fileCount++;
    }
  }

  printf("Found %d mp4 files\n", fileCount);

  // Return if there are less than the maximum allowed.
  if (fileCount <= maxFileCount)
  {
    // Just return because we don't have to remove the oldest file...
    return;
  }

  // Find the oldest file and delete it
  uint16_t  second, minute, hour, day, month, year;  
  uint64_t  oldestFileTime = INT64_MAX;
  uint64_t  fileModTime;
  String    oldestFileName;

  for (temp = nameBuffer; strlen(temp) > 0; temp += strlen(temp) + 1)
  {
    sprintf(buffer, "%s%s", fileSystem.getRootPath(), temp);

    if (currentFile == buffer)
    {
      continue;
    }
    
    fileSystem.getLastModTime(buffer, &year, &month, &day, &hour, &minute, &second);

    fileModTime = rtc.SetEpoch(year, month, day, hour, minute, second);

    if (fileModTime < oldestFileTime)
    {
      oldestFileTime = fileModTime;
      oldestFileName = buffer;
    }
  }

  printf("%s is the oldest file\n", oldestFileName.c_str());

  if(oldestFileName.length() && fileSystem.exists(oldestFileName))
  {
    printf("Removing oldest file \n");

    fileSystem.remove(oldestFileName);
  }
}

void setFileTimeStamp(const char* fileName, TimeStamp& timeStamp)
{
  char fullFileName[128];
  int   result;
  
  if (*fileName != 0)
  {
    sprintf(fullFileName, "%s%s.mp4", fileSystem.getRootPath(), fileName);

    if (fileSystem.exists(fullFileName))
    {
      Serial.print("Setting timestamp on file: ");
      Serial.println(fullFileName);
  
      printf("Time stamp:  %04d/%02d/%02d, %02d:%02d:%02d \n", timeStamp.year+2000, timeStamp.month, timeStamp.date, timeStamp.hour, timeStamp.minute, timeStamp.second);
      
      result = fileSystem.setLastModTime(fullFileName, timeStamp.year+2000, timeStamp.month, timeStamp.date, timeStamp.hour, timeStamp.minute, timeStamp.second);
  
      if ( result !=  0)
      {
        printf("setLastModTime returned result: %d \n", result);
      }
    }
  }
}

void setRTCTime()
{
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year = 0;

  // retrieve data from DS3231
  while (year == 0)
  {
    readDS3231time(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);

    if (year == 0)
    {
      delay(100);
    }      
  }

  long long epochTime = rtc.SetEpoch(year + 2000, month, dayOfMonth, hour, minute, second);
  rtc.Write(epochTime);
}

void readDS3231time(byte *second,
                    byte *minute,
                    byte *hour,
                    byte *dayOfWeek,
                    byte *dayOfMonth,
                    byte *month,
                    byte *year)
{
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0); // set DS3231 register pointer to 00h
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 7);
  // request seven bytes of data from DS3231 starting from register 00h
  *second = bcdToDec(Wire.read() & 0x7f);
  *minute = bcdToDec(Wire.read());
  *hour = bcdToDec(Wire.read() & 0x3f);
  *dayOfWeek = bcdToDec(Wire.read());
  *dayOfMonth = bcdToDec(Wire.read());
  *month = bcdToDec(Wire.read());
  *year = bcdToDec(Wire.read());
}

// Convert binary coded decimal to normal decimal numbers
byte bcdToDec(byte val)
{
  return( (val/16*10) + (val%16) );
}

void printInfo(void)
{
    Serial.println("------------------------------");
    Serial.println("- Summary of Streaming -");
    Serial.println("------------------------------");
    Camera.printInfo();
    Serial.println("- MP4 Recording Information -");
    mp4.printInfo();
}
