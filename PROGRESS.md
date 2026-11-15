# Avanzamento

Aggiornato: 2026-11-15 · Branch di lavoro: `hub-node`

## Fase in corso

**Fase 0 · Orientamento**: riassunto e piano presentati, in attesa dell'ok di Pedro.

## Fatto

- Letti `HANDOFF.md`, il firmware del vecchio nodo, la dashboard e lo schema Supabase reale (sola lettura).
- Toolchain verificata: Arduino IDE con `arduino-cli` incluso, core esp32 3.0.7, schede XIAO C6 e S3 disponibili.
- Porte seriali lette con esptool: COM4 = ESP32-S3 16 MB, COM10 = ESP32-C6 4 MB.
- Creati `CLAUDE.md`, `PROGRESS.md` e cinque agenti in `.claude/agents/`.

## Trovato

- Il nodo `entrance` ha caricato l'ultima lettura il 2026-11-14 alle 21:29 UTC; batteria a 4,0 V, quindi non è scarico.
- Su Supabase non c'è nessuna edge function. C'è una tabella `readings_archive` (25.637 righe) che il repo non descrive.
- Manca la libreria `Seeed_GFX2`. Il core esp32 è indietro (3.0.7 contro 3.3.12).
- Il codice del test al banco ESP-NOW non è nel repo.

## Prossimo passo

Fase 1 · Hub hardware: installare `Seeed_GFX2`, scanner I2C (atteso 0x44), lettura SHT41, HelloWorld, tre tasti, pin batteria dallo schematico EE05.

## Serve da Pedro

- Ok al piano.
- Conferma: COM4 è il Hub (EE05) e COM10 è il Node?
- Che fine fa `entrance`: resta, o il suo hardware passa al Node?
- Dove sta il codice del test al banco.
- `HANDOFF.md` e `Hardware/` non sono in git: vanno nel repo (pubblico) o restano solo in locale?
