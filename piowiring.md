Dan wordt het een stuk eenvoudiger.Goed nieuws: alles direct, geen spanningsdeler nodig.

SRX882S voeding: 2.4–5.5V, STX882 voeding: 3.3–5.5V — beide werken prima op 3.3V, en de data-pinnen zijn direct RP2040-compatibel.

SRX882S pinout: pin 1 = ANT, pin 2 = GND, pin 3 = VCC, pin 4 = CS (SRX882**S**: VCC aansluiten!), pin 5 = DATA uit, pin 6 = GND. STX882: pin 1 = ANT, pin 2 = DATA in, pin 3 = VCC, pin 4 = GND.

**Belangrijk verschil SRX882 vs SRX882S:** pin 4 (CS) moet bij de S-versie op VCC aangesloten worden, niet zwevend of GND — anders werkt de module niet.

## Bedradingsoverzicht

```
SRX882S (ontvanger)        RP2040
------------------         ------------------
pin 1  ANT   --------      17cm draad
pin 2  GND   ------------ GND
pin 3  VCC   ------------ 3.3V
pin 4  CS    ------------ 3.3V  (!)
pin 5  DATA  ------------ GPIO RX (PIO input)
pin 6  GND   ------------ GND

STX882 (zender)            RP2040
------------------         ------------------
pin 1  ANT   --------      17cm draad (andere antenne!)
pin 2  DATA  ------------ GPIO TX (PIO output)
pin 3  VCC   ------------ 3.3V
pin 4  GND   ------------ GND
```

Dat is alles — geen level shifter, geen spanningsdeler, geen schakellogica voor RX/TX omschakeling. Aparte antennes voor RX en TX is ideaal want geen TX/RX switching nodig.

Voor de PIO code uit het vorige document verandert er niets — `RF_DATA_PIN` is gewoon de GPIO die op SRX882S pin 5 zit, en TX doe je met een tweede PIO state machine op de GPIO die naar STX882 pin 2 gaat.

Ja, dan is de SenseCAP ruim voldoende. Laat me de redenering strak maken.

## YAGNI 868MHz + dynamische timing = probleem weg

De PIO resource-vraag was eigenlijk alleen relevant als je twee banden parallel wilde. Met één band:

- 1 PIO state machine voor capture (RX)
- 1 PIO state machine voor TX
- 1 UART state machine naar ESP32-S3 (al intern geregeld door Seeed firmware)

Dat past ruim binnen de 8 beschikbare, ook met wat intern gebruik voor SD-kaart SPI.

## 433MHz protocolruimte

De grote families die op 433MHz leven:

**Statische timing (vaste T):** KAKU, NewKAKU, X10, HomeEasy, DIO, Chacon, Nexa, Brennenstuhl, Mumbi — allemaal PPM/PWM met 1–2 tijdklassen. Dit is het grootste deel van de markt.

**Dynamische timing (geen vaste T):** Oregon Scientific, AcuRite, Fine Offset, LaCrosse, Hideki — jouw `psNibbleIndex` aanpak is hier exact voor gemaakt.

**Manchester/bi-phase:** Oregon v2/v3, sommige alarm-sensoren — `RkrManchesterAnalysis` dekt dit.

**Wat RFControl/Portisch ook al bewezen:** <br>
RFControl gebruikt dezelfde filosofie als jouw PSI — tijdwaarden quantiseren naar buckets en dan pattern-matching, zonder vaste T. Portisch voegt daar nog Somfy RTS aan toe (rolling code, 433.42MHz — dat is de enige echte randcase, maar die draait ook gewoon op 433MHz hardware).

## De enige echte beperking

De 4 Grove-pinnen zijn bezet met SRX882S + STX882. Geen marge voor iets extra's. Maar als de RF bridge het enige doel is van de SenseCAP RP2040-kant, is dat geen probleem — de ESP32-S3 doet WiFi/display toch al autonoom.

## Conclusie

SenseCAP is ruim voldoende voor:
- Alle gangbare 433MHz OOK protocollen
- Dynamische timing via psNibbleIndex / OokTimeRange
- Manchester/Oregon via RkrManchesterAnalysis
- Repeat-detectie voor alles

Je hebt precies genoeg GPIO's, precies genoeg PIO, en de architectuur (RP2040 doet capture ? UART ? ESP32-S3 doet WiFi/display) is al wat Seeed bedoeld heeft. Je zwemt met de architectuur mee in plaats van er tegenin.
