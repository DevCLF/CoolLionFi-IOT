/***************************************************
  This is an example for our Adafruit FONA Cellular Module

  Designed specifically to work with the Adafruit FONA
  ----> http://www.adafruit.com/products/1946
  ----> http://www.adafruit.com/products/1963
  ----> http://www.adafruit.com/products/2468
  ----> http://www.adafruit.com/products/2542

  These cellular modules use TTL Serial to communicate, 2 pins are
  required to interface
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  BSD license, all text above must be included in any redistribution
 ****************************************************/

/*
THIS CODE IS STILL IN PROGRESS!

Open up the serial console on the Arduino at 115200 baud to interact with FONA


This code will receive an SMS, identify the sender's phone number, and automatically send a response

*/

#include "Adafruit_FONA.h"

#define FONA_RST 4

// this is a large buffer for replies
char replybuffer[255];

#if (defined(__AVR__) || defined(ESP8266)) && !defined(__AVR_ATmega2560__)
// For UNO and others without hardware serial,
// we default to using software serial. If you want to use hardware serial
// (because softserial isnt supported) comment out the following three lines
// and uncomment the HardwareSerial line
#include <SoftwareSerial.h>

#define FONA_RX 2
#define FONA_TX 3
#define LOCK_PIN 4

SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;

#else
// On Leonardo/M0/etc, others with hardware serial, use hardware serial!
HardwareSerial *fonaSerial = &Serial1;

#endif

// Use this for FONA 800 and 808s
Adafruit_FONA fona = Adafruit_FONA(FONA_RST);
// Use this one for FONA 3G
//Adafruit_FONA_3G fona = Adafruit_FONA_3G(FONA_RST);

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);

void setup()
{
  // set up Lock pins, but turn them off
  pinMode(LOCK_PIN, OUTPUT);
  digitalWrite(LOCK_PIN, LOW);
  delay(15000);
  digitalWrite(LOCK_PIN, HIGH);

  while (!Serial)
    ;

  Serial.begin(115200);
  Serial.println(F("FONA SMS caller ID test"));
  Serial.println(F("Initializing....(May take 3 seconds)"));

  // make it slow so its easy to read!
  fonaSerial->begin(4800);
  if (!fona.begin(*fonaSerial))
  {
    Serial.println(F("Couldn't find FONA"));
    while (1)
      ;
  }
  Serial.println(F("FONA is OK"));

  // Try to enable GPRS
  Serial.println(F("Enabling GPS..."));
  fona.enableGPS(true);

  // Print SIM card IMEI number.
  char imei[16] = {0}; // MUST use a 16 character buffer for IMEI!
  uint8_t imeiLen = fona.getIMEI(imei);
  if (imeiLen > 0)
  {
    Serial.print("SIM card IMEI: ");
    Serial.println(imei);
  }

  fonaSerial->print("AT+CNMI=2,1\r\n"); //set up the FONA to send a +CMTI notification when an SMS is received

  Serial.println("FONA Ready");
}

char fonaNotificationBuffer[64]; //for notifications from the FONA
char smsBuffer[250];
float latitude, longitude, speed_kph, heading, speed_mph, altitude;
char request[250];
char request_1[7] = "Info";
char request_2[7] = "Unlock";
char request_3[7] = "Lock";

