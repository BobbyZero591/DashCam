#ifndef __VIDEO_ONLY__
#define __VIDEO_ONLY__

#include <inttypes.h>
#include <wiring_constants.h>

#define DS3231_I2C_ADDRESS 0x68

const uint32_t  maxDuration =  120; // Maximum duration of a recording file
const int       maxFileCount = 100; // Maximum number of files (total recording time in hours = (maxDuration x maxFileCount) / 3600)

//-------------------------------------------------------//
// Function Primatives
bool getNextVideoFileName(char* fileName);
void loop();
byte bcdToDec(byte val);
void setRTCTime();
void setTimeStamp(const char* fileName);
void cleanupOldFiles(const String& mp4FileName);
bool getNextVideoFileName(char* fileName);
void generateCurrentFileName(char* fileName);
void readDS3231time(byte *second,
                    byte *minute,
                    byte *hour,
                    byte *dayOfWeek,
                    byte *dayOfMonth,
                    byte *month,
                    byte *year);


//-------------------------------------------------------//
// Data structures
typedef struct
{
      uint16_t year;
      uint16_t month;
      uint16_t date;
      uint16_t hour;
      uint16_t minute;
      uint16_t second;
}TimeStamp;


#endif