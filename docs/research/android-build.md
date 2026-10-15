# Android build from Linux (no sudo)

Status: verified 2026-10-15 on this box (Ubuntu 24.04, x86_64, 12 cores, no `/dev/kvm`, no
system Java). Everything lives under `tools/android/`; nothing is installed system-wide and
no Android emulator is used (no KVM). A proof APK (SDL2 window + beep + touch + back + asset
read + file picker via JNI) was built for arm64-v8a, armeabi-v7a and x86_64, in a debug
version and a signed release version. It has **not** been run on a device yet.

## 1. What is installed

| Component | Version | Path (under `/home/mmalves/workspace/emuladores/tk90x/tools/android/`) |
|---|---|---|
| Eclipse Temurin JDK | 17.0.20.1+1 | `jdk/` |
| Android cmdline-tools | 23.0 (`commandlinetools-linux-16111833_latest.zip`) | `sdk/cmdline-tools/latest/` |
| platform-tools (adb) | 37.0.1 | `sdk/platform-tools/` |
| SDK platforms | android-35 (r02), android-34 (ext7 r03) | `sdk/platforms/` |
| build-tools (aapt2, apksigner, zipalign) | 35.0.0 | `sdk/build-tools/35.0.0/` |
| NDK | r27d = 27.3.13750724 | `sdk/ndk/27.3.13750724/` |
| CMake (SDK package) | 3.22.1 | `sdk/cmake/3.22.1/` |
| SDL2 source release | 2.32.10 (the same version the Windows build uses) | `SDL2-2.32.10/` |
| Gradle (wrapper download) | 8.9 | `gradle-home/wrapper/dists/` |
| Android Gradle Plugin | 8.7.3 | `gradle-home/caches/` (Maven cache) |
| Dev release keystore | PKCS12, RSA 2048, valid 10 000 days | `keystore/tk90x-dev.jks` |
| Proof project (sources only) | n/a | `proof-project/` |
| Proof APKs | n/a | `br.tk90x.test-debug.apk`, `br.tk90x.test-release.apk` |

Disk: 3.9 GB in total. The SDK takes 2.8 GB (the NDK is most of it), `gradle-home` 714 MB,
`jdk` 318 MB and `SDL2-2.32.10` 81 MB.

Sources (sha256):

- JDK: `https://api.adoptium.net/v3/binary/latest/17/ga/linux/x64/jdk/hotspot/normal/eclipse`
  → `OpenJDK17U-jdk_x64_linux_hotspot_17.0.20.1_1.tar.gz`
  (`3808d1d15e3ec6bd5b84057fb5d84c33d8a1536a258146bcea2e603fc726e08e`)
- cmdline-tools: `https://dl.google.com/android/repository/commandlinetools-linux-16111833_latest.zip`
  (`0877a1d048fe4a24efe2eff536ca4223f7adeb58648bb81909d33c446918cfa8`). The newest build number
  is listed in `https://dl.google.com/android/repository/repository2-3.xml`.
- SDL2: `https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz`
  (`5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165`)

### Re-create from scratch

```sh
T=/home/mmalves/workspace/emuladores/tk90x/tools/android
D=$(mktemp -d); mkdir -p $T/sdk/cmdline-tools
curl -L -o $D/jdk.tgz "https://api.adoptium.net/v3/binary/latest/17/ga/linux/x64/jdk/hotspot/normal/eclipse"
curl -L -o $D/clt.zip https://dl.google.com/android/repository/commandlinetools-linux-16111833_latest.zip
curl -L -o $D/sdl.tgz https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
tar -xzf $D/jdk.tgz -C $T && mv $T/jdk-17* $T/jdk
unzip -q $D/clt.zip -d $T/sdk/cmdline-tools && mv $T/sdk/cmdline-tools/cmdline-tools $T/sdk/cmdline-tools/latest
tar -xzf $D/sdl.tgz -C $T
. $T/env.sh
yes | sdkmanager --licenses            # cmdline-tools 23 prints "no longer needed" but it does not hurt
yes | sdkmanager --sdk_root=$ANDROID_HOME "platform-tools" "platforms;android-34" "platforms;android-35" \
      "build-tools;35.0.0" "ndk;27.3.13750724" "cmake;3.22.1"      # about 1m45s
```

## 2. Environment (`tools/android/env.sh`)

```sh
. /home/mmalves/workspace/emuladores/tk90x/tools/android/env.sh
```

sets:

