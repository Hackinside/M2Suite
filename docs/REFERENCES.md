# Reference sources

Everything this project's findings were built from. If you use M2Suite's
format documentation, these are the shoulders it stands on.

Entries are marked by how they were used:

- **Ground truth** — treated as authoritative; our code was written to match it.
- **Corroborating** — used to cross-check a finding reached independently.
- **Vendored** — code from it is compiled into M2Suite (see
  [THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md)).
- **Contradicted** — we found it to be wrong for 3DO data. Named so nobody
  repeats the mistake.

---

## Official 3DO / Panasonic documentation

| Source | Use | Covers |
|---|---|---|
| *3DO M2 Release Version 2.7*, Vols 1–5 | Ground truth | M2 system architecture, graphics, audio |
| *3DO M2 v2.7 Part 2 — Mercury Programmer's Guide* | Ground truth | The Mercury renderer and texture pipeline |
| *3DO System Programmer's Guide* | Ground truth | M1 cels, CCB semantics, the Opera filesystem |
| *Konami M2 Technical Documentation, Rev 2* | Corroborating | Arcade M2 hardware |
| *ARM Architecture reference* / *arm60* / *ARM SDT* | Ground truth | M1 ARM60 executables |

## Community knowledge bases

| Source | Use | Covers |
|---|---|---|
| [**SDA — Alone in the Dark (1–3): Game Mechanics and Glitches**](https://kb.speeddemosarchive.com/Alone_in_the_Dark_(1-3)/Game_Mechanics_and_Glitches) | Corroborating | Engine behaviour documented from the outside by speedrunners — collision, room transitions, actor state, timing. Behavioural ground truth that complements the byte-level formats, and a good cross-check on whether a decoded structure means what we think it means. |

## 3DO / M2 SDK and OS source

| Source | Use | Covers |
|---|---|---|
| M2 SDK 3.1 — `Graphics/mercury/txtlib/` | Ground truth | The UTF/M2TX texture codec. This is *the* authority for texture layout. |
| M2 SDK 3.1 — `ifflib/` | Ground truth | M2 IFF chunk containers and their 4-byte alignment |
| M2 Portfolio OS source (`M2_3.0/ws_root`) | Ground truth | System structures, DataStreamer |
| Opera/Portfolio OS source (M1) | Ground truth | Cels, ANIM, IMAG, LRForm framebuffer layout |
| `CelLib.cpp`, `form3do.h` | Ground truth | Cel bit depths, packed-cel row encoding, ANIM chunk structure |

> These carry a 3DO Company confidentiality notice. See
> [THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md) — this is unresolved
> and matters before any public distribution.

## Open-source implementations

| Source | Use | Covers |
|---|---|---|
| [**fitd**](https://github.com/yaz0r/fitd) — Free In The Dark | Ground truth | The AITD engine reimplementation. `hqr.cpp createBodyFromPtr` is the authority for the body format; `renderer.cpp` shows how primitives are drawn (including that PolyTexture polygons are rendered *flat*). |
| **AITD_PakEdit** | Ground truth | PAK archive layout, and the 256-colour AITD palette (`AloneFile::palette`), transcribed into `AitdPalette.cpp` |
| **AITD-tools** | Ground truth | Confirmed PKWARE DCL *implode* as the PAK compression, after LZSS was disproved |
| [**AITD-roomviewer**](https://github.com/tigrouind/AITD-roomviewer) | Corroborating | Room/floor (`ETAGE*.PAK`) layout, camera zones, and a reference model viewer. The obvious next source for the room geometry gap in [LIMITATIONS.md](LIMITATIONS.md). |
| **AITD_PakEdit name databases** | Data | `*_PAK_DB.json` — hand-curated per-entry names and content types. Read when present; never bundled. |
| **Mark Adler's `blast`** | Vendored | Public-domain PKWARE DCL *implode* decoder — `libs/libm2model/third_party/pak_explode.cpp` |
| **operafs** (Linux driver) | Ground truth | Opera filesystem — block layout, directory chaining, Shift-JIS names |
| [**FFmpeg**](https://ffmpeg.org/) | Corroborating / **contradicted** | Cinepak: corroborating. Its `segafilm` demuxer: **contradicted** — see below. |
| **ScummVM** (image codecs) | Corroborating | Cinepak cross-check |
| **MAME** | Corroborating | PowerPC 602 semantics for the disassembler |
| **QuickBMS** | Attempted | Tried for the Gex bigfile; ships no script for that title |
| **opera-libretro** | Corroborating | M1 hardware behaviour |

### The one that was wrong

**FFmpeg's `segafilm` demuxer** treats 8-bit FILM audio as raw signed PCM.
On 3DO discs it is **SDX2-compressed**, and decoding it as PCM produces
loud noise rather than audio. Established by measuring lag-1 autocorrelation
across candidate interpretations: 0.013 for signed PCM, 0.014 for unsigned,
**0.923 for SDX2**.

This is not a criticism of FFmpeg — its demuxer targets Sega FILM files,
where the assumption is presumably right. It is a reminder that a format
name shared across platforms does not guarantee shared payload semantics,
and that decoded output beats documentation.

---

## Suggesting a new source

**Open a [Reference source issue](https://github.com/Hackinside/M2Suite/issues/new?template=reference_source.yml).**
New sources are genuinely valuable — several entries above closed problems
that had been stuck for a long time.

A good suggestion answers four questions:

1. **What is it?** Name, URL, and what kind of thing (specification,
   decompilation, open-source tool, hardware documentation, forum thread
   with real findings).
2. **What does it cover?** Which format or which game. "Might be useful" is
   much weaker than "documents the ETAGE room format".
3. **How reliable is it?** Reverse-engineered notes, an official spec, or a
   working implementation? A tool that *correctly decodes real files* is
   worth more than a document, because it can be tested.
4. **What is its licence?** Critical if code might be borrowed. A source we
   can read but not copy is still useful — findings are facts and are not
   themselves copyrightable — but that distinction has to be tracked
   deliberately.

A source that **disproves** something in [FORMATS.md](FORMATS.md) is the
most valuable kind. Please say so plainly in the title.

### What tends not to help

- Emulator binaries with no source and no documentation
- Format descriptions with no worked example and no test file
- Wiki pages that cite no primary source (these have circulated several
  errors between them; the FILM-audio-is-PCM claim is one)

---

## Suggesting future work

For a capability rather than a source, open a
[Feature request](https://github.com/Hackinside/M2Suite/issues/new?template=feature_request.yml)
and check [LIMITATIONS.md](LIMITATIONS.md) first — it is organised by
whether each gap is blocked on *knowledge* (🔬 needs research), *work*
(🔧 needs someone to write it), or is out of scope (🚫). Knowing which one
you are volunteering for makes the discussion much shorter.

Ideas that have been raised and are open for anyone:

- AITD model animations (`LISTANIM.PAK`) and skinning — 🔧 well-understood
  format, working reference implementation, nobody has done it
- AITD room geometry (`ETAGE*.PAK`) — 🔧 would allow reconstructing whole
  locations rather than isolated props
- UTF RLE-compressed and direct texel formats — 🔧 SDK source is the
  ground truth, this is transcription
- glTF export alongside OBJ — 🔧 would carry materials and, eventually,
  animation
- A command-line batch converter over the same libraries — 🔧 the libraries
  are already Qt-free specifically to allow this
- PEBM dimensions — 🔬 genuinely unsolved; the game executable is
  disassemblable and is the most promising route
- A Linux/macOS build — 🔧 libraries are portable, the shell uses only
  cross-platform Qt
