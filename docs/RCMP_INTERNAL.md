# NextGen10 / RCMP internal protocol — clean-room notes

Evidence from `NextGen10.dll` (SHA-256 `99fa3efb...`) via Ghidra 12.1.4.
No third-party code or notes used.

## Key functions (RTTI/trace strings → function via xref)

| Trace string | Function |
|---|---|
| `CRcmpProtocol::SendRequest` | `0x10018123` |
| `CRcmpProtocol::BundleTCP` | `0x100187b6` |
| `CRcmpBundler::WriteIshList` | `0x1000ab20` |
| `CRcmpBundler::EnterProgramMode` | `0x10011a98` |
| `CRcmpBundler::SendCommand` | `0x1000b5b0` |
| `CRcmpBundler::populateIshLetFromArray` | `0x1000bd6f` |
| `CRcmpBundler::ReadWriteIshLetToRadio` | `0x1000beb6` |
| `CRcmpBundler::Read/WriteIshList` | `0x1000ab20` |

## Framing (BundleTCP, 0x100187b6)

Payload is shifted right by two bytes and prefixed with its **big-endian u16
length**; the length counter is increased by 2.

```
[u16_be length][payload...]
```

This is the RCMP record framing.

## SendRequest (0x10018123)

- Copies the SAFEARRAY bytes into a send buffer.
- If the record begins `01 01`, updates a running counter from bytes 7..8.
- Sends in chunks of up to `0x410` (1040) bytes.
- Timeout argument initialized to 2000 ms.
- Calls `BundleTCP` for command classes 4, 8, 9, 10, 0xC, 0xD.

## Commands

`ReadWriteIshLetToRadio` (0x1000beb6) selects command **0x22** (read) or
**0x23** (write) of an ISH item and requires the reply's first payload byte to
be `'G'` (0x47) for success. `SendCommand` (0x1000b5b0) builds the record from
up to 7 fields into a SAFEARRAY and sends it via `FUN_1000ba53`.

The first two bytes are the command/opcode; subsequent fields are
length/tag/instance/payload with small type markers.

## ISH item wire/record shape (0x1000bd6f)

Reads four big-endian u16 values then the payload:

```
u16 tag
u16 instance
u16 flags/partition
u16 length
u8  payload[length]
```

This matches the `.cpg` `Strm_N` record exactly.

## Write session (WriteIshList, 0x1000ab20)

- Walks a SAFEARRAY of items.
- Calls the per-item write (`FUN_1000bd6f`) and `ReadWriteIshLetToRadio`.
- Progress uses 0x5A (90-byte) units.

## Transport

- `NextGen10` uses COM (`CoCreateInstance`) for its protocol layers and a
  serial/TCP transport.
- Strings: `Radio PPP Timed Out`, `CRcmpProtocol::OpenPort`,
  `CRcmpProtocol::CopyDataToArray`, `CRcmpProtocol::BundleTCP`.
- `UdcDrIsh10c.dll` is the device layer: `OpenRadio`, `GetCodeplugFromDevice`,
  `WriteToDevice`, `IVirtualRadio`.

## OpenDepot implementation targets

1. Open COM20 with the correct DCB (baud/flow control from `AppGetBaudRate` and
   `PortManager10`/`RadioSerial10`).
2. PPP LCP/IPCP, then TCP to `192.168.128.1:8002` (or the direct RCMP transport
   the RDK stack uses for this cable).
3. RCMP: big-endian length framing, command 0x22/0x23 for ISH read/write,
   prepare/erase and program-mode commands from `SendCommand` callers.
4. Read/backup first; write only partitions 0x80/0x81/0x85; never 0x82.

## Captured Depot session (COM20, XTS5000 H46UCF9PW6BN)

Captured with the Depot's serial monitor and with a com0com bridge. This is the
authoritative sequence used by `PatPortDepot.exe`:

1. `AT&V` at **9600 8N1, RTS/CTS**. The radio replies:
   `CARRIER:9600`, `PROTOCOL:ALT`, `COMPRESSION:NONE`, `FLOW:K3`,
   `WV304:192.168.128.1`, `RADIO:XTS 2500`, `MODEL:H46UCF9PW6BN`,
   `SERIAL:205CNX2873`, `PORT:SIDE CONNECTOR AS RS232`, `SECURE:R000000`,
   `CPS:HARDWARE FLOW CONTROL`, `OK`.
2. Close/reopen, `AT#DATLCL=1` → `OK`.
3. Close/reopen, `AT+IPR=115200` → `OK`; the port changes to **115200**.
4. Close/reopen, send `CLIENT`; radio replies `CLIENTSERVER`.
5. PPP: LCP configure/ack (magic `0x00002af3`/`0xdd95c234`), IPCP with host
   `192.168.128.2` and radio `192.168.128.1`.
6. RCMP runs over **PPP protocol `0x2d`** (not TCP). Each frame after the PPP
   FCS/unescape is:

   ```
   request : 0c seq(2) cmd flags(2) len(1) data[len]
   response: 1c seq(2) cmd flags(2) len(1) data[len]
   ```

   - read request `cmd=0x07`, read response `cmd=0x0d`
   - write request `cmd=0x10`, write response `cmd=0x3d`
   - data header for an ISH item: `partition(2) tag(2) instance(2)
     length(2) offset(4) payload[length]`
   - ISH list entry (for ReadIshList/WriteIshList):
     `tag(2) instance(2) partition(2) length(2) payload[length]`
7. On close the depot drops the port; the radio returns to normal operation.

The vendor `VirtualRadio`/`RcmpBundler` driver in `NextGen10.dll` implements
steps 1-6 and was used by the OpenDepot 32-bit helper. The CLI/GUI read uses
`ReadAllTypeInstances` per ISH type in a single session (with a separate session
for `0x0FFF`), and the write uses one `WriteIshList` call with the full list.

