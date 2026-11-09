# ASTRO 25 Portable Depot codeplug (`.cpg`) container — reverse-engineered notes

These notes were derived by static analysis of the shipped binaries
(`PatPortDepot.exe`, `fhAdk10c.dll`, `cgIsh10c.dll`, ...) and by black-box
comparison of two real codeplugs (`320CFP2121-backup.cpg` baseline and
`Trunking-patched.cpg`). They describe the on-disk container only. They do not
describe the higher-level CPS item schema (zones/channels/personalities), which
is a separate layer built on top of these records.

## 1. Outer file

`.cpg` is an **OLE2 Compound File (CFB / DocFile)**:

```
/AdkFileHeader       800-byte CFhAdkFileHeader
/DataStg/             storage
    Strm_0            stream directory (map)
    Strm_1 .. Strm_N  one data stream per record
```

`fhAdk10c.dll` (`CFhAdkIshArchHandler`) reads/writes this via
`StgOpenStorage` / `StgCreateDocfile` (`STGM_CREATE|STGM_READWRITE|STGM_SHARE_EXCLUSIVE`).
The map stream is literally named `Strm_0`; the storage is `DataStg`.

## 2. AdkFileHeader (800 bytes, plaintext)

Object offset = file offset + 4 (the in-memory object has a vtable pointer at +0;
`ReadFromFile` reads 800 bytes into object+4).

| file offset | meaning |
|---|---|
| 0x00 | version string `010000` |
| 0x06 | u16 (adk/serial sub-version) |
| 0x08 | flags: `adkType = b & 7`, `encType = (b & 0x18) >> 3` |
| 0x0C | 16-byte GUID |
| 0x1C | model string, e.g. `H18QDH9PW7AN` |
| 0x30 | serial string, e.g. `320CFP2121` |
| 0x40 | description / copyright |
| 0x44 | header checksum byte = `0x5A - sum(file[0..0x40]) & 0xFF` |
| 0x239 | length of crypto key blob (typically 18) |
| 0x23A | crypto key blob (custom-cipher ciphertext of a 9-byte seed) |
| 0x320 | end of header |

`encType`: `0` = legacy custom cipher, `1` = Windows CryptoAPI RC4,
`2` = no encryption. Real R20 codeplugs use type `1`.

## 3. Legacy custom cipher (CFhAdkEncrypt)

Used for the 18-byte key blob (and whole files of `encType 0`).

Ciphertext = 9-byte header + escaped XOR stream.

```
next(x) = (x * 0x55 + 0x19) & 0xFF          # PRNG
```

Header generation (given initial PRNG value `s0`, normally `time()&0xff`):
```
r1..r9  = next^9(s0)          -> header[0..8]
header[5] = next()            # b1
header[6] = next()            # b2
header[8] = next()
prng = ((b2 + b1) - header[8]) & 0xFF
ks   = next(prng)             # initial keystream
```
Stream byte `i`:
```
c = ks ^ plaintext[i]
if c in {0x1A,0x1B}: emit 0x1B, then (c==0x1A ? 0xE5 : new random not in {E5,1B,1A})
emit c
ks = (ks + next(prng) + plaintext[i]) & 0xFF
```
Decryption mirrors this: header bytes 5,6,8 seed the PRNG
(`prng = ((h6 + h5) - h8) & 0xFF`), header byte 3 participates in the 4-byte
keystream register, and the `0x1B` prefix unescapes `0xE5 -> 0x1A`, else `0x1B`.

The recovered 9-byte seed for both sample codeplugs is ASCII `A8Z6J9QP5`.

## 4. SDK crypto (CFhAdkSdkCrypto, encType 1)

Constructor:
```
CryptAcquireContext("Microsoft Base Cryptographic Provider v1.0", PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)
CryptCreateHash(CALG_MD5)
CryptHashData(md5, seed, 9)
CryptDeriveKey(CALG_RC4)           # 40-bit key = MD5(seed)[:5]
```
Each `DataStg/Strm_N` is independently RC4-encrypted with this key (fresh
keystream per stream). `ReadDataStream` decrypts the whole stream; `WriteStream`
re-encrypts the whole stream. Note: the Microsoft provider's `CALG_RC4` output
does not match textbook RC4 for the same key bytes, so OpenDepot calls the same
Windows CSP instead of reimplementing it.

## 5. Stream directory (Strm_0)

Plaintext (after decrypt): `N` 10-byte entries followed by a 2-byte checksum.

Entry (big-endian fields):
```
u32 id          virtual address / record id
u16 type        (v1) 0x0080 normally, 0x0081 for the three identity records
u16 checksum    (v2) equals the record stream's trailing u16
u16 stream      (v3) the Strm_<n> ordinal
```
Directory checksum = `(sum(entries) + 0xAAAB) & 0xFFFF`, big-endian.

## 6. Data record (Strm_N, plaintext)

```
u32 id
u16 len            == payload_len + 2
u8  payload[len-2]
u16 checksum       # compared by VerifyStreamData with the directory entry's v2
```
The record's `id` is also the directory key and the first field of the map entry.
`CFhAdkIshArchHandler::VerifyStreamData` only checks that the record trailer
equals the directory value; it does not recompute it.

## 7. Verified facts

Baseline: 664 records; patched: 709 records. All records and directory entries
verify. RC4 `decrypt` then `encrypt` reproduces every original stream byte for
byte. Directory checksum formula reproduces the stored value exactly. Header
checksum formula reproduces `file[0x44]`.

The three `0x0081` records are `0x10040000` (serial/model identity),
`0x10050000`, `0x10060000`.

## 8. Block checksums

Each record's trailing `u16` is a **CRC-16/CCITT** with polynomial `0x1021`,
initial value `0x0000`, no reflection, computed over the payload only
(`record[6 : len-2]`, i.e. excluding the 6-byte stream header and the 2-byte
checksum itself). This matches all 663 baseline and all 708 patched records.
The directory's `v2` field is a copy of this CRC.

OpenDepot recomputes the CRC for every record on save and writes the same value
into the directory, so edited blocks stay valid.

## 9. Verified against the original Depot

OpenDepot's C++ writer reproduces the baseline codeplug byte-for-byte at the
`DataStg` level (all 665 streams identical) with a freshly created OLE2
container. Opening that file in `PatPortDepot.exe` finishes loading in about
17 seconds with no error dialog. Editing the identity (serial/model in the
header and the `0x10040000` block, with the block CRC recomputed) also loads
cleanly and reaches the Depot's *Radio Information* view. A deliberately
un-updated block produced the Depot error
"Block Hardware Configuration Block [1] has an invalid checksum."
