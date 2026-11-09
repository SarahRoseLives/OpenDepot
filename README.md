# WARNING — THIS SOFTWARE CAN BRICK YOUR RADIO

> **OpenDepot can permanently brick your radio. Use it at your own risk.**
>
> This is **experimental, reverse-engineered software**. A write can leave a radio
> permanently corrupted (for example a blank or mismatched identity block causing
> `FAIL 01/92`), and an interrupted or incorrect write can leave it unbootable.
>
> - It has **only been tested on a single XTS 2500**. Every other model/band is
>   untested on real hardware.
> - **Firmware flashing is NOT supported.** A radio whose firmware failed to flash
>   cannot be repaired by OpenDepot (codeplugs only).
> - **Always keep a verified backup** and run `--check-write` before writing.
>   **Never force** a write that OpenDepot refuses.
> - Do not use it on a radio you cannot afford to lose.
>
> Provided **as is**, with **no warranty** of any kind. You assume all risk.
> OpenDepot is not affiliated with, endorsed by, or supported by Motorola
> Solutions.

# OpenDepot

Clean-room C++ reimplementation of the ASTRO 25 codeplug layer, with a Dear ImGui
GUI and a CLI. It reads, edits, creates and writes Motorola ASTRO 25 `.cpg`
codeplugs, and reads/writes a connected radio over its serial/PPP programming
link — without needing the Depot for the read or the codeplug write.

## Current state

Experimental / alpha. The container layer and radio transport work, but the
higher-level CPS item model (building zones/channels/personalities/talkgroups
from scratch) is **not** implemented — creation works by cloning a same-model
built-in schema and then applying identity and FLASHcode.

Working today:

- Decode the `.cpg` OLE2/ADK container (header, encrypted data streams, stream
  directory) and verify every record against the directory and CRC-16/CCITT.
- Re-encode into a new OLE2 container (RC4 via the same Windows CSP the vendor
  uses) so the original Depot and the radio accept it.
- Read/edit identity (serial/model), FLASHcode, zone and channel names, and
  channel frequencies.
- Read the FLASHcode option map and rebuild a valid FLASHcode from selected
  packages/add-ons (option tables for 13 ASTRO 25 platforms).
- **Read from a radio** with a native reader (no vendor DLL): identity is read
  and the radio is returned to normal mode.
- **Write to a radio** with a standalone raw writer (no Depot, no vendor DLL):
  the full record set is written, the codeplug is committed, and the written
  identity is verified by a read-back.
- **Create a codeplug from a built-in schema** for the models in the built-in
  model database (see below), with or without a template file.
- Convert a codeplug to conventional operation, from a template or a reference.

Not implemented:

- Building zones/channels/personalities/talkgroups from scratch.
- Any firmware/boot flash. A radio that failed a firmware flash cannot be
  recovered here.

## What it is used for

- Inspect and verify an existing `.cpg` (records, CRC, directory, header).
- Change serial/model, FLASHcode, zone/channel names and frequencies.
- Generate a codeplug for a supported model and write it to a radio.
- Back up and restore a codeplug.
- Recover a radio whose codeplug identity block is damaged, by writing a
  verified, matching codeplug back.
- Read, set, or clear the codeplug read/write **passcode** (record
  `0x00510000`), entirely from the codeplug.
- Copy a whole zone (channel names, frequencies/selections, per-channel
  personalities and entries) from one codeplug into a zone of another with
  `--copy-zone`.

## Supported models (built-in model database)

A codeplug can be generated without a template for these built-in schemas.
Only the **800 MHz XTS 2500** has been tested on hardware; the rest are
untested — treat them as experimental.

| Model (example) | Radio | Band | Tested |
|---|---|---|---|
| `H46UCF9PW6BN` | XTS 2500 | 800 MHz | **yes** |
| `H46WCF9PW6BN` | XTS 2500 | 900 MHz | no |
| `H46QDD9PW5BN` | XTS 2500 | UHF | no |
| `H46KDD9PW5BN` | XTS 2500 | VHF | no |
| `H18UCC9PW5AN` | XTS 5000 | 800 MHz | no |
| `H66UCC9PW5AN` | XTS 1500 | 800 MHz | no |
| `H67UCC9PW5AN` | MT 1500 | 800 MHz | no |
| `H10SDD9PW6AN` | SSE 5000 | 800 MHz | no |
| `H79KDC9PW5AN` | PR 1500 | 800 MHz | no |
| `H18KCN9PW9AN` | XTS 4000 | 800 MHz | no |

