// ————— map mode (LSDJ LiveMap) —————

#include <MIDI.h>  // assumes YOU’VE already done USING_NAMESPACE_MIDI and MIDI_CREATE_INSTANCE in your main

// Forward declarations from main:
extern int  memory[];
extern bool sequencerStarted;
extern void sequencerStart();
extern void sequencerStop();
extern void sendByteToGameboy(uint8_t b);
extern void updateVisualSync();
extern void setMode();
extern void updateStatusLight();
extern void updateBlinkLights();

extern uint8_t mapQueueWaitSerial;
extern uint8_t mapQueueWaitUsb;
extern int     mapCurrentRow;
extern int     mapQueueMessage;
extern unsigned long mapQueueTime;

// MIDI objects from main:
extern __umt usbMIDI_t;  // usb transport
extern __ss  uMIDI;      // USB‑MIDI interface
extern MidiInterface<HardwareSerial> sMIDI; // Serial‑MIDI interface

// single entrypoint for ALL LSDJ Map messages:
void handleLsdjMapMessage(byte type, byte channel, byte data1);

// call this instead of loop‑style code in main:
void modeLSDJMapSetup()
{
  // register real‑time (clock/start/stop) callbacks
  uMIDI.setHandleRealTimeSystem(handleLsdjMapMessage);
  sMIDI.setHandleRealTimeSystem(handleLsdjMapMessage);

  // blink LED to show we’re in Map mode, then enter main loop
  modeLSDJMap();
}

void modeLSDJMap()
{
  while (1) {
    // 1) drain USB‑MIDI
    while (uMIDI.read()) {
      byte type    = uMIDI.getType();
      byte channel = uMIDI.getChannel() - 1;
      byte data1   = uMIDI.getData1();
      if (channel == memory[MEM_LIVEMAP_CH] ||
         (channel == memory[MEM_LIVEMAP_CH] + 1)) {
        handleLsdjMapMessage(type, channel, data1);
      }
    }

    // 2) drain Serial‑MIDI
    if (sMIDI.read()) {
      byte type    = sMIDI.getType();
      byte channel = sMIDI.getChannel() - 1;
      byte data1   = sMIDI.getData1();
      if (channel == memory[MEM_LIVEMAP_CH] ||
         (channel == memory[MEM_LIVEMAP_CH] + 1)) {
        handleLsdjMapMessage(type, channel, data1);
      }
    }

    // 3) UI and visuals when no new MIDI
    setMode();
    updateStatusLight();
    // allow any queued map‑bytes to fire:
    if (mapQueueMessage >= 0 && millis() > mapQueueTime) {
      if (mapQueueMessage == 0xFF) {
        sendByteToGameboy(0xFF);
      } else {
        if (mapQueueMessage == 0xFE ||
            mapCurrentRow == mapQueueMessage) {
          mapCurrentRow = -1;
          sendByteToGameboy(0xFE);
        }
      }
      mapQueueMessage = -1;
      updateVisualSync();
    }
    updateBlinkLights();
  }
}

void setMapByte(uint8_t b, bool usb)
{
  uint8_t wait = usb ? mapQueueWaitUsb : mapQueueWaitSerial;
  switch (b) {
    case 0xFF:  // SystemReset / clock‑pulse placeholder
      if (mapQueueMessage < 0 || mapQueueMessage == 0xFF) {
        mapQueueTime    = millis() + wait;
        mapQueueMessage = 0xFF;
      }
      break;

    case 0xFE:  // ActiveSensing placeholder → “stop row”
      if (!sequencerStarted) {
        sendByteToGameboy(0xFE);
      } else if (mapCurrentRow >= 0) {
        if (mapQueueMessage < 0 || mapQueueMessage == 0xFF) {
          mapQueueTime    = millis() + wait;
          mapQueueMessage = mapCurrentRow;
        }
      }
      break;

    default:
      // immediate row trigger
      mapCurrentRow = b;
      sendByteToGameboy(b);
      mapQueueMessage = -1;
      break;
  }
}

void handleLsdjMapMessage(byte type, byte channel, byte data1)
{
  switch (type) {
    // ——— CLOCK ———
    case midi::Clock:
      // immediate 0xFF for every tick
      sendByteToGameboy(0xFF);
      updateVisualSync();
      break;

    // ——— TRANSPORT ———
    case midi::Start:
    case midi::Continue:
      sequencerStart();
      break;

    case midi::Stop:
      sequencerStop();
      // “stop row” on next queue if needed:
      setMapByte(0xFE, true);
      break;

    // ——— NOTES = map‑rows ———
    case midi::NoteOff:
      setMapByte(0xFE, true);
      break;

    case midi::NoteOn:
      // upper channel offsets by +128
      if (channel == memory[MEM_LIVEMAP_CH] + 1) {
        setMapByte(128 + data1, true);
      } else {
        setMapByte(data1, true);
      }
      break;

    default:
      // ignore CC, etc.
      break;
  }
}
