# poseidon android port

this repository holds the android arm64 port of the engine and game source code (codename poseidon) behind arma: cold war assault. the code has been modernized to c++20 and built with cmake and clang. it features a custom gles32 backend specifically designed for mobile devices.

this branch is dedicated entirely to the android port.

## quick start

to build the android arm64 port, navigate to the android directory and run gradle assembleDebug. this will build the native libraries using the ndk and package them into an apk using the gradle wrapper.

## layout

* apps, executable targets
* engine, engine libraries, gles32 backend, and rust trident tooling
* mserver, rust service and cli crates
* cmake, presets, toolchains, vcpkg triplets, and overlay ports
* thirdparty, vendored third party headers and sources
* android, gradle project for android builds

## license

the source in this repository is licensed under the gnu general public license version 3 or later, with additional terms under section 7 of the gpl. see the license file for the full text.

the thirdparty directory is excluded from the project's gpl license. it contains vendored third party code under their own respective licenses. dependencies pulled in via vcpkg likewise remain under their own licenses.

### game data and assets

game data and assets (models, textures, sounds, missions) are not part of this repository and are not covered by the gpl. the compiled binaries need game data to run.