Models are matched by their model number; unrelated variants may share some
records but have not been validated.

## Tested hardware

- **One XTS 2500 (Model II), model `H46UCF9PW6BN`, 800 MHz trunking, Host/DSP
  R17.00.02.**
- No other model has been written to or read from a real radio.
- Mobile radios are present in the model database for generation only and are
  **not** validated for the radio read/write path.

## Limitations

- **Codeplug layer only.** No firmware, no boot flash, no repair of a failed
  firmware update.
- **No from-scratch channel/personality/talkgroup authoring.** Creation clones a
  built-in or template codeplug, then changes identity/flashcode.
- **Writes are wholesale.** A write sends the entire record set and commits;
  a partial/failed write can corrupt the radio.
- **Reverse-engineered protocol.** The auth and write/commit sequence were
  recovered from observed traffic; a different firmware revision may behave
  differently.
- **Windows only.** Uses the Windows CryptoAPI for RC4 and a Win32/D3D11 GUI.
- **Untested breadth.** Only one radio model/band has been exercised end to end.
- **Read-back after write** verifies the identity only, not every record.

## Before you write anything

1. Read the radio/back up first, and keep a **verified** codeplug for the exact
   model/serial.
2. Run `--check-write <file>` (or open it in the GUI). If it says `REFUSED`, fix
   the codeplug — do not force it.
3. Understand the failure modes: a blank/mismatched identity causes `FAIL 01/92`;
   a firmware flash failure is **outside** OpenDepot's ability to repair.
4. See `docs/SAFETY.md` for the incident analysis and the preflight rules.

## Build

Requires Visual Studio 2022 (C++ workload). The `.NET Framework 4` / `csc.exe`
helper (`tools/radio_helper.cs` → `radio_helper.exe`) is only needed for the
legacy `--erase-radio` / `--replace-radio` paths; native read and raw write do
not use it.

```powershell
$cm = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cm -S . -B build\vs -G "Visual Studio 17 2022" -A x64
& $cm --build build\vs --config Release
```

Optional helper (erase/replace only):

```powershell
& "C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe" /nologo /platform:x86 `
  /r:"C:\Windows\SysWOW64\Interop.NEXTGENLib.dll" `
  /out:tools\radio_helper.exe tools\radio_helper.cs
Copy-Item tools\radio_helper.exe,tools\Interop.NEXTGENLib.dll build\vs\Release\ -Force
```

Outputs:

- `build\vs\Release\OpenDepot.exe` — GUI
- `build\vs\Release\opendepot-cli.exe` — console

## CLI

```text
opendepot-cli --verify <in.cpg>
opendepot-cli --dump <in.cpg>
opendepot-cli --records <in.cpg>
opendepot-cli --clone <in.cpg> <out.cpg>
opendepot-cli --models
opendepot-cli --zones <in.cpg>
opendepot-cli --set-zone-name <in.cpg> <out.cpg> <zoneIndex> <name>
opendepot-cli --set-channel-name <in.cpg> <out.cpg> <zoneIndex> <chanIndex> <name>
opendepot-cli --set-channel-freq <in.cpg> <out.cpg> <zoneIndex> <chanIndex> <rxMHz> <txMHz>
opendepot-cli --set-identity <in.cpg> <out.cpg> <serial> <model>
opendepot-cli --password <in.cpg>
opendepot-cli --set-password <in.cpg> <out.cpg> <password>
opendepot-cli --clear-password <in.cpg> <out.cpg>
opendepot-cli --radio-password <port>
opendepot-cli --flashcode <in.cpg> [radio|platform]
opendepot-cli --set-flashcode <in.cpg> <out.cpg> <flashcode>
opendepot-cli --flashcode-options <radio|platform>
opendepot-cli --build-flashcode <radio|platform> <base> <code,code,...>
opendepot-cli --create <template.cpg> <out.cpg> <serial> <model> <flashcode>
opendepot-cli --create-new <out.cpg> <model> <serial> [flashcode]
opendepot-cli --conventional <in.cpg> <out.cpg>
opendepot-cli --conventional-ref <ref.cpg> <src.cpg> <out.cpg>
opendepot-cli --create-conventional <template.cpg> <out.cpg> <serial> <model>
opendepot-cli --rec <in.cpg> <out.rec>
opendepot-cli --from-rec <template.cpg> <in.rec> <out.cpg>
opendepot-cli --taglist <in.cpg> <out.bin>
opendepot-cli --ishlist <in.cpg> <out.bin>
opendepot-cli --read-radio <port> <out.cpg>
opendepot-cli --read-radio <port> <template.cpg> <out.cpg>
opendepot-cli --check-write <in.cpg>
opendepot-cli --write-radio <port> <in.cpg>
opendepot-cli --erase-radio <port> [--yes-erase]
opendepot-cli --replace-radio <port> <old.cpg> <new.cpg>
opendepot-cli --report <in.cpg> [--json]
opendepot-cli --radio-report <port> [--json]
opendepot-cli --doctor <in.cpg> [<out.cpg>]
opendepot-cli --diff <a.cpg> <b.cpg> [--json]
opendepot-cli --copy-zone <src.cpg> <srcZone> <dst.cpg> <dstZone> <out.cpg>
opendepot-cli --backup-radio <port> <outdir>
```

