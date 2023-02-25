# ATmega DRAM Tester

- Uses standard March C- algorithm to fully test real-world DRAM faults
- Read mode for measuring row access time
- Only requires an Arduino Nano and a ZIF socket

## Assembling the circuit

### Arduino Nano pinout
```
 Din-|B5 |USB| B4|-Green LED+
    -|   |___| B3|-Red LED+
    -|         B2|-Mode Select
    -|C0       B1|-
    -|C1       B0|-Dout
    -|C2       D7|-A7
  WE-|C3       D6|-A6
 RAS-|C4       D5|-A5
 CAS-|C5       D4|-A4
    -|         D3|-A3
    -|         D2|-A2
  5V-|5V      GND|-GND
    -|           |-
 GND-|GND ...  D0|-A0
    -|    ...  D1|-A1
```

Mode Select:
- SPST switch to GND
- Open to select march C- test
- Close to select access time measurement

### 4164 pinout
(41128/41256 add A8)
```
 (A8)-|1  \/ 16|-GND
  Din-|2     15|-CAS
   WE-|3     14|-Dout
  RAS-|4     13|-A6
   A0-|5     12|-A3
   A2-|6     11|-A4
   A1-|7     10|-A5
   5V-|8      9|-A7
```

NOTE only CAS and A0-A2 cross sides given above alignment

## Building the software

Use the [PlatformIO](https://platformio.org/) plugin for [VSCode](https://code.visualstudio.com/).

Open the project folder with VSCode, select the environment for your board (`nano`, `oldnano`, `uno`), and click `Upload`.

See [this video](https://www.youtube.com/watch?v=nlE2203Q3XI) for help building with PlatformIO.

Distributed under the [MIT license](LICENSE.txt)
