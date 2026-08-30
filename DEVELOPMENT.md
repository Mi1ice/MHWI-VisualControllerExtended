# Development and provenance notes

## Project-owned behavior

The maintained project specification consists of:

- ordered INI rules mapping a numeric query ID and published player-state snapshot to a Boolean;
- pass-through behavior when no rule matches or required state is unavailable;
- guarded, read-only state collection and double-buffered publication;
- hotkeys, chat commands, logging and configuration reload;
- synthetic tests for rule boundaries and unavailable-state behavior.

The target locator is version-specific compatibility data for Monster Hunter: World 15.23.00.
It is not an assertion of ownership over game code. A user may replace it with `TargetRva` or
`Signature` in the INI. 

## Release requirements

1. Preserve `LICENSE`, `THIRD_PARTY_NOTICES.md`, `third_party/minhook/LICENSE.txt` and
   `MHW_TOOLKIT_LICENSE.txt`.
2. Build only from the checked-in dependency sources; do not distribute unrelated files from
   `build` or `out`.
3. Package only `MHWI-VisualControllerExtended.dll`, `MHWI-VisualControllerExtended.ini`, `README.md`,
   `LICENSE`, `THIRD_PARTY_NOTICES.md`, `DEVELOPMENT.md`, `MINHOOK_LICENSE.txt` and
   `MHW_TOOLKIT_LICENSE.txt`. `MINHOOK_LICENSE.txt` must be copied from
   `third_party/minhook/LICENSE.txt`. For source releases, include the corresponding source tree
   and license files instead.
4. Record the source revision, compiler version and hashes of release artifacts.
