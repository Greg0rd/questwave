# QSP 5.9.5 adapter (Questwave)

Questwave keeps Questopia's Java bridge (`NativeMethods`, `LibQpCallbacks`) and its
render/media architecture, but replaces the bundled QSP 5.7.0 engine with QSP 5.9.5.

One shared library is built: `libndkqsp.so`.

```
Java (LibQpProxyImpl) ── JNI ── androidqspwrapper.c ── QSP 5.9.5 default binding
                                        │
                                        └── LibQpCallbacks (Java)
```

* Engine sources: `app/src/main/jni/qsp-5.9.5/qsp` (upstream tag/commit, MPL-2.0).
* Oniguruma sources: `app/src/main/jni/qsp-5.9.5/oniguruma` (pinned commit, BSD-2-Clause).
* Adapter: `app/src/main/jni/qsp-5.9.5/androidqspwrapper.c`.
* Build: `app/src/main/jni/qsp-5.9.5/CMakeLists.txt`, all four historical ABIs.

`QSP_CHAR` is compiled as a 2-byte `wchar_t` (`-fshort-wchar`), i.e. exactly one
UTF-16 code unit, so the adapter copies strings to/from Java (`jchar`) without
transcoding. The engine never calls `wcs*` functions, so a 2-byte `wchar_t` is safe.

## NativeMethods mapping

| `NativeMethods` (old surface) | Implementation over QSP 5.9.5 |
| --- | --- |
| `QSPInit` | `QSPInit()` + `QSPSetCallback()` for 15 callbacks; caches Java callbacks and DTO classes/fields |
| `QSPDeInit` | `QSPTerminate()`; drops global refs, paths and cached hashes |
| `QSPIsInCallBack` | internal `qspIsInCallback` |
| `QSPEnableDebugMode` | `QSPEnableDebugMode` |
| `QSPGetCurStateData` | stub (`null`), same as 5.7 wrapper |
| `QSPGetVersion` | `QSPGetVersion` (returns `5.9.5`) |
| `QSPGetCompiledDateTime` | `QSPGetCompiledDateTime` |
| `QSPGetFullRefreshCount` | internal `qspFullRefreshCount` |
| `QSPGetQstFullPath` | adapter remembers the legacy `fileName` argument of `QSPLoadGameWorldFromData` |
| `QSPGetCurLoc` | internal `qspLocs[qspCurLoc].Name` |
| `QSPGetExprValue` | stub (`null`), same as 5.7 wrapper |
| `QSPGetMainDesc` | `QSPGetMainDesc` |
| `QSPIsMainDescChanged` | FNV-1a snapshot comparison (5.9.5 removed the 5.7 dirty flag) |
| `QSPGetVarsDesc` | `QSPGetVarsDesc` |
| `QSPIsVarsDescChanged` | FNV-1a snapshot comparison |
| `QSPGetVarValuesCount` | stub (`null`), unused by Questopia and unused by the 5.7 wrapper |
| `QSPGetVarValues` | `QSPGetVarValue` + `QSPConvertValueToString`; `intValue` from numeric variant or `qspStrToNum` for strings |
| `QSPGetMaxVarsCount` | `0` (5.9.5 has no fixed variable table; unused) |
| `QSPGetVarNameByIndex` | stub (`null`), same as 5.7 wrapper |
| `QSPSetInputStrText` | `QSPSetInputStrText` |
| `QSPGetActionsCount` | `QSPGetActions(NULL, 0)` |
| `QSPGetActionData` | `QSPGetActions` into a 50-item stack buffer, then index |
| `QSPExecuteSelActionCode` | `QSPExecuteSelActionCode` |
| `QSPSetSelActionIndex` | `QSPSetSelActionIndex` |
| `QSPGetSelActionIndex` | `QSPGetSelActionIndex` |
| `QSPIsActionsChanged` | FNV-1a snapshot over count, descriptions and images |
| `QSPGetObjectData` | internal `qspGetObjectInfoByIndex`: `Title` → `name`, `Image` → `image` (5.9.5 object groups) |
| `QSPGetObjectsCount` | `QSPGetObjects(NULL, 0)` |
| `QSPSetSelObjectIndex` | `QSPSetSelObjectIndex` |
| `QSPGetSelObjectIndex` | `QSPGetSelObjectIndex` |
| `QSPIsObjectsChanged` | FNV-1a snapshot over `Title`/`Image` |
| `QSPExecString` | `QSPExecString` |
| `QSPExecLocationCode` | `QSPExecLocationCode` |
| `QSPExecCounter` | `QSPExecCounter` |
| `QSPExecUserInput` | `QSPExecUserInput` |
| `QSPGetLastErrorData` | `QSPGetLastErrorData` struct → `ErrorData` (`LocName`, `ErrorNum`, `ActIndex`, `IntLineNum`) |
| `QSPGetErrorDesc` | `QSPGetErrorDesc` |
| `QSPLoadGameWorldFromData` | remembers `fileName`'s directory for includes, then `QSPLoadGameWorldFromData(..., TRUE)` |
| `QSPSaveGameAsData` | two-pass caller-owned buffer: `QSPSaveGameAsData(NULL, &size, ...)`, then write; up to 4 attempts |
| `QSPOpenSavedGameFromData` | `QSPOpenSavedGameFromData`; hashes reset so the loaded UI is always re-read |
| `QSPRestartGame` | `QSPRestartGame`; hashes reset |
| `QSPSelectMenuItem` | stores the selected index consumed by the `QSP_CALL_SHOWMENU` callback |

