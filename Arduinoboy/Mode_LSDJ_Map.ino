// Mode_LSDJ_Map.ino
// — LSDJ Live‑Map (“SyncMap”) mode with proper clock sync —

#include <Arduino.h>
#include <MIDI.h>
USING_NAMESPACE_MIDI;

// Pull in these globals/functions from your main Arduinoboy.ino:
extern byte              memory[];
extern bool              sequencerStarted;
extern void              sequencerStart();
extern void              sequencerStop();
extern void              sendByteToGameboy(uint8_t b);
extern void              updateVisualSync();
extern void              setMode(byte mode);
extern void              updateStatusLight();
extern void              updateBlinkLights();

// Map‑queue state (from your globals):
extern uint8_t           mapQueueWaitSerial;
extern uint8_t           mapQueueWaitUsb;
extern int               mapCurrentRow;
extern int               mapQueueMessage;
extern unsigned long     mapQueueTime;

// Forward declarations for our helper functions:
void setMapByte(uint8_t b, bool usb);
void setMapQueueMessage(uint8_t m, uint8_t wait);
void resetMapCue();
void checkMapQueue();
void handleLsdjMapMessage(byte type, byte channel, byte data1);

// Called when entering Map mode:
void modeLSDJMapSetup()
{
  // LED & clock pin init (same as before)
  digitalWrite(pinStatusLed, LOW);
  pinMode(pinGBClock, OUTPUT);
  digitalWrite(pinGBClock, HIGH);

  blinkMaxCount = 1000;
  modeLSDJMap();
}

// The Map‑mode “loop”
void modeLSDJMap()
{
  while (1) {
    // — Drain USB MIDI —
#ifdef HAS_USB_MIDI
    while (uMIDI.read()) {
      byte t  = uMIDI.getType();
      byte ch = uMIDI.getChannel() - 1;
      byte d1 = uMIDI.getData1();
      if (ch == memory[MEM_LIVEMAP_CH] || ch == memory[MEM_LIVEMAP_CH] + 1) {
        handleLsdjMapMessage(t, ch, d1);
      }
    }
#endif

    // — Drain Serial MIDI —
    if (sMIDI.read()) {
      byte t  = sMIDI.getType();
      byte ch = sMIDI.getChannel() - 1;
      byte d1 = sMIDI.getData1();
      if (ch == memory[MEM_LIVEMAP_CH] || ch == memory[MEM_LIVEMAP_CH] + 1) {
        handleLsdjMapMessage(t, ch, d1);
      }
      checkMapQueue();
    }

    // — No MIDI pending: UI + fire queued q‑bytes —
    setMode(memory[MEM_MODE]);
    updateStatusLight();
    checkMapQueue();
    updateBlinkLights();
  }
}

// Send a map byte (0x00–0xFF) to Game Boy, with optional queuing
void setMapByte(uint8_t b, bool usb)
{
  uint8_t wait = usb ? mapQueueWaitUsb : mapQueueWaitSerial;
  switch (b) {
    case 0xFF:  // placeholder for clock tick
      setMapQueueMessage(0xFF, wait);
      break;

    case 0xFE:  // placeholder for note‑off / row‑off
      if (!sequencerStarted) {
        sendByteToGameboy(0xFE);
      } else if (mapCurrentRow >= 0) {
        setMapQueueMessage(mapCurrentRow, wait);
      }
      break;

    default:    // immediate row trigger
      mapCurrentRow = b;
      sendByteToGameboy(b);
      resetMapCue();
      break;
  }
}

// Queue a byte to fire after “wait” ms (if not already queued)
void setMapQueueMessage(uint8_t m, uint8_t wait)
{
  if (mapQueueMessage == -1 || mapQueueMessage == 0xFF) {
    mapQueueTime    = millis() + wait;
    mapQueueMessage = m;
  }
}

// Cancel any queued row‑off
void resetMapCue()
{
  mapQueueMessage = -1;
}

// Fire any queued map byte if its time has come
void checkMapQueue()
{
  if (mapQueueMessage >= 0 && millis() > mapQueueTime) {
    if (mapQueueMessage == 0xFF) {
      // clock tick
      sendByteToGameboy(0xFF);
    } else {
      // row‑off
      if (mapQueueMessage == 0xFE || mapCurrentRow == mapQueueMessage) {
        mapCurrentRow = -1;
        sendByteToGameboy(0xFE);
      }
    }
    mapQueueMessage = -1;
    updateVisualSync();
  }
}

// Handle every incoming NoteOn/Off, Transport, and Clock message:
void handleLsdjMapMessage(byte type, byte channel, byte data1)
{
  switch (type) {
    // === FULL‑RES CLOCK PULSE ===
    case midi::Clock:
      sendByteToGameboy(0xFF);
      updateVisualSync();
      break;

    // — Transport Start/Continue —
    case midi::Start:
    case midi::Continue:
      resetMapCue();
      sequencerStart();
      break;

    // — Transport Stop —
    case midi::Stop:
      sequencerStop();
      setMapByte(0xFE, true);
      break;

    // — Map rows via NoteOff/NoteOn —
    case midi::NoteOff:
      setMapByte(0xFE, true);
      break;

    case midi::NoteOn:
      if (channel == memory[MEM_LIVEMAP_CH] + 1) {
        setMapByte(128 + data1, true);
      } else {
        setMapByte(data1, true);
      }
      break;

    default:
      // ignore CC, Program Change, etc.
      break;
  }
}
