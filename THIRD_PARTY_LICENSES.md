# Third-party provenance & licensing notes

> **⚠ Redistribution is not cleared.** Parts of this tree derive from
> sources whose terms are unresolved. Read this file before publishing this
> repository, cutting a release, or distributing any built binary.

M2Suite's own code has no licence chosen yet; until one is added, treat it
as all-rights-reserved.

---

## 1. `libs/libm2core`, `libs/libm2texture` — ported from the 3DO M2 SDK

Ported from `M2_3.1/Graphics/mercury/txtlib/` and `ifflib/` — Panasonic/3DO
SDK source. `M2TXTypes.h` states:

> Copyright (c) 1993-1996, an unpublished work by The 3DO Company. All
> rights reserved. This material contains confidential information that is
> the property of The 3DO Company. Any unauthorized duplication, disclosure
> or use is prohibited.

The 3DO Company dissolved in 2003; the disposition of its IP since then is
not something this project has researched or resolved. The porting was done
for **personal and research purposes**.

**This is the blocker for publishing.** It must be resolved before this
source, or any binary built from it, is distributed publicly. Options worth
evaluating: establishing who holds the rights today and seeking permission;
or reimplementing the affected decoders from the *format documentation*
rather than from the SDK source, since a file format itself is not
copyrightable even when a particular implementation of it is.

## 2. `libs/libm2model/third_party/pak_explode.cpp` — PKWARE DCL *implode*

Mark Adler's `blast` decoder, released into the **public domain** by its
author. No restriction on use or redistribution.

Renamed from `.c` to `.cpp` and wrapped in `extern "C" { ... }` because the
root `project()` declares `LANGUAGES CXX` only — see
[docs/BUILDING.md](docs/BUILDING.md).

## 3. `libs/libm2model/src/AitdPalette.cpp` — the AITD palette

The 256-colour Alone in the Dark palette, transcribed from AITD_PakEdit's
`AloneFile::palette`. This is a table of colour values — a fact about the
game data rather than creative expression — but the transcription route is
recorded here for completeness.

## 4. Format knowledge from open-source implementations

The body and PAK layouts implemented in `libm2model` were derived by
**reading** `fitd`, AITD_PakEdit and AITD-tools, not by copying their code
(apart from item 2 above, which is public domain). Formats are facts;
implementations are not. Every source is credited in
[docs/REFERENCES.md](docs/REFERENCES.md).

## 5. Not included in this repository

The following exist in the working tree but are **deliberately excluded**
here, so this repository's licensing story stays as simple as possible:

- **`third_party/mame-ppc/`** — a PowerPC 602 CPU core and disassembler
  adapted from a MAME-derived Konami M2 emulator with no bundled licence
  file. MAME code of that era (circa 2008–2010) is typically
  GPL-2.0-or-later. If the disassembler module is ever shipped, either pin
  the exact MAME version to establish the applicable licence text and
  fulfil its obligations, or write a clean-room PowerPC 602 disassembler.
  Keeping it isolated in `third_party/` is what makes that choice possible
  later.
- **`Repositories/`** — clones of the reference projects listed in
  [docs/REFERENCES.md](docs/REFERENCES.md). Each carries its own licence;
  none is redistributed here.
- **`Documents/`** — official 3DO/Panasonic/ARM documentation PDFs.
- **Built binaries and the portable package** — these belong in Releases,
  and only once the item 1 question is resolved.

## 6. Game data

M2Suite ships **no game data**, and none may be added. 3DO, Panasonic M2,
and every game named in this repository are the property of their respective
owners. Formats are documented here for preservation and interoperability.

This is also why the test suite synthesises its fixtures: a test that needs
copyrighted data is a test only one person can run.