```sh
TK_ANDROID=/home/mmalves/workspace/emuladores/tk90x/tools/android   # used by app/build.gradle to find the keystore
JAVA_HOME=$TK_ANDROID/jdk
ANDROID_HOME=$TK_ANDROID/sdk
ANDROID_SDK_ROOT=$ANDROID_HOME
ANDROID_NDK_HOME=$ANDROID_HOME/ndk/27.3.13750724
ANDROID_USER_HOME=$TK_ANDROID/android-user-home   # debug.keystore + adb keys, instead of ~/.android
GRADLE_USER_HOME=$TK_ANDROID/gradle-home          # Gradle distribution + Maven cache, instead of ~/.gradle
SDL2_ANDROID_SRC=$TK_ANDROID/SDL2-2.32.10
PATH=$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/build-tools/35.0.0:$PATH
```

Because of `ANDROID_USER_HOME`, the auto-generated **debug** key lives at
`tools/android/android-user-home/debug.keystore`. Keep it: if the debug key changes, a device
refuses to update the installed app until the old copy is uninstalled.

## 3. The proof project

The project is in `tools/android/proof-project/`. It was copied from
`SDL2-2.32.10/android-project` and then changed as follows:

| File | Change vs. SDL template |
|---|---|
| `build.gradle` | AGP `8.1.1` → `8.7.3`; `task clean` → `tasks.register('clean', Delete)` (uses `layout.buildDirectory`) |
| `gradle/wrapper/gradle-wrapper.properties` | `gradle-8.1.1-bin.zip` → `gradle-8.9-bin.zip` |
| `app/build.gradle` | ndk-build → **CMake**. Also `namespace`/`applicationId 'br.tk90x.test'`, `compileSdk 35`, `targetSdk 35`, `minSdk 21`, `ndkVersion '27.3.13750724'`, `cmake { version '3.22.1' }`, `abiFilters 'arm64-v8a','armeabi-v7a','x86_64'`, cmake args `-DANDROID_STL=c++_static -DSDL_STATIC=OFF -DSDL_TEST=OFF`, and a release `signingConfig` read from `$TK_ANDROID/keystore/keystore.properties` |
| `app/jni/CMakeLists.txt` | `add_subdirectory(SDL)` + `add_subdirectory(src)` |
| `app/jni/src/CMakeLists.txt` | `add_library(main SHARED main.c)` + `target_link_libraries(main SDL2 SDL2main log)` |
| `app/jni/SDL` | relative symlink → `../../../SDL2-2.32.10` (SDL is compiled from source by the NDK) |
| `app/jni/Android.mk`, `src/Android.mk`, `Application.mk` | deleted (only the CMake path is used) |
| `app/src/main/AndroidManifest.xml` | activity `.TestActivity` + `android:screenOrientation="sensorLandscape"` |
| `app/src/main/java/br/tk90x/test/TestActivity.java` | new: `SDLActivity` subclass with the file picker (§6) |
| `app/src/main/assets/hello.txt` | new: an asset read via `SDL_RWFromFile("hello.txt")` |
| `app/jni/src/main.c` | new: test program |
| `app/src/main/res/values/strings.xml` | `app_name` = "TK90X Test" |

`main.c` does the following: it opens a fullscreen window and fills it with Spectrum blue. It
plays a 200 ms square-wave beep at startup and a short beep on each touch; a touch also
changes the colour. A two-finger tap opens the document picker, and a picked file turns the
screen green. The back button (`SDLK_AC_BACK`) returns from `main()`, which finishes the
Activity. The program also reads the asset and writes `settings.txt` to internal storage.

### Build commands that worked

```sh
. /home/mmalves/workspace/emuladores/tk90x/tools/android/env.sh
cd $TK_ANDROID/proof-project
./gradlew assembleDebug      # → app/build/outputs/apk/debug/app-debug.apk
./gradlew assembleRelease    # → app/build/outputs/apk/release/app-release.apk (signed with the dev key)
./gradlew installDebug       # = build + adb install on the connected device
```

`local.properties` is not needed, because `ANDROID_HOME` is enough.

Timings on this machine (12 cores):

| Build | Time |
|---|---|
| First `assembleDebug`, which also downloaded Gradle 8.9 and AGP | 2 min 00 s |
| First `assembleRelease` | 2 min 03 s |
| Clean `assembleDebug` with warm caches | 72 s |
| Incremental build after touching `main.c` (Gradle daemon running) | 14 s |
| Clean `assembleDebug assembleRelease` together | 2 min 34 s |

SDL itself is compiled once per ABI per build type, and that is most of the time.

### Results

