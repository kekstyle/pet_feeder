# BOM — Distributeur automatique (Arduino Mega 2560 + DRV8825)

Matériel principal (fourni par toi)
| Référence | Qté | Spécifications clés | Remarques |
|---|---:|---|---|
| Arduino Mega 2560 | 1 | 5 V, 16 MHz, nombreux GPIO | Port USB pour programmation et debug série. |
| Carte d’extension pilote DRV8825 (shield avec DIP) | 1 | Support DRV8825, sélecteurs DIP micro‑pas, bornier VMOT/GND | Permet de régler le micro‑pas (MS1/MS2/MS3). Alim moteur séparée via bornier. |
| Driver pas‑à‑pas DRV8825 | 1 | 8.2–45 V VMOT, jusqu’à ~2.2 A crête (≈1.5 A RMS avec refroid.) | Ajouter radiateur. Régler VREF en fonction du moteur. |
| Boutons poussoirs | 4 | Momentanés, 6×6 mm (ou équivalent) | OK, MENU, UP, DOWN. Câblés vers GND, INPUT_PULLUP dans le code. |
| Écran LCD I2C 16×2 | 1 | PCF8574, 5 V, adresse 0x27 (souvent) | Contraste via potentiomètre. Vérifier adresse I2C. |
| Jeu de fils Dupont | 1 jeu | M–M / M–F assortis | Pour relier LCD, boutons, signaux STEP/DIR/EN si besoin. |
| Alimentation 12 V (pour la carte d’extension) | 1 | 12 V DC, ≥2 A recommandé | Alimente VMOT. Masse commune avec l’Arduino. |

Recommandés (sécurité/stabilité)
| Référence | Qté | Spécifications clés | Remarques |
|---|---:|---|---|
| Condensateur électrolytique VMOT | 1 | 100–220 µF, ≥35 V | À souder au plus près du bornier VMOT/GND du shield. Indispensable avec DRV8825. |
| Radiateur autocollant pour DRV8825 | 1 | 9×9 mm (typique) | Aide au refroidissement, surtout >0.8 A. |
| Ventilation (petit ventilateur 40 mm) | 1 | 5 V ou 12 V selon dispo | Option si courant moteur élevé. |
| Pile CR2032 + support (si RTC ajoutée) | 1 | — | Seulement si tu ajoutes une RTC DS3231/DS1307. |
| Entretoises/vis | 1 set | Nylon ou métal | Fixation propre du LCD/shield. |

Câblage cible (rappel)
- DRV8825: STEP → D6, DIR → D7, EN → D8 (actif LOW), nFAULT (option) → D9.
- LCD I2C: SDA/SCL (Mega: 20/21), +5 V, GND, adresse par défaut 0x27.
- Boutons: D2 (OK), D3 (MENU), D4 (UP), D5 (DOWN) vers GND (INPUT_PULLUP).
- Alimentation: VMOT 12 V sur le shield; GND VMOT relié au GND Arduino (masse commune).

Notes d’intégration
- Réglage du courant: ajuster VREF du DRV8825 en fonction du courant nominal du NEMA 17. Commencer bas, monter progressivement.
- Micro‑pas: configurer MS1/MS2/MS3 sur le shield (DIP) selon la résolution souhaitée; documenter le réglage dans le README.
- Sens de rotation: via DIR_INVERT dans le code ou en inversant une seule bobine (A+↔A− ou B+↔B−).

Cases à cocher avant mise sous tension
- [ ] Condensateur VMOT installé
- [ ] Radiateur collé sur DRV8825
- [ ] Masse commune Arduino ↔ Shield VMOT
- [ ] VREF réglé
- [ ] Polarité moteur correcte
- [ ] Adresse I2C LCD confirmée
