# Known game-compatibility notes (QSP 5.9.5)

This document describes what is expected to work, what is known to fail, and
what still has to be checked on a real device. Questwave keeps Questopia's
rendering, media and file-picking code; only the engine and its JNI adapter
changed.

## What should work unchanged

* `.qsp` / `.gam` game files produced for QSP 5.7.x. `QSP_GAMEMIN_VER` (5.9.4)
  is checked only when loading **saves**, not game worlds.
* INCLUDE/OPENQST of files stored next to the main `.qsp`: the adapter resolves
  each request relative to the main game file and reads it through
  `LibQpCallbacks.GetFileContents` (SAF-aware).
* Music, sound, images and GIF playback: `PlayFile` / `ShowPicture` callbacks
  are unchanged; the media layer is Questopia's.
* Timers, input, menus, windows, `#` counters and user input.
* Saves made by QSP 5.9.4+/5.9.5 (the new format), including saves copied into
  the game folder.
* Installing next to Questopia: different `applicationId`, provider authority
  and preferences file.

## Known incompatibilities

1. **Old save files are not migrated.** A `.sav` written by QSP 5.7.x /
   Questopia 3.19.4 has an older game-version marker and is rejected by
   5.9.5. Questwave catches the failure, writes the technical reason to
   logcat, and shows a localized "incompatible or corrupted save" dialog
   instead of crashing. Creating new saves works.
2. **Foreign/corrupted saves** fail the same way: a clear dialog, no crash.
3. **Questopia plugins are not compatible.** The AIDL package and the plugin
   discovery action were renamed (`org.qp.android.plugin` →
   `com.questwave.player.plugin`, `org.qp.android.intent.action.PICK_PLUGIN` →
   `com.questwave.player.intent.action.PICK_PLUGIN`). Even a rebuilt plugin
   binds only to the new interface.
4. **Object groups.** QSP 5.9.5 adds object groups. Questwave shows the
   group-updated `Title` and `Image`, same as the old `ObjectData.name`/
   `image`. A game that deliberately relies on 5.9.5 group semantics in the
   object list may look different from a 5.9.5 desktop player.
5. **`SETINPUTSTRTEXT` host callback** is not registered (Questopia 3.19.4 did
   not have it either). Java calls `QSPSetInputStrText` before executing user
   input, so the normal flow is unaffected; a game that expected the engine to
   push text into an already open input box will not see it.
6. **No filesystem in the engine.** QSP 5.9.5 removed its file layer. Includes
   are resolved relative to the main `.qsp` only. A game that hard-codes an
   absolute device path for an include will not find it unless that path is
   readable through the normal Java file API.
7. **16 KB page alignment (debug APK).** All four ELF libraries have
   `p_align = 0x4000`. In the packaged APK, the arm64 entry is 16 KB aligned;
   the 32-bit and x86 ZIP entries are 4 KB aligned because AGP 8.1.2 writes
   them that way. This does not affect the ELF load segments, but on a strict
   16 KB-only device the APK should be re-aligned with
   `zipalign -P 16` **and re-signed** (see below).
8. **No game has been executed on a device in this workspace.** There are no
   `.qsp`/`.sav` files under `D:\QSP`, so the manual compatibility matrix is
   still open. Use the checklist below on your own device.

## Manual test checklist

Install the debug APK (it can coexist with Questopia) and test at least:

- [ ] Several old QSP 5.7 games: first location, ~50 actions, save, close,
      reopen the save.
- [ ] A heavy game (many objects/actions, HTML tags, large images).
- [ ] GIF animation and video playback smoothness versus Questopia 3.19.4.
- [ ] Music/sound start, stop (including `CLOSEFILE` with no argument).
- [ ] Audio/video does not stutter while the main text is refreshing.
- [ ] Menu with images and many items; cancel the menu.
- [ ] Input box with a default value, cancel.
- [ ] Message dialog, timer, `WAIT`, counter.
- [ ] Save from the game menu, load from the game menu and from
      "Load save" (file picker).
- [ ] Old `.sav` from Questopia: expect the clear dialog, no crash.
- [ ] Old `.sav` that actually uses the 5.9.5 format: loads.
- [ ] Rotate/resume during a long action.
- [ ] Compare turn time (action → UI refresh) on the same game/device against
      Questopia 3.19.4. Record the difference in the stage report.

If a game fails, capture `adb logcat -s LibQpProxyImpl` around the failure and
the game name; the technical QSP error is logged there.
