-----

# Midea AC IR Protocol Decode (RG57A7/BGEF)

## Overview

This repository documents the reverse-engineered Infrared (IR) protocol for the **Midea MSAFA-09CRDN8** Air Conditioner using the **RG57A7/BGEF** remote control.

While libraries exist for generic Midea protocols, this specific remote model exhibits unique behaviors regarding checksum calculation, temperature encoding (Look-Up Table), and "Special Function" toggles (LED/Turbo) that differ from standard implementations. This documentation is intended for developers creating custom ESP32/Arduino controllers.

## Physical Layer (Timing Specification)

  * **Carrier Frequency:** 38 kHz
  * **Protocol Type:** Pulse Distance Encoding (LSB First)
  * **Transmission:** 48 bits (6 Bytes) sent twice per command for redundancy.

### Timing Constants (Microseconds)

| State | Duration (µs) | Tolerance |
| :--- | :--- | :--- |
| **Header Mark** | 4350 | ±200 |
| **Header Space** | 4400 | ±200 |
| **Bit 1 Mark** | 560 | ±150 |
| **Bit 1 Space** | 1690 | ±150 |
| **Bit 0 Mark** | 560 | ±150 |
| **Bit 0 Space** | 560 | ±150 |
| **Stop Mark** | 560 | N/A |
| **Frame Gap** | 5200 | (Minimum) |

### Packet Frame Structure

```text
[Header Mark] [Header Space] 
[Byte 0] [Byte 1] [Byte 2] [Byte 3] [Byte 4] [Byte 5] 
[Stop Mark] [Frame Gap] 
(Repeat Entire Sequence)
```

-----

## Data Layer (Byte Definitions)

### Standard State Packet (Power, Temp, Mode, Fan)

The remote sends a 6-byte packet for all standard state changes.

| Byte | Value (Hex) | Description |
| :--- | :--- | :--- |
| **0** | `0x4D` | Static Manufacturer Header A |
| **1** | `0xB2` | Static Manufacturer Header B |
| **2** | `Variable` | **Fan Speed** & **Mode bits** |
| **3** | `~Byte2` | **Inverted Byte 2** (Validation check) |
| **4** | `Variable` | **Operating Mode** (High Nibble) + **Temperature** (Low Nibble) |
| **5** | `Checksum` | Calculated Checksum |

### Byte 2: Fan Speed & Mode Bits

This byte dictates the Fan Speed. Note: If the AC is in **Auto Mode**, this byte changes to a specific "Auto" signature regardless of the selected fan speed.

| Setting | Hex Code |
| :--- | :--- |
| **Fan Auto** | `0xFD` |
| **Fan Low** | `0xF9` |
| **Fan Med** | `0xFA` |
| **Fan High** | `0xFC` |
| **Auto Mode (Override)** | `0xF8` |

*\> **Critical Logic:** If the AC is in Auto Mode (Byte 4 High Nibble = `0x1`), Byte 2 MUST be set to `0xF8`. Sending `0xF9` (Fan Low) while in Auto Mode will be rejected or ignored by the AC.*

### Byte 4: Mode & Temperature

This byte is a split byte.

  * **High Nibble (Bits 4-7):** Operating Mode
  * **Low Nibble (Bits 0-3):** Temperature Index (See LUT)

#### High Nibble (Mode)

| Mode | High Nibble | Binary (Upper) |
| :--- | :--- | :--- |
| **Cool** | `0x0_` | `0000xxxx` |
| **Auto** | `0x1_` | `0001xxxx` |
| **Heat** | `0x3_` | `0011xxxx` |

#### Low Nibble (Temperature Look-Up Table)

The temperature encoding is **non-linear**. It does not correspond directly to the integer value.

| Temp (°C) | Low Nibble (Hex) | Binary (Lower) |
| :--- | :--- | :--- |
| **17** | `0x0` | `xxxx0000` |
| **18** | `0x8` | `xxxx1000` |
| **19** | `0xC` | `xxxx1100` |
| **20** | `0x4` | `xxxx0100` |
| **21** | `0x6` | `xxxx0110` |
| **22** | `0xE` | `xxxx1110` |
| **23** | `0xA` | `xxxx1010` |
| **24** | `0x2` | `xxxx0010` |
| **25** | `0x3` | `xxxx0011` |
| **26** | `0xB` | `xxxx1011` |
| **27** | `0x9` | `xxxx1001` |
| **28** | `0x1` | `xxxx0001` |
| **29** | `0x5` | `xxxx0101` |
| **30** | `0xD` | `xxxx1101` |

