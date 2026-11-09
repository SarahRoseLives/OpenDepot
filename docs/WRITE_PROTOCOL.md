# Codeplug write protocol — reverse-engineering notes

Status: **SOLVED — OpenDepot flashes standalone, no depot.**
Verified live on the XTS 2500 (205CNX2873): full codeplug + feature change wrote
and persisted (`100001-000100-6` -> `100001-000300-4`, Q387), identity intact,
all readback records CRC-OK.

## Working standalone flash recipe (`tools/radio_tcp.py --write-cpg <cpg> --all`)
1. `bringup`: `AT&V`, `AT#DATLCL=1`, `AT+IPR=115200`, `CLIENT`, LCP/IPCP.
2. RCMP: `01 06 00` begin, `01 04 85 12 bf 00 00 00 00`, `00 0f 00`.
3. Auth: `03 00` -> challenge; response = LFSR transform (section 1); `03 01 <resp>`.
4. `01 08 81` program mode; config `01 02 80 00 47 00 00 00 01`,
   `01 03 80 00 02 01 f4`, `01 03 80 0e 0b 0f ff`, `01 07 80 00`.
5. **Write the ENTIRE record set** (identity 0x1005/0x1004/0x1006 first, `0x0047` last)
   as `01 01 <part><tag2><inst2><len16><len32><data>`. Writing only a subset
   leaves the ISH directory inconsistent -> FAIL 01/02.
6. **Reorg commit: `01 07 80 00`** (`CRcmpBundler::Send ReOrg`, function 0x1001773c).
7. **End: `01 06 01`** -> radio returns to normal with the new codeplug.

Transport notes: TCP over PPP, payload `[u16 len][RCMP body]`; receive must be
TCP sequence-aware (dedupe retransmissions/overlap) or the length-prefixed
stream desyncs on long writes.

The CLI/GUI use this path: `opendepot-cli --write-radio <port> <cpg>` ->
`tools/radio_tcp.py <port> --write-cpg <cpg> --all`.

Historical notes below (recovery of earlier failed experiments:
COM21<->COM22 bridge + depot Write Codeplug).

## 1. Radio auth (SOLVED, verified live)
`C:\Windows\SysWOW64\NextGen10.dll`
- `CRcmpBundler::UnlockSecurity` -> vtable +0x60 (get challenge) ->
  **`FUN_1000fd89`** (compute response) -> +0x5c (send response).

`FUN_1000fd89` (32-bit LFSR, seeded once per auth):
```
state = 0x51E88D21
for k in 0..31:
    b = challenge[k]; rev = challenge[31-k]
    for bit in 7..1:                         # 7 clocks/byte, bit 0 skipped
        fb = ((rev>>bit)&1) ^ ((state>>9)&1)
        state = (state*2 + fb) & 0xFFFFFFFF
        b ^= (fb<<bit)
    response[k] = b
```
Verified against 3 captured challenge/response pairs and live
(`03 00` -> `83 00 00 <32>`; `03 01 <resp>` -> `83 01 00`).
Implementations: `tools/radio_tcp.py::auth_response`.

## 2. RCMP wire (from depot captures)
- Transport: serial 9600 -> PPP -> TCP 8001/8002; PPP proto 0x2d also seen.
- Body: `[01 req/81 reply][opcode][sub][args]`.
- Begin `01 06 00`; end/commit `01 06 01`.
- Program mode `01 08 81`; config `01 02 80 00 47 00 00 00 01`,
  `01 03 80 00 02 01 f4`, `01 03 80 0e 0b 0f ff`, `01 07 80 00`.
- Query `01 02 <part><tag2><inst2><00 00 00 01>`.
- Write `01 01 <part><tag2><inst2><len16><len32><data>`.
- Auth `03 00` / `03 01 <32>`.

## 3. Depot full-write sequence (build\out\vj_write.txt)
Depot writes every record (queries + writes), ending with the directory record
`tag=0047 inst=0000 len=46` (e.g. `261107075703002400...`), then `01 06 01`.
That `0x0047` reorg is what commits the codeplug. Writing records without it
leaves the ISH directory inconsistent -> FAIL 01/02. Writing the wrong layout
drops identity -> FAIL 01/92.

## 4. Vendor COM API (Interop.NEXTGENLib.dll, all in NextGen10.dll)
- `IVirtualRadio` (IID 08B74B8F..., CLSID CDC45D7E...):
  `Open`, `ReadCodeplug`, `WriteCodeplug`, `WriteToRadio`, `NetWriteCodeplug`,
  `GetBundlerPointer`, `ReadRadioKey`, `UnlockSecurity`, ...
- `IRcmpBundler` (06E9DD53...): `Open`, `WriteIshList`, `ReadAllTypeInstances`,
  `EnterProgramMode`, `ResetRadio`, `UnlockSecurity`, ...
- `INetRcmpBundler` (BFE38CDC...): adds `NetOpen`, `NetWriteIshList`.
- `IRcmpProtocol` (ED5C93C2...): `OpenPort`, `SendRequest`, `GetReply`
  (CLSID CDC45D76...).

Findings:
- Basic `IRcmpBundler::WriteIshList` (our ISH list) is accepted (hr=0) and
  preserves identity, but does not persist (no reorg/commit).
- `IVirtualRadio::WriteToRadio(<ISH list>)` is WRONG: it expects the ADK
  codeplug block, and writing the ISH list wiped identity (FAIL 01/92).
- `IVirtualRadio::ReadCodeplug` needs a valid session/version; our call returned
  `0xC0001013` (the `CVirtualRadio::Open` `SupportedCpgVersion` was 0).
- `CVirtualRadio::Open` selects transport from the **port-name prefix**
  (`COM`/`TCP`/...), not the `protocol` argument.

## 5. Remaining work for direct flashing
1. Produce the depot's codeplug block (the 700-byte SAFEARRAY built in
   `CVirtualRadio::WriteCodeplug`, `FUN_1001c92d`) from the `.cpg` using the
   ADK (`fhAdk10c.dll` / `UDCISH10C.dll`), then call `WriteCodeplug`
   (which performs the `0x0047` reorg).
2. Open `IVirtualRadio` with the real `SupportedCpgVersion` / `SupportedSysRelVersion`
   / `UUIDList` (not 0) so `ReadCodeplug`/`WriteCodeplug` pass their checks.

## 6. Safety
- `radio_helper --write` is left on the basic `WriteIshList` (non-destructive,
  no persistence). Do NOT use `WriteToRadio` with the ISH list.
- Recovery of FAIL 01/92: `Desktop\01 92 FAIL Recovery\xts2500_identity_bridge.py`
  (COM22<->COM20) + depot COM21 Write Codeplug of
  `build\radio-backups\205CNX2873-test-radio-backup.cpg`.
