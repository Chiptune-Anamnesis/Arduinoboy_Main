/**************************************************************************
 * Name:    Timothy Lamb                                                  *
 * Email:   trash80@gmail.com                                             *
 ***************************************************************************/
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <Arduino.h>
#include <MIDI.h>       // or whatever MIDI library you’re using
#include <MIDI_USB.h>   // for uMIDI
#include <MIDI_Serial.h>// for sMIDI

// pins, state vars, etc.
extern uint8_t pinStatusLed, pinGBClock;
extern int blinkMaxCount;
extern bool sequencerStarted;
extern int mapQueueWaitSerial, mapQueueWaitUsb;
extern int mapCurrentRow, mapQueueMessage;
extern unsigned long mapQueueTime;
extern uint8_t memory[];
void sequencerStart();
void sequencerStop();
void sendByteToGameboy(uint8_t);
void updateVisualSync();
void setMode();
void updateStatusLight();
void updateBlinkLights();

// MIDI interfaces
MIDI_NAMESPACE::MidiUSB   uMIDI;  // HAS_USB_MIDI
MIDI_NAMESPACE::MidiSerial sMIDI; // HAS_SERIAL_MIDI

void handleLsdjMapMessage(byte type, byte channel, byte data1);

void modeLSDJMapSetup()
{
  digitalWrite(pinStatusLed, LOW);
  pinMode(pinGBClock, OUTPUT);
  digitalWrite(pinGBClock, HIGH);

  // register for Real‑Time messages
#ifdef HAS_USB_MIDI
  uMIDI.setHandleRealTimeSystem(handleLsdjMapMessage);
#endif
#ifdef HAS_SERIAL_MIDI
  sMIDI.setHandleRealTimeSystem(handleLsdjMapMessage);
#endif

  blinkMaxCount = 1000;
  modeLSDJMap();
}

void modeLSDJMap()
{
  while (1) {
    modeLSDJMapUsbMidiReceive();
    checkMapQueue();

    // if no serial MIDI message pending, do UI stuff
    if (!modeLSDJMapSerialMidiReceive()) {
      setMode();
      updateStatusLight();
      checkMapQueue();
      updateBlinkLights();
    }
  }
}

void setMapByte(uint8_t b, boolean usb)
{
    uint8_t wait = usb ? mapQueueWaitUsb : mapQueueWaitSerial;

    switch (b) {
      case midi::SystemReset:   // 0xFF
        setMapQueueMessage(0xFF, wait);
        break;
      case midi::ActiveSensing: // 0xFE
        if (!sequencerStarted) {
          sendByteToGameboy(0xFE);
        } else if (mapCurrentRow >= 0) {
          setMapQueueMessage(mapCurrentRow, wait);
        }
        break;
      default:
        mapCurrentRow = b;
        sendByteToGameboy(b);
        resetMapCue();
    }
}

void setMapQueueMessage(uint8_t m, uint8_t wait)
{
    if (mapQueueMessage == -1 || mapQueueMessage == 0xFF) {
        mapQueueTime    = millis() + wait;
        mapQueueMessage = m;
    }
}

void resetMapCue()
{
    mapQueueMessage = -1;
}

void checkMapQueue()
{
  if (mapQueueMessage >= 0 && millis() > mapQueueTime) {
      if (mapQueueMessage == 0xFF) {
          sendByteToGameboy(0xFF);
      } else {
          if (mapQueueMessage == 0xFE || mapCurrentRow == mapQueueMessage) {
              mapCurrentRow = -1;
              sendByteToGameboy(0xFE);
          }
      }
      mapQueueMessage = -1;
      updateVisualSync();
  }
}

void modeLSDJMapUsbMidiReceive() {
#ifdef HAS_USB_MIDI
  while (uMIDI.read()) {
    byte type    = uMIDI.getType();
    byte channel = uMIDI.getChannel() - 1;
    byte data1   = uMIDI.getData1();

    if (channel != memory[MEM_LIVEMAP_CH]
     && channel != (memory[MEM_LIVEMAP_CH] + 1)) {
      continue;
    }

    handleLsdjMapMessage(type, channel, data1);
  }
#endif
}

bool modeLSDJMapSerialMidiReceive() {
#ifdef HAS_SERIAL_MIDI
  if (!sMIDI.read()) return false;

  byte type    = sMIDI.getType();
  byte channel = sMIDI.getChannel() - 1;
  byte data1   = sMIDI.getData1();

  if (channel != memory[MEM_LIVEMAP_CH]
   && channel != (memory[MEM_LIVEMAP_CH] + 1)) {
    return true;  // we consumed it, but do nothing
  }

  handleLsdjMapMessage(type, channel, data1);
  checkMapQueue();
  return true;
#else
  return false;
#endif
}

void handleLsdjMapMessage(byte type, byte channel, byte data1) {
  switch (type) {
    // === IMMEDIATE CLOCK SYNC ===
    case midi::Clock:
      sendByteToGameboy(0xFF);
      updateVisualSync();
      break;

    // === TRANSPORT ===
    case midi::Start:
    case midi::Continue:
      resetMapCue();
      sequencerStart();
      break;
    case midi::Stop:
      sequencerStop();
      setMapByte(0xFE, true);
      break;

    // === NOTE MAPPING ===
    case midi::NoteOff:
      setMapByte(0xFE, true);
      break;
    case midi::NoteOn:
      if (channel == (memory[MEM_LIVEMAP_CH] + 1)) {
        setMapByte(128 + data1, true);
      } else {
        setMapByte(data1, true);
      }
      break;

    default:
      // ignore all other messages
      break;
  }
}
