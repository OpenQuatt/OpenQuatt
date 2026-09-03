# Handmatige installatie

De normale installatieroute is de [OpenQuatt installer](https://openquatt.github.io/OpenQuatt/install/). Gebruik handmatige installatie alleen als de installer niet werkt of als je bewust een firmwarebestand uit een release wilt flashen.

## Wanneer gebruik je dit?

Gebruik deze route alleen bij:

- een browser of computer waarop de installer niet goed werkt;
- een herstelactie na een mislukte flash;
- testen met een specifiek releasebestand;
- gevorderde diagnose.

Voor de meeste gebruikers is dit niet de eerste keuze.

## Wat heb je nodig?

- Chrome of Edge op desktop;
- een USB-datakabel;
- het juiste `*.firmware.factory.bin` bestand uit een OpenQuatt-release;
- zekerheid over je opstelling: `Single` of `Duo`;
- zekerheid over je hardwareprofiel.

## Het juiste bestand kiezen

Kies een factory-bestand dat precies past bij je installatie.

Voor de Heatpump Controller Q-edition is er sinds de gecombineerde netwerkfirmware nog maar één factory-binary per topologie. Wi-Fi en Ethernet zitten in dezelfde firmware; de verbindingsmodus wordt runtime gekozen.

Gebruik voor Q-edition:

```text
openquatt-heatpump-controller-q-single.firmware.factory.bin
openquatt-heatpump-controller-q-duo.firmware.factory.bin
```

Oudere Wi-Fi-/Ethernet-manifestnamen blijven voor OTA-compatibiliteit bestaan, maar verwijzen naar dezelfde canonieke Single- of Duo-binary. Er worden hiervoor geen aparte Q Wi-Fi- of Ethernet-releasebinaries gepubliceerd.

Voor andere ondersteunde hardwareprofielen kan de verbindingsvariant nog wel onderdeel van de bestandsnaam zijn.

Gebruik geen `ota.bin` voor een eerste installatie via USB. Voor de eerste flash heb je een factory-binary nodig.

## Flashen

1. Download het juiste factory-bestand uit de GitHub Release.
2. Open [ESP Web Tools](https://web.esphome.io/).
3. Sluit de OpenQuatt-module via USB aan.
4. Kies `Connect`.
5. Kies het gedownloade factory-bestand.
6. Flash de module.
7. Bij een Heatpump Controller Q stel je Wi-Fi eenmalig in voor fallback; Ethernet en Wi-Fi gebruiken daarna dezelfde firmware.

## Na het flashen

Als Wi-Fi niet direct via de browserflow lukt, gebruikt een Wi-Fi-capabele build het OpenQuatt fallback access point:

- SSID: `OpenQuatt`
- wachtwoord: `openquatt`

Open daarna de web-app via:

```text
http://openquatt.local
```

Loop vervolgens de Quick Start in de web-app door.

## Veelgemaakte fouten

- `Single` flashen op een `Duo`-installatie, of andersom.
- Een firmwarebestand voor het verkeerde hardwareprofiel kiezen.
- Een OTA-bestand gebruiken voor de eerste USB-installatie.
- De browser sluiten voordat Wi-Fi is ingesteld.
- Bij Q-edition zoeken naar een aparte Wi-Fi- of Ethernet-factory-binary; gebruik daar de canonieke Single- of Duo-binary.

Als je twijfelt, kies dan opnieuw je route in het [projectoverzicht](../README.md#kies-je-route) en gebruik de normale installer of Q-edition-handleiding.
