# Performance Model Observer

Compacte uitwerking van punten 4 en 7, alleen voor Duo. De actieve regeling
blijft de bestaande mappings en temperatuurreferentie gebruiken.

`Performance Model Observer` staat na boot uit en slaat geen voorkeur op.
Ingeschakeld geeft `Performance Model Observer Report` elke 10 s een lokale
momentopname van het **actuele frequentiepaar**. Het interne rapport is expliciet
leesbaar via `/openquatt/entities` met domain `text_sensor` en die naam.

- `own`: Pth per ODU met gemeten Hz en eigen gekalibreerde Tout.
- `shared`: Pth per ODU met dezelfde gedeelde waterreferentie als de regelmodus
  (PH: geselecteerde systeemaanvoer; Curve: effectief aanvoerdoel).
- `series`: totaal Pth met de vaste waterketen **HP1 → HP2**.
- `pel`: elektrisch totaal van eigen Tout / serievoorspelling.
- `tout`: berekende uitlaattemperaturen van het seriepaar.

Vermogen staat in W, temperatuur in °C en frequentie in Hz. Bij uitgeschakelde
observer staat er `disabled`; ontbrekende of onbruikbare invoer geeft `unavailable`.
Een ontbrekend gedeeld referentiepunt wordt als `nan` weergegeven.

De serieberekening gebruikt HP1-buitenlucht, HP1-inlaat en HP1-flow. Met
`K = flow_lph × 4186 / 3600` wordt per ODU `Q = map(hz, buitenlucht, Tin + Q/K)`
opgelost; de berekende HP1-uitlaat wordt de HP2-inlaat. Bisectie gebruikt
hoogstens 16 stappen en een balansrest van maximaal 1 W. Een gestopte ODU
voegt geen compressorwarmte toe. Er is geen extrapolatie buiten de bestaande maps.
**90 Hz inclusief** is toegestaan; hogere frequenties worden afgewezen.

De basiscontroles vereisen online ODU's, verse oorspronkelijke water-, flow-,
frequentie- en modusreceipts (30 s; buitenlucht 30 min), verwarmen of uit,
geen defrost, flow 300–3000 l/h en maximaal 0,5 °C tussen HP1-Tout en HP2-Tin.
De installatie wordt als HP1 → HP2 met gemeenschappelijke flow verondersteld.

Er is geen kandidaatzoektocht, cache, stabiliteitsvenster, aparte taak of
PSRAM-werkallocatie. De momentopname verklaart een modelverschil; ze bewijst
geen stationair bedrijf, betere nauwkeurigheid of besparing. Veldvalidatie
blijft nodig voordat eigen Tout of seriekoppeling actief mag sturen.

## Validatie

Gerichte hosttests controleren eigen Tout, seriebalans, Single/OFF en de
bestaande modelgrenzen. De volledige hostregressie (124 tests), 443 Python-tests
(twee bestaande skips), formattering en onafhankelijke koude review slagen.
De Q-Duo-configuratie is geldig; de bestaande NVS-budgetcheck blijft falen
(60 entries vrij tegenover 100 vereist).

HIL op `scripts/hil/configs/water_performance_duo.yaml`: OTA/default-off PASS,
firmware `0x9d338e0f`, SHA256
`79fbb4a3c94698c84efeffb9335d895559e271b57d56420d3b862bd1820dfaf7`.
Bij -7 °C, 870,144 l/h, Tin1 30 °C en werkelijk 90/82 Hz gaf de compacte versie
`own=5829.51/5002.60`, `shared=5936.71/5385.62`, `series=10835.01` W.
Uitschakelen gaf binnen de 10s-cadans `disabled`; de frequenties bleven 90/82 Hz.
Bij omschakelen naar Curve met 35 °C doel veranderde de gedeelde voorspelling
naar 5877,50/5331,31 W voor hetzelfde gerapporteerde frequentiepaar, gelijk aan
de hostberekening. Dit toetst de referentiebron; er wordt geen Curve-keuzezoektocht
uitgevoerd of stationair Curve-bedrijf aangetoond.

Actief intern geheugen: 87671 B vrij, minimum sinds boot 39828 B, grootste
blok 40960 B, fragmentatie 53,28%; vrij PSRAM 5342260 B. Statische DIRAM:
218639 B, 1736 B minder dan de uitgebreide observerbuild. Xtensa-stackframes:
rapport 592 B, solver 112 B, modeladapter 96 B. Deze begrensde REST/SSE-labproef
kwalificeert geen volledige HA-/MQTT-belasting.