## Callback mapping

| QSP 5.9.5 callback | Java `LibQpCallbacks` |
| --- | --- |
| `QSP_CALL_REFRESHINT` | `RefreshInt()` |
| `QSP_CALL_SHOWIMAGE` | `ShowPicture(path)` |
| `QSP_CALL_SETTIMER` | `SetTimer(msecs)` |
| `QSP_CALL_SHOWMSGSTR` | `ShowMessage(message)` |
| `QSP_CALL_PLAYFILE` | `PlayFile(path, volume)` |
| `QSP_CALL_ISPLAYINGFILE` | `IsPlayingFile(path)` |
| `QSP_CALL_CLOSEFILE` | `CloseFile(path)` |
| `QSP_CALL_OPENGAMESTATUS` | `OpenGame(filename)` (save load; `null` → file picker) |
| `QSP_CALL_SAVEGAMESTATUS` | `SaveGame(filename)` (save; `null` → file picker) |
| `QSP_CALL_INPUTBOX` | `InputBox(prompt)` |
| `QSP_CALL_GETMSCOUNT` | `GetMSCount()` |
| `QSP_CALL_SHOWWINDOW` | `ShowWindow(type, isShow)`; 5.9.5 bit mask remapped to the old enum (`ACTS=0`, `OBJS=1`, `VARS=2`, `INPUT=3`), MAIN/VIEW ignored |
| `QSP_CALL_SHOWMENU` | `DeleteMenu()`, `AddMenuItem(...)` per item, `ShowMenu()`, then returns the index recorded by `QSPSelectMenuItem` (`-1` when cancelled) |
| `QSP_CALL_SLEEP` | `Wait(msecs)` |
| `QSP_CALL_OPENGAME` | adapter: path resolution + `GetFileContents` + `QSPLoadGameWorldFromData` |

`QSP_CALL_CHANGEQUESTPATH` no longer exists in 5.9.5; `ChangeQuestPath` is kept in the
Java interface for source compatibility but is not invoked.

`QSP_CALL_INPUTBOX` result is copied into the caller's buffer and always
NUL-terminated. Menu item images and names are passed as Java strings.

`QSP_CALL_SHOWMENU` returns the selected index directly (5.9.5 API), while Questopia's
old flow selected a menu item through `QSPSelectMenuItem`; the adapter stores that
index and returns it.

## Differences that are intentional

* **File system.** QSP 5.9.5 has no file I/O. INCLUDE/OPENQST arrive as
  `QSP_CALL_OPENGAME(file, isNewGame)` and are read through Java
  (`LibQpCallbacks.GetFileContents`) relative to the directory of the main `.qsp`.
  The save-file pickers (`OpenGame(null)` / `SaveGame(null)`) keep working.
* **Changed flags.** 5.7 exposed never-cleared dirty flags. 5.9.5 expects the host to
  compare state. The adapter keeps FNV-1a snapshots, so the existing Java refresh
  code still avoids rebuilding identical lists.
* **Object display text.** 5.9.5 adds object groups. The display title is `Title`
  (group-updated description, or the object name for old games), which matches what
  the old `ObjectData.name` displayed.
* **Save format.** 5.9.5 writes its own save format; old `.sav` files are not
  migrated. Foreign/corrupted files report a QSP error instead of crashing.
* **Plugins.** Questopia plugins are tied to the old engine ABI and are not
  compatible with 5.9.5 (documented as a known limitation).

## Build

Debug APK (Windows example, JDK 17 and Android SDK with NDK 27.3 / CMake 3.22.1):

```powershell
$env:JAVA_HOME = 'C:\...\jdk-17'
$env:ANDROID_HOME = 'C:\Users\...\Android\Sdk'
.\gradlew.bat :app:assembleDebug --no-daemon --console=plain
```

Output: `app/build/outputs/apk/debug/app-debug.apk`.

The CMake build links the shared library with
`-Wl,-z,max-page-size=16384` (16 KB page support) and builds
`armeabi-v7a`, `arm64-v8a`, `x86`, `x86_64` — the same set as Questopia 3.19.4.
