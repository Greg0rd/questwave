# Questwave

Questwave is a **private, local-only** Android client for interactive text
games in the QSP format. It is a rebranded build of
[Questopia 3.19.4](https://github.com/Pixel-Narrative-Punks/Questopia) with the
native QSP engine replaced by **QSP 5.9.5**.

* Package / application ID: `com.questwave.player`
* App name: Questwave
* Engine: QSP 5.9.5 (vendored, pinned) + Oniguruma
* Base: Questopia 3.19.4, engine integration based on the Questopia JNI bridge
* ABIs: armeabi-v7a, arm64-v8a, x86, x86_64

Questwave is not published anywhere and is not a Questopia/QuestPlayer/
Quest Soft Player release. It installs next to those apps and has its own
preferences, save pickers and FileProvider authority.

## Documentation

| Document | Content |
| --- | --- |
| [`docs/build.md`](docs/build.md) | requirements, Gradle, Android Studio, GitHub Actions, verification |
| [`docs/qsp-5.9.5-api-mapping.md`](docs/qsp-5.9.5-api-mapping.md) | old JNI surface → QSP 5.9.5 API/callbacks mapping |
| [`docs/renamed-places.md`](docs/renamed-places.md) | every package ID/action/authority rename |
| [`docs/game-compatibility.md`](docs/game-compatibility.md) | expected behavior, known incompatible saves/plugins, manual checklist |
| [`app/src/main/jni/qsp-5.9.5/README.md`](app/src/main/jni/qsp-5.9.5/README.md) | pinned engine/Oniguruma revisions and licenses |

## What changed

* Full package/application ID rename to `com.questwave.player`, including JNI
  symbol names and `System.loadLibrary("ndkqsp")` (library name kept).
* New launcher icon and splash; Questopia news/banners, external chats,
  donations, update checks and JitPack dependencies removed.
* Own FileProvider authority and preferences; coexists with Questopia.
* QSP 5.7.0 sources deleted; QSP 5.9.5 and Oniguruma vendored at pinned
  revisions and built with CMake for all four ABIs, with 16 KB ELF page
  alignment (`-Wl,-z,max-page-size=16384`).
* Thin C adapter keeps `NativeMethods` and `LibQpCallbacks` unchanged where
  possible, so the rendering/media/file-picking architecture stayed intact.
* Friendly, localized error for incompatible/corrupted saves instead of a
  technical QSP dialog or a crash.

Plugins built for Questopia are **not** compatible: the AIDL package and the
plugin discovery action were renamed. See
[`docs/game-compatibility.md`](docs/game-compatibility.md).

## Build

```powershell
$env:JAVA_HOME = 'C:\path\to\jdk-17'
$env:ANDROID_HOME = 'C:\Users\<you>\AppData\Local\Android\Sdk'
.\gradlew.bat :app:assembleDebug --no-daemon --console=plain
```

Output: `app/build/outputs/apk/debug/app-debug.apk`.

CI: `.github/workflows/questwave-debug.yml` (manual dispatch or push to
`feature/questwave-qsp-5.9.5`) uploads the debug APK. No secrets are required.

## Credits and licensing

Questwave would not exist without the work of others:

* **QuestPlayer / QuestPlayer Android** — seedhartha and contributors
  (original Android port).
* **Questopia 3.19.4** — Pixel Narrative Punks and contributors (the base of
  this build).
* **QSP 5.9.5** — QSP Foundation / Val Argunov, licensed under MPL-2.0.
* **Oniguruma** — K. Kosako and contributors, BSD-2-Clause.
* **Quest Soft Player** — the original QSP player.