**Example Construction:**
To send **24°C** in **Cool Mode**:

  * Mode Cool = `0x0`
  * Temp 24 = `0x2`
  * **Byte 4 = `0x02`**

To send **30°C** in **Heat Mode**:

  * Mode Heat = `0x3`
  * Temp 30 = `0xD`
  * **Byte 4 = `0x3D`**

-----

## Checksum Algorithm

The checksum is the last byte (Byte 5). For the RG57A7/BGEF, the checksum logic is derived as follows:

```cpp
// Formula
Checksum = (0xFD - (Byte0 + Byte1 + Byte2 + Byte3 + Byte4)) & 0xFF;
```

*Note: Standard algebraic summation is used. The `0xFD` constant appears to be specific to the specific bit-masking of this remote version.*

-----

## Special Function Packets (LED / Turbo)

Functions like LED Toggle and Turbo do not send the full state (Temp/Fan). They send a "Toggle Command" using a different header.

| Byte | Value | Notes |
| :--- | :--- | :--- |
| **0** | `0xAD` | **Different Header A** |
| **1** | `0x52` | **Different Header B** |
| **2** | `0xAF` | Static |
| **3** | `0x50` | Static |
| **4** | `Variable` | Command ID |
| **5** | `Checksum` | Same Algorithm |

**Known Special Byte 4 IDs:**

  * **LED Toggle:** `0xA5` (Full Packet: `AD 52 AF 50 A5 Checksum`)
  * **Turbo Toggle:** `0x45` (Full Packet: `AD 52 AF 50 45 Checksum`)

-----

## Implementation Logic (Pseudo-Code)

When implementing a "Virtual Remote" to avoid resetting the temperature to default values, use this logic flow:

1.  **Store State:** Keep global variables for `CurrentTemp`, `CurrentMode`, `CurrentFan`.
2.  **Input Handling:** If user requests "Fan Low":
      * Check: Is `CurrentMode == Auto`?
      * If Yes: Ignore command or Force Byte 2 to `0xF8`.
      * If No: Update `CurrentFan = Low`.
3.  **Packet Construction:**
      * Retrieve `FanByte` from Fan table.
      * Retrieve `TempNibble` from LUT.
      * Retrieve `ModeNibble` from Mode table.
      * Combine into Byte 4 (`Mode << 4 | Temp`).
      * Calculate Checksum.
      * Send Raw Timing.

-----

## Contributing
**This project is open source and we welcome contributions from the community!**

The Midea IR protocol is used by dozens of rebranded air conditioners (including **Carrier, Toshiba, Pioneer, Senville, Comfee,Lennox (Specific mini-split lines), Klimaire, AirCon, Arctic King (Midea sub-brand) and MrCool**). If you have a different remote model that looks similar to the RG57 series, we need your help to verify compatibility.

### How you can help:
1.  **Test on your Hardware:** If this code works for your specific AC model, please open an Issue or Pull Request to add your Model Number to our "Supported Devices" list.
2.  **Report Bugs:** If you find a specific mode (like "Dry" or "Fan Only") that behaves unexpectedly, please log an issue with your serial logs.
3.  **Code Improvements:** Optimizations to the checksum algorithm or the ESP32 state management logic are always welcome via Pull Requests.

Let's build the most robust open-source documentation for this protocol together.

## Credits
* **Maintainer:** [chegame56](https://github.com/chegame56/)
* **Hardware Verified:** Midea MSAFA-09CRDN8
* **Remote Model:** RG57A7/BGEF
* **Decoding Strategy:** Reverse engineered via raw IR capture analysis and state-reconstruction.

*Special thanks to the open-source community for providing the tools (Arduino-IRremote, IRremoteESP8266) that made this reverse engineering possible.*
