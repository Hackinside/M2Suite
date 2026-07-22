# Provenance, attribution & disclaimer

M2Suite's own code is licensed under **GPL-3.0-or-later** — see
[LICENSE](LICENSE).

This file records where everything else came from. Preservation work is
built on other people's work, and saying so precisely is both the honest
thing and the useful thing: it tells the next person where to go to check a
finding.

---

## Disclaimer

M2Suite is an independent, non-commercial **interoperability and digital
preservation** project. It is not affiliated with, endorsed by, or connected
to The 3DO Company, Panasonic, Konami, or any game publisher named anywhere
in this repository.

- **No game data is included or distributed.** M2Suite reads files you
  already own. None are bundled, and none may be added — see
  [Game data](#game-data).
- **No SDK, firmware, or BIOS images are included or distributed.**
- All trademarks and product names are the property of their respective
  owners and are used only to identify the formats and titles being
  described.
- File **formats** are documented here as factual descriptions, for
  interoperability. A format is not itself a creative work; a particular
  implementation of one is, which is why provenance is tracked below.

The 3DO Company ceased trading in 2003 and its M2 platform has been out of
production for roughly three decades. The reference materials cited below
have circulated publicly for many years — on GitHub, the Internet Archive,
and preservation sites — and are cited rather than reproduced wherever
possible.

**If you hold rights to any material referenced here and object to its use,
please open an issue or contact the maintainer, and it will be addressed
promptly.** That is a commitment, not a formality.

---

## 1. `libs/libm2core`, `libs/libm2texture` — 3DO M2 SDK

Derived from the Panasonic/3DO M2 SDK 3.1 source:
`M2_3.1/Graphics/mercury/txtlib/` and `ifflib/` — the UTF/M2TX texture codec
and the M2 IFF chunk-container handling.

The original headers carry a 1993–1996 3DO Company confidentiality notice.
The SDK has been publicly archived for many years and is the only accurate
description of these formats in existence; the alternative to using it is
guesswork, which for undocumented binary formats means producing
*plausible-looking wrong output* rather than failing — the failure mode this
project works hardest to avoid.

The code here is a **port**, not a copy: structures were re-expressed in
modern C++ with different error handling, bounds checking and API shape.
Where behaviour matches the SDK, that is because the format requires it.

Cited in full in [docs/REFERENCES.md](docs/REFERENCES.md).

## 2. `libs/libm2model/third_party/pak_explode.cpp` — PKWARE DCL *implode*

Mark Adler's `blast` decoder, released into the **public domain** by its
author. No restrictions.

Renamed from `.c` to `.cpp` and wrapped in `extern "C" { ... }` because the
root `project()` declares `LANGUAGES CXX` only — see
[docs/BUILDING.md](docs/BUILDING.md).

## 3. `libs/libm2model/src/AitdPalette.cpp` — the AITD palette

The 256-colour Alone in the Dark palette, transcribed from AITD_PakEdit's
`AloneFile::palette`. A table of colour values — a fact about the game data
rather than creative expression — but the route is recorded for
completeness. AITD_PakEdit is GPL-2.0, which is compatible with this
project's GPL-3.0-or-later.

## 4. Format knowledge from open-source implementations

The AITD body and PAK layouts in `libm2model` were derived by **reading**
`fitd`, AITD_PakEdit and AITD-tools — not by copying their code, apart from
item 2 above, which is public domain. Formats are facts; implementations are
not.

Every source is credited in [docs/REFERENCES.md](docs/REFERENCES.md),
including the one that turned out to be **wrong** for 3DO data (FFmpeg's
`segafilm` audio assumption), because a corrected reference is as valuable
as a confirmed one.

## 5. Not currently in this repository

- **`third_party/mame-ppc/`** — a PowerPC 602 CPU core and disassembler
  adapted from a MAME-derived Konami M2 emulator with no bundled licence
  file. MAME code of that era (circa 2008–2010) is typically
  **GPL-2.0-or-later**, which is compatible with this project's GPL-3.0
  licence, so it *can* be brought in — but the exact upstream version should
  be pinned first so the applicable licence text and attribution are
  correct. Choosing GPL-3.0-or-later for M2Suite keeps that door open.
- **`Repositories/`** — working clones of the reference projects listed in
  [docs/REFERENCES.md](docs/REFERENCES.md). Each carries its own licence and
  is cited, not redistributed.
- **`Documents/`** — 3DO/Panasonic/ARM documentation PDFs, cited not
  redistributed.

## 6. Bundled at release time

The portable package published under
[Releases](https://github.com/Hackinside/M2Suite/releases) — and mirrored in
[`dist/`](dist/) for people who would rather not build — bundles:

| Component | Licence |
|---|---|
| Qt 6 (Widgets, Multimedia, MultimediaWidgets) | LGPL-3.0 — dynamically linked, replaceable |
| FFmpeg libraries | LGPL-2.1+ (GPL components not enabled) |
| Microsoft Visual C++ runtime | Microsoft redistributable licence |
| M2Suite itself | GPL-3.0-or-later |

Qt is **dynamically linked**, which is what LGPL compliance requires: the
DLLs sit beside the executable and can be replaced with your own build.
M2Suite's own source for any released binary is this repository, at the tag
matching that release.

## Game data

M2Suite ships **no game data**, and none may be added to this repository —
not as fixtures, not as test files, not as issue attachments.

This is also why the test suite synthesises its own inputs: a test that
requires copyrighted data is a test only one person can run. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#testing).
