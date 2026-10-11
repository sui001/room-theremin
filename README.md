# Room Theremin

A theremin you play by walking around a room. An HLK-LD2450 mmWave radar tracks up to three bodies, an ESP32-S3 SuperMini passes their positions to a laptop over USB, and a browser page turns them into sound.

One person is enough: left/right sets pitch, distance from the sensor sets volume. Each extra person (up to three) gets their own voice.

## Files

| Path | What it is |
|---|---|
| `ld2450_theremin_v1_0/ld2450_theremin_v1_0.ino` | Firmware. Reads the radar, prints one JSON line per frame (~10 Hz) |
| `room_theremin.html` | Visualiser and synth. Top-down room view, note bands, Web Audio voices |

## Wiring (ESP32-S3 SuperMini)

| LD2450 | SuperMini |
|---|---|
| TX | GPIO4 |
| RX | GPIO5 |
| 5V | 5V |
| GND | GND |

Onboard LED (GPIO47): breathing blue = empty room, green = someone playing, amber flash = radar silent.

## Flash

- Board: ESP32S3 Dev Module, USB CDC On Boot: Enabled, Flash 4MB
- Library: Adafruit NeoPixel
- Check in Serial Monitor: walk in front of the sensor, expect `{"t":[[x,y,v],null,null]}` lines

## Play

1. Close the Arduino Serial Monitor (only one program can hold the port).
2. Open `room_theremin.html` in Chrome or Edge (Web Serial needed; Firefox and Safari won't work).
3. **Connect sensor**, pick the port, then **Start sound**.

### From plotterpi, no laptop wiring

plotterpi already reads up to three LD2450s through its S3-Matrix, and serves this page at `http://<plotterpi>:8090/theremin`. Opened there, it listens to plotterpi's radar stream by itself, so any phone or laptop on the tailnet can play it and the sound comes out of that device.

- **On 0 1 2**: which sensors exist and are powered. This is plotterpi's own setting, shared with its room view.
- **Play**: which sensor the theremin follows. Until you pick, it follows whichever one is sending.

**Listen** also works with `ld2450/serve.py` from gen3d, pointed at its `/targets` address.

No hardware? Drag on the room view to fake bodies (multi-touch works), or turn on **Ghost walker** for a simulated second player.

## Serial format

```
{"t":[[x,y,v],[x,y,v],null]}   x, y in mm (sensor at origin, y = out from it), v = speed cm/s, null = empty slot
{"stale":1}                    no radar frame for 1 s
# ...                          status text, ignored by the page
```

## Known limits

- Radar latency plus smoothing is roughly 100 to 200 ms. Scale snapping (pentatonic is the default) hides the jitter better than continuous mode.
- The radar picks up breathing, so someone standing still holds a note.
- More than one mmWave sensor in the same room may interfere.
- Firmware has not been compiled or run on hardware yet.

## Next

- BLE Auracast for multiple receivers
- ESP-NOW so the theremin can join the Who is the node? mesh
