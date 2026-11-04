# Corsa-e HS CAN 1 database

For the separate 125 kbit/s infotainment/telematics bus, use the [INFO-DIV database and notes](README-info.md). That export covers the full source bus and has no vehicle capture validation yet; do not mix it with HS1 by matching CAN IDs.

Load **[opel_corsa_e_hscan1.dbc](opel_corsa_e_hscan1.dbc)** for OBD pins 6/14 traffic. This now includes all three CSV captures in `logs/`: READY, MyOpel wake-up and sleep-to-unlock. It replaces the earlier reviewed subset with a **database-first research baseline**.

- **89 observed identifiers / 181,002 frames**, each with its observed DLC.
- **84 messages with database fields**, five raw-only because no HS1 definition exists: `25A`, `352`, `3FA`, `494`, `4A2`.
- **1,161 signals**, including source-reserved fields and raw coverage; these are not all independently validated physical quantities.
- Message names start with the **ECU alias and description**, e.g. `ABS_WheelSpeedRaw`, `BSI_Version`, `E_VCU_BattCellVoltages`, `VCU_TractionTemps`. CAN IDs remain in DBC identifiers/comments, not `Raw_<ID>` or `Hs1_<ID>` names.
- Unknown senders use `Unknown_Message01` etc.; the inventory maps them to IDs. An ECU identity is not invented from payload resemblance.
- When several source senders exist, the electric-VCU alternative is preferred for the name on this EV. All original alternatives remain in transmitter metadata, comments and the inventory. They are **database claims**, not measured physical transmitters; forwarding is possible.
- Former reviewed aliases/confidence remain. Newly imported fields are marked `DATABASE_CANDIDATE` in comments. Constant, invalid-only, default-only and ICE/hybrid-labelled fields are retained for investigation.

## Exclusion policy and corrections

All fitting **canonical HS.HS1 source fields** are imported. Only **12 fields** are excluded because their bit ranges extend past every captured payload: [rejection list](opel_corsa_e_hscan1.rejections.csv). These occur in `0A6`, `495`, `4F8`, `543`. Their remaining in-bounds fields are retained with a source-DLC mismatch warning. Missing source DLC on `40B/42B/44B` is filled from the capture, not treated as proof of a wrong definition.

Existing documented corrections remain:

- `5A3`: impossible source cell-index factor 11 is replaced by candidate raw factor 1 for the observed 108-cell pack. Physical cell ordering is unverified.
- `334`: `CellGroup` multiplexing for 108 logical cell voltages.
- `3F4`: group/byte values become 54 logical temperature slots without asserting inconsistent source probe identities. Group-7 padding remains raw, not a sensor. This is not proof of 54 installed thermistors.
- `5C1`: misleading `BML_VERSION` becomes `VCU_TractionTemps`, with all six fitting fields retained.

The ambiguous `563` layout and default-only `208/58E` fields remain available with warnings. Source multiplexing is preserved, including `55A`; unknown selector states retain raw coverage.

Unresolved alternative bit positions/scales are not declared wrong. The canonical layout is used in the DBC; alternatives remain in signal comments and the [complete source snapshot](opel_corsa_e_hscan1.source.json), including message-level alternatives. Cosmetic enum quotes are normalized for DBC syntax; original labels remain in the snapshot. Text encoding is UTF-8.

Read [the capture analysis](../docs/obd-hscan1-2026-10-28.md) before using candidate fields. Raw invalid/default sentinels are described in signal comments. **DBC numeric decoding can still scale an invalid sentinel**; a consumer must check validity explicitly. There are no verified vehicle commands or diagnostic transaction definitions in this file. A DBC describes layouts and must not be treated as permission/evidence to transmit them.

The [wake-up captures](../docs/can-wakeup-2026-10-28.md) show zero-filled startup battery payloads before real readings become available. Sentinel checks alone do not establish validity; consumers need startup/availability gating. The export includes startup IDs `102/112/12A`. Two zero/unavailable complete cell sweeps are excluded from the **173 nonzero** sweep summaries.

## Provenance and license

Definitions derive from [prototux/PSA-RE](https://github.com/prototux/PSA-RE), revision `67f7581e7622410efdbb29c7eda3b86982b9b450`, using the `AEE2010.electric → AEE2010.ev → AEE2010.full` inheritance chain and **HS.HS1 only**. Definitions from HS.HS2, HS.HYB, HS.IS and low-speed networks are not substituted merely because an ID matches.

The derived database retains **Apache-2.0** provenance: [license](LICENSE-PSA-RE.txt), [modification notice](NOTICE-PSA-RE.txt). The firmware and generator retain the project's BSD license. Modifications include all-observed-ID export, ECU-first naming, payload-bound exclusions, raw coverage, shorter names, cell-index correction and temperature-slot multiplexing.

## Reproduce and verify

Install [the analysis dependencies](../tools/requirements-can.txt), download the ZIP archive of the pinned source revision, then run from the repository root:

```powershell
python tools/generate_hscan1_dbc.py `
  --psa-re C:\path\to\PSA-RE.zip `
  --capture-dir logs `
  --output dbc/opel_corsa_e_hscan1.dbc

python -m unittest discover -s tests -p test_hscan1_dbc.py -v
```

Alternatively repeat `--capture path.csv` for selected logs. Each log retains its relative clock and frame order; period statistics never bridge files. Inputs are not modified.

The generator performs no vehicle I/O. It reloads the actual export with strict bounds/overlap checks and decodes **all 181,002 frames**, comparing **2,151,479 named field values and 323,061 raw/reserved values** against an independent MSB-first extractor. Four tests cover unavailable-only fields, enum quoting, payload bounds, signed multiplexing, unknown selector states, ECU naming and independent clocks. These checks validate conversion/coverage, not every physical meaning.

Outputs:

| File | Contents |
| --- | --- |
| `opel_corsa_e_hscan1.dbc` | Database to load into a CAN viewer |
| `opel_corsa_e_hscan1.inventory.csv` | Every ID, ECU name, captures, DLC, period and source link |
| `opel_corsa_e_hscan1.validation.json` | Hashes, per-capture validation, ranges and invalid/default counts |
| `opel_corsa_e_hscan1.audit.json` | Every retained, corrected, replaced or excluded field and reason |
| `opel_corsa_e_hscan1.rejections.csv` | The 12 demonstrably out-of-payload fields |
| `opel_corsa_e_hscan1.source.json` | Resolved source metadata and unresolved alternatives |
| `opel_corsa_e_hscan1.cells.csv` | Logical cell voltage ranges over 173 nonzero complete sweeps |

Edit documented generator rules and regenerate; DBC and reports must remain in sync. This export adds no firmware mappings or vehicle commands.