| APK | Size | Signer |
|---|---|---|
| `app-debug.apk` | 2 242 198 bytes (2.2 MB) | `CN=Android Debug` |
| `app-release.apk` | 1 769 589 bytes (1.7 MB) | `CN=TK90X Emulator Dev, O=tk90x, C=BR`; signature schemes v1 + v2 |

Copies are in `tools/android/br.tk90x.test-{debug,release}.apk`.

```
$ aapt2 dump badging app-debug.apk
package: name='br.tk90x.test' versionCode='1' versionName='1.0' compileSdkVersion='35'
minSdkVersion:'21'   targetSdkVersion:'35'
launchable-activity: name='br.tk90x.test.TestActivity'  label='TK90X Test'
uses-feature: name='android.hardware.screen.landscape'
native-code: 'arm64-v8a' 'armeabi-v7a' 'x86_64'
$ unzip -l app-release.apk | grep lib/
  1439632  lib/arm64-v8a/libSDL2.so      9168  lib/arm64-v8a/libmain.so
  1021028  lib/armeabi-v7a/libSDL2.so    6772  lib/armeabi-v7a/libmain.so
  1561608  lib/x86_64/libSDL2.so         9248  lib/x86_64/libmain.so
```

`llvm-nm -D libmain.so` exports `SDL_main` and `Java_br_tk90x_test_TestActivity_nativeFilePicked`.

Other checks: `apksigner verify --print-certs X.apk` and `aapt2 dump xmltree --file AndroidManifest.xml X.apk`.
`apkanalyzer` (in cmdline-tools) also works: `apkanalyzer apk summary X.apk`.

## 4. Gotchas

- **AGP and Gradle versions.** The SDL 2.32.10 template ships AGP 8.1.1 with Gradle 8.1.1 and
  `compileSdkVersion 34`. AGP 8.1 is only tested up to API 34, so I moved to AGP 8.7.3 with
  Gradle 8.9 (the minimum Gradle for AGP 8.7) to use `compileSdk 35`. Both versions run on JDK 17.
  If you keep the template's AGP 8.1.1, use `compileSdk 34`/`targetSdk 34`; `platforms;android-34`
  is installed too.
- **NDK pin.** Without `ndkVersion`, AGP asks for its own built-in default NDK version, which is
  not the one installed here, so the build would fail or try to download it. The pin
  `ndkVersion '27.3.13750724'` is required. Likewise, `cmake { version '3.22.1' }` makes AGP pick
  the installed CMake.
- **minSdk.** SDL2 itself supports API 19, but NDK r26+ cannot target below API 21
  (Android 5.0), so `minSdk 21`. The template's `APP_PLATFORM=android-19` / `minSdkVersion 19`
  must go.
- **targetSdk 35.** On Android 15, apps that target 35 are drawn edge-to-edge. The emulator
  runs `SDL_WINDOW_FULLSCREEN` (immersive), so this does not matter, but in windowed mode the
  status bar would overlap the top of the picture. If that is ever a problem, `targetSdk 34` is
  fine for sideloading; Play's target requirement does not apply.
- **cmdline-tools 23.0** is the new "Android CLI" release. `sdkmanager` prints a deprecation
  notice (the replacement is `android sdk ...`), but it still works with the classic
  `"ndk;27.3.13750724"` package syntax. AGP 8.7 prints `Warning: SDK processing. This version only
  understands SDK XML versions up to 3 but an SDK XML file of version 4 was encountered` — harmless.
- **SDL is built from source** by `add_subdirectory(SDL)`. You get `libSDL2.so` (shared, loaded by
  `SDLActivity.getLibraries()` = `{"SDL2", "main"}`) and `libmain.so` containing `SDL_main`.
  The app's `main()` becomes `SDL_main` through `SDL_main.h` (included by `SDL.h`). Use
  `add_library(... SHARED)`, never `add_executable`.
- **Symlinked SDL.** A symlink at `app/jni/SDL` works. SDL's in-tree check only compares the
  source and binary dirs, and the binary dir is `app/.cxx/...`.
- **Do not call `exit()`.** Return from `main()`, and the Java side finishes the Activity.
- **Back button.** In SDL 2.32, the back key reaches C as `SDL_KEYDOWN`, sym
  `SDLK_AC_BACK`, and does not close the app by itself. Return from `main`, or call
  `SDL_AndroidBackButton()` to run the system behaviour. For the emulator, map it to a menu or
  OSD toggle and quit on a second press.
- **Lifecycle.** When the app goes to the background, SDL blocks the event loop
  (`SDL_HINT_ANDROID_BLOCK_ON_PAUSE`). On `SDL_APP_WILLENTERBACKGROUND`, pause audio and save
  state. The process may be killed afterwards without any further event.