- `--verify` returns 0 when every record verifies, 3 when any record is bad.
- `--check-write` returns 0 when safe to write, 3 when refused.
- `--read-radio` reads natively; with no template it derives the header basis
  from a built-in schema and syncs identity from the radio.
- `--write-radio` validates first, writes the whole record set, commits, then
  re-reads the identity to verify it.
- `--erase-radio` and `--replace-radio` are destructive and gated; they use the
  legacy vendor helper. Do not use them casually.

## GUI

- **File** → Open `.cpg`, Save As, Exit.
- **Tools** → Create Codeplug…, Re-verify.
- **Radio** → port dropdown (enumerated live), Read from Radio… (works with no
  codeplug open), Write to Radio… (validates, shows the identity, confirms),
  Cancel.

The Create dialog takes Band / Radio / Type / Model / Serial / FLASHcode and
optional packages/add-ons. When a codeplug is open, it prefills from the read
(serial, model, FLASHcode) and derives band from the first channel frequency.

## Codeplug passcode

The codeplug read/write passcode is stored as codeplug record **`0x00510000`**
(partition `0x80`): `payload[0]` = enabled flag, `payload[1..]` = ASCII password,
zero padded. That is exactly what the vendor `CRcmpBundler::GetRadioPassword`
reads (a read of partition `0x80` / tag `0x51` / instance `0`). OpenDepot reads
and edits the record directly from the codeplug; no vendor DLL is involved.

- `--password <in.cpg>` — show the enabled flag and passcode.
- `--set-password <in.cpg> <out.cpg> <password>` — set it.
- `--clear-password <in.cpg> <out.cpg>` — clear it.
- `--radio-password <port>` — native read of the radio's passcode (does a full
  read and parses record `0x00510000`).
- GUI: **Radio → Read Passcode from Radio / Set Passcode… / Clear Passcode**.

Set/clear only changes the codeplug; **Save As** or **Write to Radio** applies
it. Whether a radio with a passcode set still allows this native read is
untested; if it blocks the read, recovery needs a codeplug that was read while
the radio was unlocked.

## Memory / calibration backup (read-only)

`opendepot-cli --backup-radio <port> <outdir>` (GUI: **Radio → Backup
(memory/calibration)…**) creates a folder with:

- `codeplug.cpg` — native, CRC-verified read of the codeplug.
- `softpots.bin` — tuning softpot values, read via the vendor API (read-only).
- `info.txt` — processor type, version attempt, ESN attempt, security-partition
  attempt, and the softpot dump.
- `security.bin` — security/calibration partition, when the API returns it.
- `manifest.json` — SHA-256 and size of each artifact.

It is read-only and never writes to the radio. Known limitations: outside the
Depot the security-partition calls (`GetSecurityPartitionSize/Data`) currently
fail with "Class not registered", and firmware/flash memory is not dumped yet
(the address map and boot-mode access are still being worked out). The codeplug
and softpot tuning values are captured.

