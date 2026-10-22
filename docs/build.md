# Building and verifying Questwave

Questwave is a private local build. The debug APK is signed with the local
Android debug key, so it can be installed next to Questopia/QuestPlayer (the
application ID is `com.questwave.player`).

## Requirements

* JDK 17
* Android SDK Platform 34, Build-Tools 34+ (CI uses the runner defaults)
* Android NDK `27.3.13750724`
* CMake `3.22.1` (installed through the SDK manager)
* Windows/Linux/macOS with Gradle wrapper 8.0

`local.properties` must point to the SDK, e.g.:

```properties
sdk.dir=C:\\Users\\<you>\\AppData\\Local\\Android\\Sdk
```

## Local command line (Windows PowerShell)

```powershell
$env:JAVA_HOME = 'C:\path\to\jdk-17'
$env:ANDROID_HOME = 'C:\Users\<you>\AppData\Local\Android\Sdk'
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME

cd D:\QSP\Questopia-3.19.4
.\gradlew.bat :app:assembleDebug --no-daemon --console=plain
```

Output:

```
app\build\outputs\apk\debug\app-debug.apk
```

Install with:

```powershell
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

If several devices are attached, add `-s <serial>`. Uninstalling is not needed
before installing another Questwave debug build; `-r` replaces it.

## Android Studio

1. Open the `Questopia-3.19.4` folder (project name: Questwave).
2. Use the bundled JDK 17.
3. Make sure SDK Platform 34 and NDK 27.3.13750724 are installed.
4. Build → Build Bundle(s)/APK(s) → Build APK(s).

The native build is CMake-based (`app/src/main/jni/qsp-5.9.5/CMakeLists.txt`)
and builds all four ABIs. Do not add `qsp/` to the include path: it contains a
`time.h`, which would shadow the system header.

## GitHub Actions

`.github/workflows/questwave-debug.yml` builds the debug APK on Ubuntu:

* JDK 17
* `android-actions/setup-android`
* `sdkmanager "ndk;27.3.13750724" "cmake;3.22.1"`
* `:app:assembleDebug`
* uploads `questwave-debug-apk` (v4 artifact)

Trigger it with **Run workflow** or push to
`feature/questwave-qsp-5.9.5`. No secrets or release keystore are needed.

The older `android-build.yml` workflow builds a release; Questwave has no
release signing configuration, so use it only after adding your own keystore
(as with any other Android project).

## What the build produces

* `libndkqsp.so` per ABI (`armeabi-v7a`, `arm64-v8a`, `x86`, `x86_64`).
* QSP version reported by `QSPGetVersion()`: `5.9.5`.
* Package `com.questwave.player`, label `Questwave`.

Quick checks:

```powershell
# package, label, ABIs
& "$env:ANDROID_HOME\build-tools\36.0.0\aapt.exe" dump badging `
  app\build\outputs\apk\debug\app-debug.apk |
  Select-String 'package:|application-label:|native-code'

# JNI symbols (41 NativeMethods + JNI_OnLoad)
& "$env:ANDROID_HOME\ndk\27.3.13750724\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-nm.exe" `
  -D --defined-only app\build\intermediates\stripped_native_libs\debug\out\lib\arm64-v8a\libndkqsp.so |
  Select-String 'Java_com_questwave_player_model_libQP_NativeMethods_'
```

## 16 KB page alignment

The linker is called with `-Wl,-z,max-page-size=16384`, and all four ELF
libraries report `p_align = 0x4000` on every `LOAD` segment. The APK entry for
arm64 is already 16 KB aligned with AGP 8.1.2; the other entries are 4 KB
aligned in the ZIP. If a device requires the ZIP entry itself to be 16 KB
aligned, post-process and re-sign:

```powershell
$align = "$env:ANDROID_HOME\build-tools\36.0.0\zipalign.exe"
$sign  = "$env:ANDROID_HOME\build-tools\36.0.0\apksigner.bat"
& $align -P 16 -f 4 app-debug.apk app-debug-aligned.apk
& $sign sign --ks "$env:USERPROFILE\.android\debug.keystore" `
  --ks-pass pass:android --key-pass pass:android `
  --out app-debug-aligned-signed.apk app-debug-aligned.apk
```

Do not run `zipalign` after `apksigner`; the v2 signature covers the ZIP
structure.

## Where the engine came from

See `app/src/main/jni/qsp-5.9.5/README.md` for the pinned QSP and Oniguruma
revisions, licenses and the reason `qsp/` is not an include directory.
