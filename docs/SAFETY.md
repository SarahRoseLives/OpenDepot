# Safety: why OpenDepot caused FAIL 01/92, and what now prevents it

## 1. What FAIL 01/92 is

On the XTS 2500/5000, `FAIL 01/92` is the boot self-test reporting that the
**radio's codeplug identity block is missing or invalid**. The identity is:

- the `MODEL` / `SERIAL` strings in the codeplug header, and
- the three partition-`0x0081` records `0x10040000`, `0x10050000`, `0x10060000`
  (serial/model, radio config, FLASHcode).

When that block is blank or unreadable the radio answers `AT&V` with a truncated
reply (`MODEL:<garbage>`, empty `SERIAL:`), and every Depot operation fails to
open the port. It is **not** a firmware fault, which is why a FLASHport update
does not clear it.

## 2. Evidence from the incident

Artifacts left under `%TEMP%\`:

| File | Time | Records | `0x0081` identity records |
|---|---|---|---|
| `opendepot-radio\read-main.rec` | 08:15 | 425 | **present** (0x1004/0x1005/0x1006) |
| `opendepot-gui\gui-template.cpg` | 08:22 | 447 | **present** |
| `opendepot-gui\gui-read.cpg` | 08:25 | 246 | **absent** |

`read-main.rec` shows the radio was healthy at 08:15 (its `0x10040000` payload
contains `205CNX2873` and `H46UCF9PW6BN`). By 08:22 the GUI read produced
`gui-read.cpg` with **no identity records at all**, i.e. the radio had already
lost its identity block. The cause was a write between the two reads.

The recovered radio confirmed the same state: `AT&V` was truncated at
`SERIAL:` with `MODEL:<0x01>` until the good codeplug (`205CNX2873-test-radio-backup.cpg`)
was written back.

## 3. The unsafe paths that existed

`opendepot-cli --write-radio` and the GUI `Radio -> Write to Radio` did this:

1. Serialize the open codeplug's records into an ISH list.
2. Call the vendor `IRcmpBundler::WriteIshList` with that list.

There were **no checks** that the list actually contained a valid identity, and
several ways to produce a list without one:

- **Writing a codeplug that has no identity records** — e.g. the codeplug just
  read back from a radio that was already failing, or any codeplug edited with a
  tool that dropped the `0x0081` set. `WriteIshList` then leaves/sets the
  identity block blank.
- **`--erase-radio`** → `IRcmpBundler::DelCodeplug(0)`, which removes the
  codeplug including the identity, with no confirmation.
- **`--replace-radio`** → `WriteIshList` followed by `DelIshTypes`/`DelIshItem`
  for every old item not present in the new list. If the new codeplug lacked the
  `0x0081` items, the replace **deleted** them.
- **`radio_helper --deltype/--delitem`** — direct deletion of any tag, including
  the identity tags.
- **Writing a mismatched identity** (a codeplug from another model/band) with no
  comparison against the connected radio.

Two further defects made this worse:

- The GUI told the user "A verified backup is kept in `build\radio-backups\`."
  but `StartRadioJob` never made a backup — it only staged a temp copy of the
  file being written.
- A write was never verified by reading the radio back.

Any of these produces exactly the observed state: a codeplug with the identity
block blank → `FAIL 01/92`.

## 4. What OpenDepot now enforces

### 4.1 Preflight identity validation (`ValidateRadioCodeplug`)

Every radio write path now refuses a codeplug unless:

- it contains all three identity records `0x10040000/0x10050000/0x10060000`,
- each is in partition `0x0081`,
- the header `serial` and `model` are non-empty, <= 19 bytes, and both actually
  appear inside the `0x10040000` payload,
- no record is in partition `0x0082` (calibration/tuning),
- every record's length field matches its size and every record CRC verifies.

Failure returns a specific reason, e.g.:

```
REFUSED: missing identity record 0x10040000 (partition 0x0081);
writing this would blank the radio (FAIL 01/92)
```

This is tested: the codeplug that represented the failed radio state
(`gui-read.cpg`, 246 records, no `0x0081`) is refused, while the good backups
are accepted.

### 4.2 Enforced in the CLI

- `--write-radio` validates first and refuses invalid codeplugs.
- `--replace-radio` validates the **new** codeplug, so a replace can no longer
  delete the identity items.
- `--erase-radio` refuses unless `--yes-erase` is passed.
- New `--check-write <in.cpg>` runs the validator alone (exit 0 = safe,
  3 = refused) so a file can be checked before touching a radio.
- After a successful `--write-radio`, OpenDepot re-reads the identity records
  and reports whether the written serial/model is present on the radio.

Usage:

```
opendepot-cli --check-write updated.cpg
opendepot-cli --write-radio COM20 updated.cpg
```

### 4.3 Enforced in the GUI

- `Radio -> Write to Radio...` validates the open codeplug before the
  confirmation dialog and shows the identity (`serial / model`) that will be
  written. If invalid, the dialog shows the refusal and offers no Write button.
- Before writing, the GUI now performs a **real pre-write read** of the radio
  into `build\radio-backups\prewrite-<serial>-<timestamp>.cpg`. If that read
  fails, the write is aborted ("pre-write backup failed; refusing to write").
- The misleading "verified backup is kept" text was replaced with the actual
  behavior.

## 5. Operating guidance

1. **Always** run `--check-write` (or open the file in the GUI) before writing.
   If it says `REFUSED`, fix the codeplug; do not force it.
2. Keep a known-good backup (`build\radio-backups\`). A codeplug read from a
   radio that is already in FAIL has no identity and must never be written back.
3. Do not use `--erase-radio`, `--deltype`, `--delitem`, or `--replace-radio`
   casually. They remain available but are gated/validated now.
4. If a radio is already in FAIL 01/92, recover it by writing a **verified good
   codeplug** for that exact model/serial (this is what restored 205CNX2873).

## 6. Verification performed

- Release build succeeds (`OpenDepot.exe`, `opendepot-cli.exe`).
- `--check-write build\radio-backups\205CNX2873-test-radio-backup.cpg`
  → `OK: safe to write (identity 205CNX2873/H46UCF9PW6BN, 139 records)`.
- `--check-write %TEMP%\opendepot-gui\gui-template.cpg`
  → `OK: safe to write (identity 205CNX2873/H46UCF9PW6BN, 447 records)`.
- `--check-write %TEMP%\opendepot-gui\gui-read.cpg`
  → `REFUSED: missing identity record 0x10040000 ... FAIL 01/92` (exit 3).
