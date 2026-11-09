# Depot Create Codeplug — recovered model

Recovered from `PatPortDepot.exe` menus/string tables and live dialog introspection.
No third-party code used.

## Command IDs (from RT_MENU bytes, little-endian)

| Action | ID |
|---|---|
| Read Device | 32789 |
| Write Device | 32790 |
| Create Codeplug | 50195 |
| Read Codeplug | 50197 |
| Update Codeplug | 50198 |
| Write Codeplug | 50200 |
| CBI Program | 50202 |
| Flash Radio | 50207 |
| File Open | 57601 |
| File Save | 57603 |
| File Save As | 57604 |

`Create/Read/Update/Write Codeplug` live under `&Tools -> Depot`.

## Dialog `Depot Create Codeplug` (command 50195)

| Control | ID | Items / notes |
|---|---|---|
| Band | 21419 | 800 MHz, VHF, UHF, 900 MHz |
| Radio | 21457 | XTS2500, XTS5000, SSE5000, XTS1500, MT1500, PR1500, XTS4000, XTS2000/ATS3000 |
| Type | 21420 | Model I, Model II, Model III, ALL |
| Model Number | 21423 | large model list, filtered by Band/Radio/Type |
| Serial Number | 21410 | free text |
| FLASHcode f1 | 21408 | text |
| FLASHcode f2 | 21404 | text |
| FLASHcode f3 | 21402 | text |
| Packages | 21461 | 3600 Analog Trunk Data, 3600 Digital Trunk Data, 9600 Digital Trunk Data, P25 MDC Conv Data, (Analog Cnv Data appears for some models) |
| Add-on checklist | 21478 | per-model list; examples recovered: Q387, H43, G857, Q52, Q53, H37, H869, Q498, H46, H04, H02, Q445, H345, Q507, Q369 |
| OK | 21406 | create |
| Cancel | 21400 | |
| Help | 21405 | |

`Add on options` (21474) toggles/enables the add-on checklist.

## Observed dependency flow

1. Dialog opens with Type = ALL, Band/Radio disabled.
2. Choosing a Type enables Band/Radio.
3. Choosing Band + Radio filters the Model Number list.
4. Choosing a Model repopulates Packages and the add-on checklist (per model/type).
5. Serial and FLASHcode fields are then entered manually (or populated by the package).

Example (test radio): UHF + XTS5000 + model `H46UCF9PW6BN` produces add-ons
Q387, H43, G857, H37, H869, Q498, Q53, H46, H04, H02, Q445, H345, Q507, Q369.

## OpenDepot plan

1. Reproduce this dialog in Dear ImGui with the recovered option lists.
2. Creation is template-based: clone a per-model template `.cpg`, then apply
   serial/model, FLASHcode, packages and add-on effects.
3. Learn option→block deltas by asking the Depot to generate reference codeplugs
   for a matrix of selections and diffing them with `tools/`.
4. Flashing: implement the Motorola RCMP/PPP transport from `NextGen10.dll`
   (`CRcmpProtocol`, `CRcmpBundler`, `CVirtualRadio`) and `UdcDrIsh10c.dll`,
   validated first with read/backup, then a gated write (never partition 0x82).
