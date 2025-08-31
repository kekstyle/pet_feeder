
docs/GUIDE_INSTALLATION.md
```markdown
# Guide d’installation — Distributeur Arduino

Ce guide explique l’installation matérielle et logicielle pas à pas avec l’Arduino IDE.

## 1. Prérequis
- Arduino Uno ou Mega 2560
- DRV8825 + radiateur + condensateur ≥100 µF entre VMOT et GND
- Moteur pas‑à‑pas NEMA 17
- LCD I2C 16x2 (0x27 par défaut)
- 4 boutons poussoirs (OK, MENU, UP, DOWN)
- (Option) RTC DS3231/DS1307
- Alimentation moteur adaptée (séparée de préférence)

## 2. Câblage rapide
- DRV8825 → Arduino:
  - STEP D6, DIR D7, EN D8 (LOW = actif), nFAULT D9 (INPUT_PULLUP, option)
- LCD I2C → SDA/SCL, +5 V, GND
- Boutons → D2..D5, tous vers GND (INPUT_PULLUP dans le code)
- Moteur → connecteur 4 fils (A+/A−, B+/B−). Inverser UNE paire pour changer le sens.
- Ajoutez un condensateur électrolytique (≥100 µF) près de VMOT.

Sécurité: ne branche/débranche jamais le moteur sous tension.

## 3. Réglage du DRV8825 (VREF)
1) Couper l’alim moteur.
2) Mettre le multimètre: COM sur GND, + sur le potentiomètre VREF.
3) Régler VREF selon le courant du moteur (cf. datasheet/guide DRV8825).
4) Rallumer et vérifier la température lors des premiers tests.

## 4. Préparation logicielle (Arduino IDE)
1) Installer les bibliothèques:
   - Outils → Gérer les bibliothèques:
     - LiquidCrystal_I2C
     - RTClib (Adafruit)
2) Ouvrir le sketch du projet.
3) Sélectionner la carte (Uno/Mega) et le port.
4) Moniteur Série: 115200 bauds.

## 5. Paramètres du projet
Dans le code (section CONFIG MOTEUR):
```cpp
#define USE_DRV8825 1
#define DIR_INVERT  0
const int STEPS_PER_REV = 200;
