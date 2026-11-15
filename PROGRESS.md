# Avanzamento

Aggiornato: 2026-11-15 · Branch di lavoro: `hub-node`

## Fase in corso

**Fase 1 · Hub: hardware**, in corso. Fase 0 chiusa con l'ok di Pedro il 2026-11-15.

## Fatto

- Fase 0: orientamento, `CLAUDE.md`, agenti.
- Installata `Seeed_GFX2` (da GitHub); core esp32 aggiornato da 3.0.7 a 3.3.12 perché la libreria non compilava.
- Porte confermate da Pedro: COM4 = Hub (EE05), COM10 = Node (C6).
- `firmware/hub_check` caricato sul Hub. Dal seriale: display inizializzato, aggiornamento in 2,3 s.
- Batteria Hub: lettura su GPIO1 (partitore 10k/10k), abilitazione su **GPIO39** attivo alto. Verificato sulla scheda: 2015 mV al pin, cioè 4,03 V.
- Dallo schematico EE05: D5/GPIO6 è libero, va solo al connettore.

## Non ancora provato

- **SHT41: lo scanner I2C non trova nessun dispositivo** su SDA GPIO5 / SCL GPIO6.
- Display: serve che Pedro guardi lo schermo (o una foto).
- Tasti: serve che Pedro li prema mentre leggo il seriale.

## Note

- Il firmware del test al banco che era sul Hub è stato sovrascritto con l'ok di Pedro; la copia di sicurezza via esptool era fallita due volte a circa 315 KB.
- Il nodo `entrance` non carica dal 2026-11-14 alle 21:29 UTC.

## Serve da Pedro

- Controllare il cablaggio dell'SHT41, guardare lo schermo, premere i tre tasti.
- Che fine fa `entrance`: resta, o il suo hardware passa al Node?
- `HANDOFF.md` e `Hardware/` non sono in git: vanno nel repo (pubblico) o restano solo in locale?
