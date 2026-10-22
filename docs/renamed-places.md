# Renamed places (Questopia 3.19.4 → Questwave)

The rename is mechanical: all application code moved from `org.qp.android` to
`com.questwave.player`, and every externally visible identifier was changed so
Questwave can be installed next to Questopia/QuestPlayer.

## Android identity

| Place | Questopia 3.19.4 | Questwave |
| --- | --- | --- |
| Gradle namespace | `org.qp.android` | `com.questwave.player` |
| `applicationId` | `org.qp.android` | `com.questwave.player` |
| APK label | Questopia | Questwave (`@string/appName`) |
| Application class | `QuestPlayerApplication` | `QuestwaveApplication` |
| Notification channel id | `org.qp.android.channel.install_game` | `com.questwave.player.channel.install_game` |
| FileProvider authority | `${applicationId}.provider` | `${applicationId}.provider` → `com.questwave.player.provider` |
| Plugin discovery action | `org.qp.android.intent.action.PICK_PLUGIN` | `com.questwave.player.intent.action.PICK_PLUGIN` |
| AIDL package / interfaces | `org.qp.android.plugin` (`IQuestPlugin`, `AsyncCallback`) | `com.questwave.player.plugin` |
| AIDL parcel | `org.qp.android.GameDataParcel` | `com.questwave.player.GameDataParcel` |
| Data/preferences folder | derived from `applicationId` | derived from `com.questwave.player` (own sandbox) |
| Default preferences file | `org.qp.android_preferences.xml` | `com.questwave.player_preferences.xml` |

The FileProvider authority and the preferences file name are not hard-coded:
they follow `applicationId`, so the new package automatically gets its own
provider authority and its own settings file.

## Source tree

Every source, resource and test package moved with the application package:

```
app/src/main/java/org/qp/android/**        → app/src/main/java/com/questwave/player/**
app/src/test/java/org/qp/android/**        → app/src/test/java/com/questwave/player/**
app/src/androidTest/java/org/qp/android/** → app/src/androidTest/java/com/questwave/player/**
app/src/main/aidl/org/qp/android/**        → app/src/main/aidl/com/questwave/player/**
```

The directories `res/layout`, `res/drawable`, `res/values`, etc. keep their
names; only resource *content* (app name, About/Credits, icon, colors) changed.
Generated data-binding/view-binding packages follow `namespace`.

## JNI and native library

| Place | Questopia | Questwave |
| --- | --- | --- |
| JNI symbol prefix | `Java_org_qp_android_model_libQP_NativeMethods_*` | `Java_com_questwave_player_model_libQP_NativeMethods_*` |
| Shared library | `libndkqsp.so` | `libndkqsp.so` (same name, new implementation) |
| Engine sources | `app/src/main/jni/qsp-5.7/` | `app/src/main/jni/qsp-5.9.5/` |
| Build system | `ndk-build` (`Android.mk`) | CMake 3.22.1 (`CMakeLists.txt`) |
| ABIs | armeabi-v7a, arm64-v8a, x86, x86_64 | same four ABIs |

`NativeMethods` and `LibQpCallbacks` keep their class names, method names and
signatures. Only their *implementation* changed (QSP 5.9.5 adapter); this is
why the Java UI and the render/media architecture required no rewrite.

## Branding and removed external references

* New adaptive + legacy launcher icon and splash theme (navy, ring, waves).
* EN/RU About and Credits rewritten for Questwave: honest list of upstream
  projects, QSP 5.9.5, Oniguruma and their licenses.
* Questopia "news" classes, layouts and banners removed.
* Update check via GitHub/Stock removed, as well as the JitPack repository and
  the ChangeLog/AppUpdater dependencies.
* Third-party chat/donation/update links removed from the UI.
* Questopia/QuestPlayer/Quest Soft Player names and logos are not used as the
  application's own branding. Their names remain only in the credits and in
  this documentation, as required for honest attribution.

## Not renamed

* The GPL-free Qt/QSP C sources keep their upstream file names and copyright
  headers (QSP 5.9.5 is MPL-2.0).
* Oniguruma keeps its upstream layout and license files.
* Resource key names (`appName`, settings keys such as `fontSize`, `typeface`)
  were kept where they are internal, because they are not visible outside the
  app and changing them would add risk without benefit.