## Service report, doctor, diff

Read-only analysis tools (CLI):

- `--report <in.cpg> [--json]` — model, serial, FLASHcode + decoded features,
  passcode status, zone/channel summary and record counts.
- `--radio-report <port> [--json]` — native read of the radio; includes the
  `AT&V` banner and `HOST`/`SERIAL` from the handshake (Host/DSP).
- `--doctor <in.cpg> [<out.cpg>]` — checks the header checksum, directory
  checksum, per-record length/CRC and identity. With an output path it repairs
  them, re-syncs identity and writes a fixed codeplug; exit 3 if unhealthy.
- `--diff <a.cpg> <b.cpg> [--json]` — identity, FLASHcode, records by tag
  (added/removed/changed) and zone/channel differences.

GUI: **Tools → Service Report… / Codeplug Doctor… / Repair checksums/identity /
Diff with file…**. These work on the open codeplug or, when none is loaded, on a
`.cpg` you pick.

## Radio recovery (FAIL 01/92)

If a radio's identity block is blank it cannot be read normally, so the tools
are not reachable by reading it. Recovery uses a **verified backup** for that
exact model: **Radio → Recover from backup (write)…**. It validates the backup,
skips the pre-write read (which would fail on a dead radio), writes the full
record set, and verifies the identity afterwards. Always keep the backup.

## Radio transport (recovered)

Programming is exposed by the radio as an AT modem, then PPP, then RCMP:

1. Open the port **9600 8N1 with RTS/CTS**; send `AT&V` — the radio reports
   `CARRIER`, `PROTOCOL:ALT`, IP `192.168.128.1`.
2. Send `AT#DATLCL=1`, then `AT+IPR=115200`; the port switches to 115200.
3. Send `CLIENT`; the radio replies `CLIENTSERVER`.
4. PPP (LCP/IPCP; host `192.168.128.2`, radio `192.168.128.1`).
5. RCMP over PPP: read `0x07`, write `0x10`, ISH items
   `tag/instance/partition/length/payload`, with the recovered authentication
   challenge/response.
6. Write sequence: begin, authenticate, enter program mode, write all records
   (identity first, directory last), commit (reorg), end.

Read uses the native reader; write uses the raw writer. `NextGen10.dll` /
`radio_helper.exe` are only used by `--erase-radio` / `--replace-radio`.
`docs/RCMP_INTERNAL.md` has the frame details.

## Layout

- `src/adk.*` — container, ADK cipher, RC4 (CryptoAPI), CRC, codeplug model
- `src/model_db.*` — generated built-in per-model schemas
- `src/flashcode_db.cpp` — generated per-platform FLASHcode option tables
- `src/main_gui.cpp` — Dear ImGui + Win32 + Direct3D 11 UI
- `src/main_cli.cpp` — console commands
- `tools/radio_tcp.py` — native reader and raw writer (PPP/RCMP, auth)
- `tools/gen_model_db.py` — generates `src/model_db.cpp` from reference codeplugs
- `tools/flashcode_options.json` / `tools/gen_flashcode_db.py` — FLASHcode tables
- `tools/radio_helper.cs` — legacy 32-bit vendor-driver helper (erase/replace)
- `docs/ADK_FORMAT.md` — container/crypto/CRC notes
- `docs/WRITE_PROTOCOL.md`, `docs/RCMP_INTERNAL.md` — radio protocol notes
- `docs/FEATURES.md`, `docs/CREATE_CODEPLUG.md` — FLASHcode/creation notes
- `docs/SAFETY.md` — FAIL 01/92 analysis and write guardrails
- `third_party/imgui` — vendored Dear ImGui

## Disclaimer and license

- **No affiliation.** OpenDepot is an independent project. It is not affiliated
  with, endorsed by, or supported by Motorola Solutions.
- **No warranty.** The software is provided "as is", without warranty of any
  kind, express or implied. The authors are not liable for any damage to radios,
  data, or equipment. You use it entirely at your own risk.
- **Clean room.** The container, generator and CRC logic are original. Radio I/O
  uses the Windows CryptoAPI and the recovered protocol; the optional
  erase/replace helper uses the vendor driver already installed on the machine.
- See `LICENSE` for the license terms.
