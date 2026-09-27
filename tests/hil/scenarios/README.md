# HIL scenario contract

Deze map bevat alleen hardware-in-the-loop-scenario's die een blijvend
OpenQuatt-systeemcontract bewaken. HIL is geen archief van issues of PR's.

## Indeling

Gebruik stabiele domeinnamen. Voorbeelden van gewenste domeinen zijn:

- `input-sources`: selectie, validiteit en expiry van OT/HA/API-inputs;
- `communications`: link-loss, reconnect, stale data en handback;
- `duo`: HP1/HP2-beschikbaarheid, peer state en failover;
- `defrost`: startguards, flowgrenzen, overlap en completion;
- `boiler`: ketelassist en relevante CM-transities;
- `v2-performance`: V2 power-, Pmin- en performancecontracten.

Dit is geen verplichte lijst. Een nieuw domein is prima wanneer het een
zelfstandig en blijvend hardwarecontract vertegenwoordigt. Gebruik nooit een
bestandsnaam die primair een issue- of PR-nummer is, zoals `issue-742.mjs` of
`pr-708-link-loss.mjs`.

## Wanneer hoort een regressie hier?

Een nieuwe hardwarecase is alleen passend wanneer:

1. echte hardware/interfacetiming, Modbus/UART, OpenTherm, reboot of
   persistentie noodzakelijk is voor het bewijs;
2. een host- of simulator-only test het contract niet afdoende kan bewijzen;
3. de case deterministisch is en automatisch kan herstellen;
4. de case een bestaande domeinfile uitbreidt wanneer dat logisch is;
5. de case ook na het sluiten van de oorspronkelijke issue betekenis houdt.

Alles wat zonder echte hardware betrouwbaar te bewijzen is, hoort lager in de
testpiramide en draait in CI.

## Structuur

Een scenariofile bevat domeinspecifieke setup en assertions. Snapshotting,
locking, request-gating, firmware restore, recovery en algemene waits horen in
de gedeelde harness onder `scripts/hil/`.

Meerdere cases binnen één domein zijn gewenst. Bijvoorbeeld:

```text
defrost.mjs
  rejects_below_minimum_flow
  accepts_at_minimum_flow
  blocks_peer_overlap
  completes_forced_cycle
```

Maak hiervoor geen vier losse scenariofiles.

## Uitvoering

Hardwaretests worden gericht gekozen. Gebruik een rooktest plus de relevante
stage(s) voor een wijziging. Een volledige `all`-run is bedoeld voor brede
controllerwijzigingen of releasekwalificatie, niet als standaardvereiste voor
iedere pull request.
