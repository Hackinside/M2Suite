# Contributing to M2Suite

Bug reports, format findings, feature requests and code are all welcome.

Two rules override everything else on this page:

> ### 1. Never attach game data
> Every asset M2Suite reads is copyrighted. **Do not upload game files,
> disc images, or archive extracts to issues or pull requests.** Describe
> the file, hex-dump its first 64 bytes, quote the error — that is almost
> always enough. If a bug genuinely cannot be characterised without the
> data, say so in the issue and we will work out how to reproduce it
> without it.
>
> ### 2. Name the file
> A format bug that cannot be tied to a specific file cannot be fixed.
> Game, exact path inside the game, and file size. The **Copy Info** button
> in the app produces exactly this — use it.

---

## Reporting a bug

**[Open a bug report →](https://github.com/Hackinside/M2Suite/issues/new?template=bug_report.yml)**

### What a good report contains

| | |
|---|---|
| **Version** | From `Help > About`, including the build number |
| **The file** | Game name, exact path inside the game, size in bytes, what M2Suite says it is |
| **What happened** | The exact error text, or a description of the wrong output |
| **What you expected** | Sometimes obvious, sometimes not — say it anyway |
| **The log** | `m2suite.log`, beside the executable. Attach the relevant lines. |
| **First 64 bytes** | For format bugs, a hex dump of the file header |

### What makes a report *actionable*

- **"Frame 15 of 74 renders with the left half scrambled"** beats "the
  animation is broken". A specific frame in an otherwise-correct file
  usually points at one edge case.
- **Attach a screenshot** for anything visual. Wrong colours, wrong
  geometry and wrong layout look completely different to us and identical
  in prose.
- **Say whether other files of the same type work.** "This is the only .cel
  in the game that fails" and "every .cel in this game fails" lead to
  different investigations.
- **If it used to work, say which version it worked in.**

### Bugs that will be closed

- No file identified, for a format bug
- Game data attached (please repost without it)
- A request to add a game whose files are not described
- "Doesn't work" with no error, no log and no screenshot

### Severity

Say which of these it is, in your own words:

- **Crash or data loss** — highest priority, always
- **Wrong output** — decodes, but the result is incorrect. Also high: wrong
  output that looks plausible is the most dangerous failure this project
  has, because it can go unnoticed.
- **Fails to open** — a file that should be supported is rejected
- **Cosmetic** — a small artefact in otherwise correct output

---

## Requesting a feature

**[Open a feature request →](https://github.com/Hackinside/M2Suite/issues/new?template=feature_request.yml)**

**Read [docs/LIMITATIONS.md](docs/LIMITATIONS.md) first.** It lists known
gaps and marks each as blocked on knowledge (🔬), blocked on work (🔧), or
out of scope (🚫). If your request is already there, add your use case to
the existing issue instead — knowing that someone actually wants a thing is
what moves it up the list.

A good request says:

1. **What you want to do**, in terms of the outcome — "export AITD models
   to a format Blender can open with materials", not "add glTF support".
   The outcome sometimes has a cheaper answer than the implementation you
   had in mind.
2. **Which games and files** it applies to.
3. **What you do today instead**, if anything. An existing painful
   workaround is strong evidence.
4. **Whether you would use it once or routinely.** Both are fine; they
   justify very different amounts of effort.

Requests to support **an entire new game** are welcome, but scope them: name
the specific file types, and give a directory listing with sizes and header
bytes. See "Reporting a format finding" below — that is usually the more
useful issue to open.

---

## Reporting a format finding

**[Open a format finding →](https://github.com/Hackinside/M2Suite/issues/new?template=format_finding.yml)**

This is the most valuable kind of contribution, and the one this project
exists to accumulate. You do not need to write any code.

A finding is worth publishing if it says:

- **Which format**, and which files exhibit it
- **What the layout is** — offsets, field sizes, endianness
- **How you established it** — this matters more than the finding itself.
  "The file is exactly 4 + 768 + 320×200 bytes" is verifiable. "It looks
  like a palette" is a hypothesis.
- **What it explains that the current understanding does not**

[docs/FORMATS.md](docs/FORMATS.md) grades every finding as *verified*,
*consistent* or *assumed*. Say which yours is; "assumed" is a perfectly
respectable contribution as long as it is labelled.

**Findings that contradict [FORMATS.md](docs/FORMATS.md) are the most
valuable of all.** Several entries there are corrections of things that were
confidently wrong for a long time. Please put "contradicts" in the title.

### Techniques that have worked here

Offered because they generalise, and because undocumented formats rarely
announce their own errors:

- **Exact size arithmetic.** If `header + palette + width×height` equals the
  file size exactly, you have almost certainly got the layout right. This is
  how the AITD backdrop format was confirmed in one step.
- **Statistical tests on decoded output.** Real audio has high lag-1
  autocorrelation; noise does not. This settled the FILM-audio codec
  question without any listening test (0.92 versus 0.01).
- **Validate against a declared invariant.** AITD bodies carry a bounding
  box; a candidate vertex array whose vertices all fall inside it *and* fill
  it is almost certainly the real one.
- **Disprove, don't just confirm.** The AITD compression was assumed to be
  LZSS. Decoding the first control byte showed a back-reference to offset
  789 when only four bytes of output existed — impossible, so the
  assumption was wrong. Actively trying to break a hypothesis is faster than
  accumulating agreement with it.

---

## Suggesting a reference source

**[Open a reference source issue →](https://github.com/Hackinside/M2Suite/issues/new?template=reference_source.yml)**

Specifications, decompilations, open-source tools, hardware documentation,
forum threads with real findings. See
[docs/REFERENCES.md](docs/REFERENCES.md#suggesting-a-new-source) for what
makes a good one — briefly: what it is, what it covers, how reliable it is,
and what licence it carries.

---

## Contributing code

1. **Open an issue first** for anything non-trivial, so effort is not
   wasted on an approach that will not be merged.
2. **Match the surrounding style.** This codebase has consistent
   conventions for naming, comment density and error handling — read the
   neighbouring file before writing.
3. **Put format logic in a library, never in the shell.** `apps/` is the
   only Qt-dependent code; everything decodable must be decodable without a
   GUI. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
4. **Throw rather than guess.** If the parser does not understand
   something, raise `NotImplementedError`. Producing plausible-looking wrong
   output is worse than failing, because nobody notices.
5. **Add a test, and make it self-contained.** Tests synthesise their
   inputs or use small checked-in fixtures — never game data, because a
   test only one person can run is not a test.
   `tests/libm2model_tests/test_aitd_synth.cpp` is the model to follow.
6. **Explain the *why* in comments, not the *what*.** The valuable comments
   in this tree record why a non-obvious approach was necessary — which
   assumption failed, which reference was wrong. Those are what stop a
   future reader from "simplifying" a fix back into a bug.
7. **Update the docs.** New format knowledge belongs in
   [docs/FORMATS.md](docs/FORMATS.md); a closed gap should be removed from
   [docs/LIMITATIONS.md](docs/LIMITATIONS.md).

### Before you open a pull request

```bash
cmake --build --preset windows-msvc
ctest --preset windows-msvc --output-on-failure
```

Both must pass. If you touched a decoder, also open a real file of that type
in the GUI and confirm the output is still right — the test suite cannot
check what game data looks like.

---

## Licensing note for contributors

M2Suite is **[GPL-3.0-or-later](LICENSE)**. By opening a pull request you
agree your contribution is licensed under those terms.

Two things to keep clean:

- **Don't paste code from sources you can't license under GPL-3.0.** Reading
  a reference implementation to understand a *format* is fine and is how
  most of this project was built — formats are facts. Copying its
  implementation is not, unless its licence permits it. If you're unsure,
  say where you looked in the PR description; that's better than staying
  quiet.
- **Record provenance.** If you derive something from a specific external
  source, add it to [docs/REFERENCES.md](docs/REFERENCES.md), and to
  [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) if any code is
  actually vendored.

Licensing questions belong in an issue, not buried in a PR thread.
