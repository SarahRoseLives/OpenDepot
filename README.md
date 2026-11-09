# OpenDepot

Clean-room C++ reimplementation of the ASTRO 25 Portable Depot codeplug layer,
built with Dear ImGui. It reads, creates and writes Motorola ASTRO 25 `.cpg`
codeplugs without using third-party code.

## Status

Working:

- Decode the `.cpg` OLE2/ADK container (header, encrypted data streams,
  stream directory) and verify every record against the directory and CRC.
- Re-encode a codeplug into a new OLE2 container (RC4 via the same Windows CSP
  the vendor uses) so the original Depot accepts it.
- Edit identity (serial/model), FLASHcode, zone/channel names and frequencies.
- **Create Codeplug** dialog (`Tools -> Create Codeplug...`) with the recovered
  Band/Radio/Type/Model/Serial/FLASHcode/Packages/Add-ons model. Creation clones
  a same-model template, then applies identity and FLASHcode.
- **Read from Radio / Write to Radio** (`Radio` menu), using the installed
  Motorola ADK radio driver (`NextGen10.dll` `VirtualRadio`) through a 32-bit
  helper. Validated end-to-end on an XTS5000 (H46UCF9PW6BN, serial 205CNX2873):
  447/447 records read, written back, and re-read with zero mismatches.
- **Feature / FLASHcode generation**: select packages and add-on options and
  OpenDepot builds the correct FLASHcode (option tables for 13 Astro 25
  platforms, package auto-includes and option dependencies). See
  `docs/FEATURES.md`.

Not yet implemented:

- Creating zones/channels/personalities/talkgroups from scratch (higher-level
  CPS item schema). The container and radio transport are ready for it.

## Build

Requires Visual Studio 2022 (C++ workload) and .NET Framework 4 `csc.exe`
(for the radio helper).

```powershell
$cm = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cm -S . -B build\vs -G "Visual Studio 17 2022" -A x64
& $cm --build build\vs --config Release
```

The GUI/CLI look for `radio_helper.exe` next to themselves. Build and deploy it:

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
opendepot-cli --create <template.cpg> <out.cpg> <serial> <model> <flashcode>
opendepot-cli --rec <in.cpg> <out.rec>
opendepot-cli --from-rec <template.cpg> <in.rec> <out.cpg>
opendepot-cli --taglist <in.cpg> <out.bin>
opendepot-cli --ishlist <in.cpg> <out.bin>
opendepot-cli --read-radio <port> <template.cpg> <out.cpg>
opendepot-cli --check-write <in.cpg>
opendepot-cli --flashcode-options <radio|platform>
opendepot-cli --build-flashcode <radio|platform> <base> <code,code,...>
opendepot-cli --write-radio <port> <in.cpg>
opendepot-cli --set-identity <in.cpg> <out.cpg> <serial> <model>
```

`--verify` returns 0 when every record verifies, 3 when any record is bad.
`--read-radio` uses the template for the ISH/tag list and the header/seed.
`--write-radio` writes every record in the given codeplug.

## GUI

File -> Open `.cpg`. Inspect header, records and raw record bytes. On the
Overview tab set Serial/Model, press **Apply identity**, then **Save As**.
The block CRC and directory checksum are recomputed on save.

- `Tools -> Create Codeplug...` builds a codeplug from the recovered Depot
  dialog model.
- `Radio -> Read from Radio...` reads the radio on the port (default `COM20`)
  and replaces the open codeplug.
- `Radio -> Write to Radio...` confirms, then writes the open codeplug.

## Radio transport (recovered)

Captured from `PatPortDepot.exe` itself:

1. Open the port at 9600 8N1, RTS/CTS; send `AT&V` — the radio reports
   `CARRIER:9600`, `PROTOCOL:ALT`, `FLOW:K3`, IP `192.168.128.1`.
2. Send `AT#DATLCL=1`, then `AT+IPR=115200`; the port switches to 115200.
3. Send `CLIENT`; the radio replies `CLIENTSERVER`.
4. PPP (LCP/IPCP; host 192.168.128.2, radio 192.168.128.1).
5. RCMP over PPP protocol `0x2d`: read opcode `0x07`, write opcode `0x10`,
   ISH items `tag/instance/partition/length/payload`.

`docs/RCMP_INTERNAL.md` has the frame details.

## Verifying with the original Depot

`tools\depot_open_test.ps1 -File <generated.cpg> -WaitSeconds 45` launches
`PatPortDepot.exe` with the file, polls its windows, and reports any dialogs.
A good codeplug reaches the title `ASTRO 25 Portable Depot - <name>` and shows
the Radio Information tree with no error dialog. Loading takes roughly
15-20 seconds on the test machine.

## Layout

- `src/adk.*` — container, ADK custom cipher, RC4 (CryptoAPI), CRC, codeplug model
- `src/flashcode_db.cpp` — generated per-platform FLASHcode option tables
- `src/main_gui.cpp` — Dear ImGui + Win32 + Direct3D 11 UI
- `src/main_cli.cpp` — console commands
- `tools/radio_helper.cs` — 32-bit bridge to the installed Motorola radio driver
- `tools/flashcode_options.json` — scraped option tables; `tools/gen_flashcode_db.py` generates the C++
- `tools/com0com_proxy.py` — virtual-COM logging bridge used to capture the Depot
- `docs/ADK_FORMAT.md` — container/crypto/CRC notes
- `docs/RCMP_INTERNAL.md` — radio protocol notes
- `docs/FEATURES.md` — FLASHcode/feature model and generation
- `docs/SAFETY.md` — FAIL 01/92 root cause and write guardrails
- `third_party/imgui` — vendored Dear ImGui

## Scope and safety

OpenDepot's container, generator and CRC logic are clean-room. Radio I/O uses
the Motorola ADK driver already installed with the Depot; no third-party
codeplug tooling is used.

Every radio write is guarded: `ValidateRadioCodeplug` refuses any codeplug whose
partition-`0x0081` identity records (`0x10040000/0x10050000/0x10060000`) are
missing or inconsistent, that contains calibration partition `0x0082`, or that
fails a length/CRC check. Writing such a file is what produces `FAIL 01/92`
(a blank MODEL/SERIAL identity block). `--check-write` runs the same check
without touching a radio; `--erase-radio` requires `--yes-erase`; the GUI reads
a real pre-write backup into `build\radio-backups\` and refuses to write if that
read fails. See `docs/SAFETY.md` for the incident analysis.

Keep a verified backup before writing:
`build\radio-backups\205CNX2873-test-radio-backup.cpg`.