void localization()
{
  // if you ask for an altitude reading, getGPS will return false if there isn't a 3D fix
  boolean gps_success = fona.getGPS(&latitude, &longitude, &speed_kph, &heading, &altitude);

  if (gps_success)
  {
    Serial.print("GPS lat:");
    Serial.println(latitude, 6);
    Serial.print("GPS long:");
    Serial.println(longitude, 6);
    Serial.print("GPS speed KPH:");
    Serial.println(speed_kph);
    Serial.print("GPS speed MPH:");
    speed_mph = speed_kph * 0.621371192;
    Serial.println(speed_mph);
    Serial.print("GPS heading:");
    Serial.println(heading);
    Serial.print("GPS altitude:");
    Serial.println(altitude);
  }
  else
  {
    Serial.println("Waiting for FONA GPS 3D fix...");
  }

  // // Fona 3G doesnt have GPRSlocation :/
  // if ((fona.type() == FONA3G_A) || (fona.type() == FONA3G_E))
  //   return;
  // // Check for network, then GPRS
  // Serial.println(F("Checking for Cell network..."));
  // if (fona.getNetworkStatus() == 1)
  // {
  //   // network & GPRS? Great! Print out the GSM location to compare
  //   boolean gsmloc_success = fona.getGSMLoc(&latitude, &longitude);

  //   if (gsmloc_success)
  //   {
  //     Serial.print("GSMLoc lat:");
  //     Serial.println(latitude, 6);
  //     Serial.print("GSMLoc long:");
  //     Serial.println(longitude, 6);
  //   }
  //   else
  //   {
  //     Serial.println("GSM location failed...");
  //     Serial.println(F("Disabling GPRS"));
  //     fona.enableGPRS(false);
  //     Serial.println(F("Enabling GPRS"));
  //     if (!fona.enableGPRS(true))
  //     {
  //       Serial.println(F("Failed to turn GPRS on"));
  //     }
  //   }
  // }
}

