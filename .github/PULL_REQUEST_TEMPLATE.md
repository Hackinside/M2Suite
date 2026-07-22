<!--
Thanks for contributing. Delete any section that doesn't apply.
Anything non-trivial should have an issue first — see CONTRIBUTING.md.
-->

## What this changes

<!-- One or two sentences. What's different after this merges? -->

Fixes #

## Why

<!--
For a format change, the *why* is the valuable part: which assumption was
wrong, which file exposed it, what you checked it against. That's what stops
someone "simplifying" this back into a bug later.
-->

## How it was verified

- [ ] `cmake --build --preset windows-msvc` succeeds
- [ ] `ctest --preset windows-msvc --output-on-failure` passes
- [ ] Opened a real file of the affected type in the GUI and confirmed the
      output is still correct (the test suite can't check what game data
      should look like)

<!-- Which files did you open, and what did you see? -->

## Tests

- [ ] Added or updated a test
- [ ] The test is self-contained — synthesises its inputs or uses a small
      checked-in fixture, **no game data**

<!-- If you didn't add a test, say why. Sometimes that's the right answer. -->

## Documentation

- [ ] New format knowledge added to `docs/FORMATS.md`, graded
      *verified* / *consistent* / *assumed*
- [ ] Any gap this closes removed from `docs/LIMITATIONS.md`
- [ ] New external sources added to `docs/REFERENCES.md`
- [ ] `CHANGELOG.md` updated under Unreleased

## Provenance

<!--
If you derived anything from an external source, name it — a repo, a spec,
a decompilation. Reading a reference to understand a *format* is fine and is
how most of this project was built. Copying an implementation needs a
compatible licence. Say where you looked; that's better than staying quiet.
-->

- [ ] My contribution is licensed under GPL-3.0-or-later
- [ ] No copyrighted game data is included in this PR
