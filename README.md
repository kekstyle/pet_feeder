# Distributeur automatique Arduino (LCD I2C + DRV8825)

Projet Arduino Uno/Mega commandant un moteur pas‑à‑pas via DRV8825 pour distribuer une dose (en tours) à heure fixe, avec:
- LCD I2C 16x2 (adresse par défaut 0x27)
- 4 boutons (OK, MENU, UP, DOWN) en INPUT_PULLUP
- Double‑clic sur OK depuis l’écran d’accueil = distribution immédiate
- Sauvegarde des réglages en EEPROM
- Journal de debug sur Serial (115200)
- Option sécurité nFAULT du DRV8825
- Mode simulation sans matériel (USE_DRV8825=0)

## Sommaire
- [Matériel](#matériel)
- [Câblage](#câblage)
- [Installation](#installation)
- [Configuration](#configuration)
- [Utilisation](#utilisation)
- [Dépannage](#dépannage)
- [Arborescence](#arborescence)
- [Licence](#licence)

## Matériel
- Arduino Uno ou Mega 2560
- Driver DRV8825 (ou compatible STEP/DIR/EN)
- Moteur pas‑à‑pas NEMA 17 (200 pas/rev typique)
- LCD I2C 16x2 (addr 0x27)
- 4 boutons poussoirs
- RTC DS3231/DS1307 (optionnelle, via RTClib)
- Alimentation moteur séparée et adaptée

## Câblage
- DRV8825:
  - STEP → D6
  - DIR  → D7
  - EN   → D8 (actif LOW)
  - nFAULT (option) → D9 (INPUT_PULLUP)
  - VMOT/GND moteur + condensateur ≥100 µF proche du driver
- LCD I2C:
  - SDA/SCL (Uno: A4/A5 ; Mega: 20/21), 5V, GND
- Boutons (actifs à LOW):
  - OK → D2, MENU → D3, UP → D4, DOWN → D5, tous vers GND
- RTC (option): SDA/SCL partagés

Astuce: règle le courant du DRV8825 (VREF) avant l’usage réel.

## Installation
1) Installer Arduino IDE.
2) Bibliothèques:
   - LiquidCrystal_I2C
   - RTClib (Adafruit)
3) Cloner ce dépôt et ouvrir le fichier .ino principal.
4) Sélectionner la carte (Uno ou Mega) et le port.
5) Téléverser, puis ouvrir le Moniteur Série à 115200 bauds.

## Configuration
Dans le code (section CONFIG MOTEUR):
```cpp
#define USE_DRV8825 1          // 0 = simulation, 1 = driver actif
#define DIR_INVERT  0          // 0 = normal, 1 = inversé
const int STEPS_PER_REV = 200; // inclure le micro‑pas effectif si souhaité
