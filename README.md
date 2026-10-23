# LibretroDroid
LibretroDroid is a simple C++ LibRetro frontend library for Android. It's powering [Lemuroid](https://github.com/Swordfish90/Lemuroid).

### Supported features:
* 2D Cores
* GL Cores
* Audio
* Gamepad events
* Serialization and Deserialization of game states
* Serialization and Deserialization of SaveRAM
* Simple shader effects (CRT and LCD)
* Touchscreen
* Multiple disk support
* Core variables

### Tested working cores:
* Stella
* Gambatte
* mGBA
* Mupen64Plus
* Snes9x
* QuickNES
* fceumm
* nestopia
* PPSSPP
* fbneo
* picodrive
* Genesis Plus GX
* DeSmuME
* PCSXReARMed

### Setup
LibretroDroid can be added to a standard build.gradle file.
```
[app/build.gradle]
dependencies {
    ...
    implementation 'com.github.swordfish90:libretrodroid:<version>'
    ...
}
```


### Optional Android ARM64 Vulkan driver

`GLRetroViewData.vulkanDriverLibrary` accepts an absolute path to a custom
AdrenoTools driver in the application's private storage. Leaving it null uses
the system driver. The hook directory comes from `applicationInfo.nativeLibraryDir`;
Android packaging must keep `jniLibs.useLegacyPackaging = true`. Driver selection
belongs to the player session and is cleared during teardown. Applications must
validate driver compatibility and downloads before setting the path.

AdrenoTools is pinned as a recursive submodule (BSD-2-Clause); its shared hooks
are packaged only for ARM64. RPCS3's static constructors install SIGSEGV, SIGILL
and SIGPIPE handlers, so their previous values are captured before dlopen and
restored before unloading RPCS3. Stop and join the Android GL view before
unloading cores with thread-local destructors, including RPCS3 and Vita3K.