- **Orientation.** `android:screenOrientation="sensorLandscape"` in the manifest locks landscape
  before SDL starts, and also adds an implied `uses-feature android.hardware.screen.landscape`.
  The hint `SDL_HINT_ORIENTATIONS = "LandscapeLeft LandscapeRight"` does the same from C.
- **Immersive mode.** Create the window with `SDL_WINDOW_FULLSCREEN` (or
  `SDL_WINDOW_FULLSCREEN_DESKTOP`). SDLActivity then sets
  `SYSTEM_UI_FLAG_IMMERSIVE_STICKY | HIDE_NAVIGATION | FULLSCREEN` and uses the display cutout
  (`LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES`). The template theme is already
  `android:Theme.NoTitleBar.Fullscreen`.
- **Deprecation warnings.** `javac` reports "Some input files use or override a deprecated
  API" for SDLActivity and `startActivityForResult`. They are harmless.

## 5. Files on Android

- **Bundled read-only data** (ROMs such as `48.rom` / `tk90x.rom`, demo tapes) goes in
  `app/src/main/assets/`. It lands in the APK as `assets/...`.
  `SDL_RWFromFile("roms/tk90x.rom", "rb")` with a **relative** path reads it from the APK
  through `AAssetManager`. Plain `fopen()` cannot see assets, so the emulator's ROM/tape loaders
  must go through `SDL_RWops` (or read into memory with `SDL_RWops` first). Uncompressed assets
  use the fast file-descriptor path, while compressed ones fall back to an InputStream.
  To keep extensions uncompressed: `android { androidResources { noCompress 'rom', 'tap', 'tzx' } }`.
- **Writable private storage.** `SDL_AndroidGetInternalStoragePath()` returns
  `/data/user/0/br.tk90x.test/files`. Nothing else can read it and no permission is needed.
  Use it for settings, snapshots and saved tapes. `SDL_GetPrefPath(org, app)` also returns a
  dir inside it. The cache dir is `getCacheDir()` (Java), which is `.../cache`.
- **`SDL_AndroidGetExternalStoragePath()`** returns `/storage/emulated/0/Android/data/<pkg>/files`.
  The user can reach it with a USB cable or some file managers; no permission is needed.
- **User files (`.tap`/`.tzx`/`.z80`/`.sna`) anywhere on the device.** Do not ask for
  `READ_EXTERNAL_STORAGE`: it no longer gives access since API 33, and scoped storage applies
  from API 29. Use the Storage Access Framework picker (§6). It returns a `content://` URI, not a
  path, so copy the stream into the cache dir and give C a normal file path.

## 6. Java file picker → C (as built in the proof project)

C → Java. Call an instance method of the activity from the SDL thread. Do not use `FindClass`
on a native thread, because it uses the wrong class loader. Get the class from the activity
object instead:

```c
JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
jobject act = (jobject)SDL_AndroidGetActivity();          /* local ref */
jclass cls  = (*env)->GetObjectClass(env, act);
jmethodID m = (*env)->GetMethodID(env, cls, "openFilePicker", "()V");
if (m) (*env)->CallVoidMethod(env, act, m);
(*env)->DeleteLocalRef(env, cls); (*env)->DeleteLocalRef(env, act);
```

Java (`TestActivity extends SDLActivity`). It launches `ACTION_OPEN_DOCUMENT` from the UI
thread, then copies the result into the cache dir on a worker thread:

```java
public static native void nativeFilePicked(String path);   // implemented in libmain.so

public void openFilePicker() {
    runOnUiThread(() -> {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");                       // .tap/.tzx/.z80 have no registered MIME type
        startActivityForResult(i, REQ_OPEN);
    });
}

@Override protected void onActivityResult(int req, int res, Intent data) {
    super.onActivityResult(req, res, data);
    if (req != REQ_OPEN) return;
    if (res != RESULT_OK || data == null || data.getData() == null) { nativeFilePicked(null); return; }
    Uri uri = data.getData();
    new Thread(() -> nativeFilePicked(copyToCache(uri))).start();
}
// copyToCache(): query OpenableColumns.DISPLAY_NAME, then
//   getContentResolver().openInputStream(uri) → new FileOutputStream(new File(getCacheDir(), name))
```

Java → C. The JNI symbol is named after the package, class and method, and is exported by
`libmain.so`. Because libmain is loaded with `System.loadLibrary("main")`, no `RegisterNatives`
call is needed. The function runs on a Java thread, so only hand the path over through the
thread-safe `SDL_PushEvent`:

```c
static Uint32 g_picked_event;   /* = SDL_RegisterEvents(1) right after SDL_Init */

JNIEXPORT void JNICALL
Java_br_tk90x_test_TestActivity_nativeFilePicked(JNIEnv *env, jclass cls, jstring jpath)
{
    SDL_Event ev; SDL_zero(ev); ev.type = g_picked_event;
    if (jpath) {
        const char *p = (*env)->GetStringUTFChars(env, jpath, NULL);
        ev.user.data1 = SDL_strdup(p);           /* main loop SDL_free()s it */
        (*env)->ReleaseStringUTFChars(env, jpath, p);
    }
    SDL_PushEvent(&ev);
}
```

There is a shortcut that needs no custom native code. The Java side can call the existing
`SDLActivity.onNativeDropFile(path)`, and C then receives a standard `SDL_DROPFILE` event
(`e.drop.file`, `SDL_free` it). SDLActivity already does this in `onCreate` for "Open with…"
`ACTION_VIEW` intents. To use that, enable the commented-out `VIEW` intent-filter in the manifest.
Note that it uses `getData().getPath()`, which for `content://` URIs is not a usable file path;
copy the stream first, as above.

If the app should only pick specific types, pass `i.putExtra(Intent.EXTRA_MIME_TYPES, ...)`
and still check the extension in C, because providers report `.tap` as
`application/octet-stream`.

## 7. Signing

- **Debug APK.** It is signed automatically with `android-user-home/debug.keystore`
  (`CN=Android Debug`). It can be sideloaded like any APK and is debuggable. That is enough for
  personal testing.
- **Release APK (dev key).** Its native libraries are smaller, because they are optimized
  and stripped. It is not debuggable. Key details:

  | Item | Value |
  |---|---|
  | File | `tools/android/keystore/tk90x-dev.jks` (PKCS12) |
  | Alias | `tk90x` |
  | Store password | `tk90xdev` |
  | Key password | `tk90xdev` |
  | DN | `CN=TK90X Emulator Dev, O=tk90x, C=BR` |
  | Cert SHA-256 | `05:D6:26:EB:47:2E:67:C3:8B:3D:45:36:DD:81:C1:98:7A:14:1F:F1:70:11:6C:5C:F8:18:B1:5F:E4:1E:3A:4F` |

  This is a **development key**: the password is published here on purpose, and it must not be
  used for a Play Store upload. `app/build.gradle` reads `keystore/keystore.properties`
  (`storeFile/storePassword/keyAlias/keyPassword`) from `$TK_ANDROID/keystore`. You can
  override the location with `-PtkKeystoreDir=/path`. If the file is missing, `assembleRelease`
  produces an unsigned `app-release-unsigned.apk`. Made with:

  ```sh
  keytool -genkeypair -keystore tk90x-dev.jks -storetype PKCS12 -alias tk90x -keyalg RSA \
          -keysize 2048 -validity 10000 -storepass tk90xdev -keypass tk90xdev \
          -dname "CN=TK90X Emulator Dev, O=tk90x, C=BR"
  ```

- Android only lets an installed app be updated by an APK **signed with the same key**. Pick one
  key (debug or dev-release) for the builds you hand out, and stay with it.

## 8. Installing on a phone

Over a cable, with adb:

1. On the phone, open Settings → About phone and tap "Build number" 7 times. Then go to
   Developer options and turn on "USB debugging".
2. Connect the phone and accept the RSA prompt. Then run:

   ```sh
   . tools/android/env.sh
   adb devices
   adb install -r br.tk90x.test-release.apk    # -r = replace an existing install (same key)
   adb logcat -s SDL SDL/APP TK90X             # SDL_Log output
   ```

   Over Wi-Fi on Android 11+: run `adb pair IP:PORT`, then `adb connect IP:PORT`.

Without adb (sideload):

1. Copy the APK to the phone (download, e-mail, USB file transfer).
2. Tap it in a file manager or the browser's downloads list.
3. On Android 8+, permission is per app: Android asks you to allow "Install unknown apps" for
   that file manager or browser (Settings → Apps → Special app access → Install unknown apps).
   On Android 7 and older, the switch is the global Settings → Security → "Unknown sources".
4. Play Protect may warn about an unknown developer. Choose "Install anyway". Since Android 14,
   apps with targetSdk < 23 are blocked outright; targetSdk 35 is fine.

ABIs: `arm64-v8a` covers practically all current phones, and `armeabi-v7a` covers old 32-bit
ones. `x86_64` covers Chromebooks and the x86 emulator. Leaving out `x86` (32-bit) is fine.
