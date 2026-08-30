cmake_minimum_required(VERSION 3.13.2)

# now build app's shared lib
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall")

add_definitions("-DVFS_FRONTEND -DHAVE_STRL")

# Let's include oboe
set (OBOE_DIR oboe)
add_subdirectory (${OBOE_DIR} oboe)
include_directories (${OBOE_DIR}/include)
include_directories (${OBOE_DIR}/src)

include_directories("libretro/libretro-common/include")

# rcheevos, for RetroAchievements condition evaluation.
#
# The runtime, the libretro memory mapping and rhash. Not rc_client or rapi:
# those are the networking half and the app already does that in Kotlin.
#
# rhash is not optional even though the app hashes ROMs itself -- rc_libretro.c
# includes disk identification, which references md5 and the hash iterator, and
# the link is --no-undefined. Having it here is worth more than it costs anyway:
# rcheevos' own hashing knows the per-console rules (an iNES header skipped, a
# disc executable pulled out) that a plain file md5 gets wrong.
include_directories("rcheevos/include")
include_directories("rcheevos/src")

set (RCHEEVOS
        rcheevos/src/rcheevos/alloc.c
        rcheevos/src/rcheevos/condition.c
        rcheevos/src/rcheevos/condset.c
        rcheevos/src/rcheevos/consoleinfo.c
        rcheevos/src/rcheevos/format.c
        rcheevos/src/rcheevos/lboard.c
        rcheevos/src/rcheevos/memref.c
        rcheevos/src/rcheevos/operand.c
        rcheevos/src/rcheevos/rc_validate.c
        rcheevos/src/rcheevos/richpresence.c
        rcheevos/src/rcheevos/runtime.c
        rcheevos/src/rcheevos/runtime_progress.c
        rcheevos/src/rcheevos/trigger.c
        rcheevos/src/rcheevos/value.c
        rcheevos/src/rc_libretro.c
        rcheevos/src/rc_util.c
        rcheevos/src/rc_compat.c
        rcheevos/src/rc_version.c
        rcheevos/src/rhash/hash.c
        rcheevos/src/rhash/hash_disc.c
        rcheevos/src/rhash/hash_encrypted.c
        rcheevos/src/rhash/hash_rom.c
        rcheevos/src/rhash/hash_zip.c
        rcheevos/src/rhash/cdreader.c
        rcheevos/src/rhash/aes.c
        rcheevos/src/rhash/md5.c
)

set (LIBRETRO_COMMON
        libretro/libretro-common/vfs/vfs_implementation.c
        libretro/libretro-common/string/stdstring.c
        libretro/libretro-common/encodings/encoding_utf.c
        libretro/libretro-common/file/file_path.c
        libretro/libretro-common/time/rtime.c
)

add_library(libretrodroid SHARED
        ${RCHEEVOS}
        achievements.h
        achievements.cpp
        libretrodroidjni.h
        libretrodroidjni.cpp
        libretrodroid.h
        libretrodroid.cpp
        log.h
        core.h
        core.cpp
        video.h
        video.cpp
        immersivemode.h
        immersivemode.cpp
        videolayout.h
        videolayout.cpp
        renderers/renderer.h
        renderers/renderer.cpp
        renderers/es3/es3utils.h
        renderers/es3/es3utils.cpp
        renderers/es3/framebufferrenderer.h
        renderers/es3/framebufferrenderer.cpp
        renderers/es2/imagerendereres2.h
        renderers/es2/imagerendereres2.cpp
        renderers/es3/imagerendereres3.h
        renderers/es3/imagerendereres3.cpp
        audio.h
        audio.cpp
        resamplers/resampler.h
        resamplers/linearresampler.h
        resamplers/linearresampler.cpp
        resamplers/sincresampler.h
        resamplers/sincresampler.cpp
        fpssync.h
        fpssync.cpp
        environment.h
        environment.cpp
        input.h
        input.cpp
        shadermanager.h
        shadermanager.cpp
        rumble.h
        rumble.cpp
        rumblestate.h
        rumblestate.cpp
        utils/javautils.h
        utils/javautils.cpp
        utils/utils.cpp
        utils/utils.h
        utils/jnistring.h
        utils/jnistring.cpp
        utils/libretrodroidexception.h
        utils/libretrodroidexception.cpp
        utils/rect.h
        utils/rect.cpp
        errorcodes.h
        errorcodes.cpp
        vfs/vfs.h
        vfs/vfs.cpp
        vfs/vfsfile.h
        vfs/vfsfile.cpp
        vfs/fdwrapper.h
        vfs/fdwrapper.cpp
        microphone/microphone.h
        microphone/microphone.cpp
        microphone/microphoneinterface.h
        microphone/microphoneinterface.cpp
        ${LIBRETRO_COMMON}
)

# Support 16k pages for Android 15
target_link_options(libretrodroid PRIVATE "-Wl,-z,max-page-size=16384")

# add lib dependencies
target_link_libraries(libretrodroid
                      android
                      log 
                      EGL
                      oboe
                      GLESv3
)
