# Feature / FLASHcode generation

OpenDepot can now build a Motorola FLASHcode from selected options and packages.

## Where the FLASHcode lives

- The human-readable FLASHcode string (e.g. `500018-000488-0`) is stored in the
  `0x1006` record at payload offset 8 (15 bytes, `dddddd-dddddd-c`).
- `0x1005` also carries feature/config bytes; some bits there are package or
  model bits that are not user-selectable options.

## Encoding

12 hex nibbles = 48 4-bit slots. Each option occupies `(nibble, bit)` where
`bit` is 0..3 and the nibble value contribution is `1 << bit`.

The 13th character is a Luhn-style check digit computed by
`CanonicalFlashcode`:

```
total = sum over i in 0..11 of digitSum(i even ? 2*nibble[i] : nibble[i])
check = (10 - total % 10) % 10
```

Verified: `Q806,H35,Q53` on XTS2500 -> `100001-000100-6`, which is exactly the
flashcode on the test radio.

## Option tables (all depot radio types)

The per-platform option→bit tables were taken from the akardam.net FLASHcode
encoder (R05.03.02, options updated 2026-06-28) and are stored in:

- `tools/flashcode_options.json` — raw scraped tables
- `tools/gen_flashcode_db.py` — generator
- `src/flashcode_db.cpp` — generated C++ table (13 platforms)

Platforms: XTS5000, XTS2500, XTS1500, MT1500, PR1500, SSE5000, XTL5000,
XTL2500, XTL1500, PM1500, Astro Spectra Plus, XTS3000, Astro Saber.

The depot's Create dialog radio list maps as:

| Depot radio | Platform |
|---|---|
| XTS2500 | Astro_XTS2500 |
| XTS5000 | Astro_XTS5000 |
| SSE5000 | Astro_SSE5000 |
| XTS1500 | Astro_XTS1500 |
| MT1500 | Astro_MT1500 |
| PR1500 | Astro_PR1500 |
| XTS4000 | Astro_XTS5000 |
| XTS2000/ATS3000 | Astro_XTS3000 |

## Generation behavior

`BuildFlashcode(platform, baseFlashcode, selectedCodes)`:

1. Starts from the base flashcode (the template/loaded codeplug).
2. For every option in the platform table, sets its bit if the option code is
   selected, clears it otherwise.
3. Leaves every bit that is not an option slot untouched — this preserves
   package/model bits (often nibble10/11) from the base.
4. Recomputes the check digit.

This is why starting from a real base matters: a fresh all-zero base will not
know about package/model bits.

## Packages

The depot's package list maps to option codes and auto-includes their options:

| Package | Code | Auto-includes (portables) |
|---|---|---|
| 9600 Digital Trunk Data | Q574 | Q806, H14, H38, Q173 |
| 3600 Digital Trunk Data | Q575 | Q806, H14, H38, Q173 |
| P25 MDC Conv Data | Q811 | Q806, H35 |
| 3600 Analog Trunk Data | Q443 | H38, Q241, Q173 |
| Analog Cnv Data | Q241 | (single option) |

Selecting a package toggles its included options in the selection.

## Dependencies

Some options require others (from the akardam tooltips), e.g.:

- `Q947` requires `Q806`
- `Q498`, `G996`, `H04` require `Q806` + `Q947`
- `Q173` requires `H38`

When an option is selected, OpenDepot adds its required codes (transitively)
before rebuilding the FLASHcode and updates the checkboxes.

## GUI

`Tools -> Create Codeplug...` now has a **Features** section:

- **Packages** checkboxes (labels).
- **Add-on options** checkboxes showing `CODE  Label` for the selected radio.
- **FLASHcode** (generated) and **Base FLASHcode** fields.
- **Use as base** / **Reset to base** buttons.
- The FLASHcode updates live as anything is toggled and the check digit is
  shown/validated.

Creating the codeplug applies the generated FLASHcode through `SetFlashcode`.

## CLI

```text
opendepot-cli --flashcode-options <radio|platform>
opendepot-cli --build-flashcode <radio|platform> <base> <code,code,...>
```

Examples:

```text
opendepot-cli --flashcode-options XTS2500
opendepot-cli --build-flashcode XTS2500 000000000000 Q806,H35,Q53
opendepot-cli --build-flashcode XTS2500 500018-000488-0 Q806,H14,Q883,H38,Q173
```

## Caveat

Only the `0x1006` FLASHcode string is rewritten. If a radio requires the
`0x1005` feature/config bytes to agree, a feature change may need an additional
write of that record; validate on the test radio with a verified backup before
relying on it.