void loop()
{

  char *bufPtr = fonaNotificationBuffer; //handy buffer pointer

  if (fona.available()) //any data available from the FONA?
  {
    int slot = 0; //this will be the slot number of the SMS
    int charCount = 0;
    //Read the notification into fonaInBuffer
    do
    {
      *bufPtr = fona.read();
      Serial.write(*bufPtr);
      delay(1);
    } while ((*bufPtr++ != '\n') && (fona.available()) && (++charCount < (sizeof(fonaNotificationBuffer) - 1)));

    //Add a terminal NULL to the notification string
    *bufPtr = 0;

    //Scan the notification string for an SMS received notification.
    //  If it's an SMS message, we'll get the slot number in 'slot'
    if (1 == sscanf(fonaNotificationBuffer, "+CMTI: " FONA_PREF_SMS_STORAGE ",%d", &slot))
    {
      Serial.print("slot: ");
      Serial.println(slot);

      char callerIDbuffer[32]; //we'll store the SMS sender number in here

      // Retrieve SMS sender address/phone number.
      if (!fona.getSMSSender(slot, callerIDbuffer, 31))
      {
        Serial.println("Didn't find SMS message in slot!");
      }
      Serial.print(F("FROM: "));
      Serial.println(callerIDbuffer);

      // Retrieve SMS value.
      uint16_t smslen;
      if (fona.readSMS(slot, smsBuffer, 250, &smslen))
      { // pass in buffer and max len!
        Serial.println(smsBuffer);
        smsBuffer == request;
      }
      // switch (request)
      // {
      // case "info":
      //   Serial.println("Message Info!");
      //   localization();
      //   //Send geolocation
      //   delay(2000);

      //   //Send back an automatic response
      //   Serial.println("Sending reponse...");
      //   char message[60];
      //   char message_latitude[7];
      //   char message_longitude[7];
      //   char message_speed[7];
      //   char message_altitude[7];

      //   strcpy(message, "Hey, I got your text! \nHere is you information. \nLatitude:");
      //   strcat(message, dtostrf(latitude, 5, 4, message_latitude));
      //   strcat(message, "\nLongitude:");
      //   strcat(message, dtostrf(longitude, 5, 4, message_longitude));
      //   strcat(message, "\nSpeed:");
      //   strcat(message, dtostrf(speed_kph, 1, 4, message_speed));
      //   strcat(message, "\nAltitude:");
      //   strcat(message, dtostrf(altitude, 1, 4, message_altitude));
      //   strcat(message, ".");
      //   if (!fona.sendSMS(callerIDbuffer, message))
      //   {
      //     Serial.println(F("Message Failed"));
      //   }
      //   else
      //   {
      //     Serial.println(F("Message Sent!"));
      //   }
      //   //request = "";
      //   break;

      // case "Unlock":
      //   Serial.println("Message Unlock");
      //   digitalWrite(LOCK_PIN, HIGH);
      //   if (!fona.sendSMS(callerIDbuffer, "Hey, I got your text. We have unlock your device!"))
      //   {
      //     Serial.println(F("Message Failed"));
      //   }
      //   else
      //   {
      //     Serial.println(F("Message Sent!"));
      //   }
      //   delay(1000);
      //   digitalWrite(LOCK_PIN, LOW);
      //   //request = "";
      //   break;

      // case "Lock":
      //   Serial.println("Message lock");
      //   digitalWrite(LOCK_PIN, LOW);
      //   if (!fona.sendSMS(callerIDbuffer, "Hey, I got your text. We have lock your device!"))
      //   {
      //     Serial.println(F("Message Failed"));
      //   }
      //   else
      //   {
      //     Serial.println(F("Message Sent!"));
      //   }
      //   //request = "";
      //   break;
      // default:
      //   Serial.println("Message Unknown");
      //   if (!fona.sendSMS(callerIDbuffer, "Hey, I got your text. But I cannot recogize the request!"))
      //   {
      //     Serial.println(F("Message Failed"));
      //   }
      //   else
      //   {
      //     Serial.println(F("Message Sent!"));
      //   }
      // }

      //Other way to do it
      if (strcmp(smsBuffer, request_1) == 0)
      {
        Serial.println("Message Info!");
        localization();
        //Send geolocation
        delay(2000);

        //Send back an automatic response
        Serial.println("Sending reponse...");
        char message[60];
        char message_latitude[7];
        char message_longitude[7];
        char message_speed[7];
        char message_altitude[7];

        strcpy(message, "Hey, I got your text! \nHere is you information. \nLatitude:");
        strcat(message, dtostrf(latitude, 5, 4, message_latitude));
        strcat(message, "\nLongitude:");
        strcat(message, dtostrf(longitude, 5, 4, message_longitude));
        strcat(message, "\nSpeed:");
        strcat(message, dtostrf(speed_kph, 1, 4, message_speed));
        strcat(message, "\nAltitude:");
        strcat(message, dtostrf(altitude, 1, 4, message_altitude));
        strcat(message, ".");
        if (!fona.sendSMS(callerIDbuffer, message))
        {
          Serial.println(F("Message Failed"));
        }
        else
        {
          Serial.println(F("Message Sent!"));
        }
      }
      else if (strcmp(smsBuffer, request_2) == 0)
      {
        Serial.println("Message unlock");
        digitalWrite(LOCK_PIN, LOW);
        if (!fona.sendSMS(callerIDbuffer, "Hey, I got your text. We have unlock your device!"))
        {
          Serial.println(F("Message Failed"));
        }
        else
        {
          Serial.println(F("Message Sent!"));
        }
        //delay(3000);
        //digitalWrite(LOCK_PIN, LOW);
      }
      else if (strcmp(smsBuffer, request_3) == 0)
      {
        Serial.println("Message lock");
        digitalWrite(LOCK_PIN, HIGH);
        if (!fona.sendSMS(callerIDbuffer, "Hey, I got your text. We have lock your device!"))
        {
          Serial.println(F("Message Failed"));
        }
        else
        {
          Serial.println(F("Message Sent!"));
        }
      }
      else
      {
        Serial.println("Message Unknown");
        if (!fona.sendSMS(callerIDbuffer, "Hey, I got your text. But I cannot recogize the request!"))
        {
          Serial.println(F("Message Failed"));
        }
        else
        {
          Serial.println(F("Message Sent!"));
        }
      }

      // delete the original msg after it is processed
      //   otherwise, we will fill up all the slots
      //   and then we won't be able to receive SMS anymore
      if (fona.deleteSMS(slot))
      {
        Serial.println(F("Message Deleted!"));
      }
      else
      {
        Serial.print(F("Couldn't delete SMS in slot "));
        Serial.println(slot);
        fona.print(F("AT+CMGD=?\r\n"));
      }
    }
  }
}