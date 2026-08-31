/*
 *     Copyright (C) 2019  Filippo Scognamiglio
 *
 *     This program is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, either version 3 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

package com.swordfish.libretrodroid;

import java.util.List;

public class LibretroDroid {

    static {
        System.loadLibrary("libretrodroid");
    }

    public static final int MOTION_SOURCE_DPAD = 0;
    public static final int MOTION_SOURCE_ANALOG_LEFT = 1;
    public static final int MOTION_SOURCE_ANALOG_RIGHT = 2;
    public static final int MOTION_SOURCE_POINTER = 3;

    public static final int SHADER_DEFAULT = 0;
    public static final int SHADER_CRT = 1;
    public static final int SHADER_LCD = 2;
    public static final int SHADER_SHARP = 3;
    public static final int SHADER_UPSCALE_CUT = 4;
    public static final int SHADER_UPSCALE_CUT2 = 5;
    public static final int SHADER_UPSCALE_CUT3 = 6;
    public static final int SHADER_CUSTOM = 7;

    /**
     * The keys a {@link ShaderConfig.Custom} chain travels under.
     *
     * The JNI seam is already a {@code Map<String, String>}, so a whole shader
     * chain — GLSL source included — goes through the signature that is already
     * there. Nothing new for R8 to rename, which matters: the last two JNI
     * lookups this library gained both aborted the process in release builds
     * only.
     */
    public static final String SHADER_CUSTOM_PARAM_PASSES = "PASSES";
    public static final String SHADER_CUSTOM_PARAM_LINEAR_TEXTURE = "LINEAR_TEXTURE";
    public static final String SHADER_CUSTOM_PARAM_PASS_VERTEX = "_VERTEX";
    public static final String SHADER_CUSTOM_PARAM_PASS_FRAGMENT = "_FRAGMENT";
    public static final String SHADER_CUSTOM_PARAM_PASS_LINEAR = "_LINEAR";
    public static final String SHADER_CUSTOM_PARAM_PASS_SCALE = "_SCALE";
    public static final String SHADER_CUSTOM_PARAM_PASS_FLOAT = "_FLOAT_";

    public static final String SHADER_UPSCALE_CUT_PARAM_USE_DYNAMIC_BLEND = "USE_DYNAMIC_BLEND";
    public static final String SHADER_UPSCALE_CUT_PARAM_BLEND_MIN_CONTRAST_EDGE = "BLEND_MIN_CONTRAST_EDGE";
    public static final String SHADER_UPSCALE_CUT_PARAM_BLEND_MAX_CONTRAST_EDGE = "BLEND_MAX_CONTRAST_EDGE";
    public static final String SHADER_UPSCALE_CUT_PARAM_BLEND_MIN_SHARPNESS = "BLEND_MIN_SHARPNESS";
    public static final String SHADER_UPSCALE_CUT_PARAM_BLEND_MAX_SHARPNESS = "BLEND_MAX_SHARPNESS";
    public static final String SHADER_UPSCALE_CUT_PARAM_STATIC_BLEND_SHARPNESS = "STATIC_BLEND_SHARPNESS";
    public static final String SHADER_UPSCALE_CUT_PARAM_EDGE_USE_FAST_LUMA = "EDGE_USE_FAST_LUMA";
    public static final String SHADER_UPSCALE_CUT_PARAM_EDGE_MIN_VALUE = "EDGE_MIN_VALUE";
    public static final String SHADER_UPSCALE_CUT_PARAM_EDGE_MIN_CONTRAST = "EDGE_MIN_CONTRAST";

    public static final String SHADER_UPSCALE_CUT2_PARAM_USE_DYNAMIC_BLEND = "USE_DYNAMIC_BLEND";
    public static final String SHADER_UPSCALE_CUT2_PARAM_BLEND_MIN_CONTRAST_EDGE = "BLEND_MIN_CONTRAST_EDGE";
    public static final String SHADER_UPSCALE_CUT2_PARAM_BLEND_MAX_CONTRAST_EDGE = "BLEND_MAX_CONTRAST_EDGE";
    public static final String SHADER_UPSCALE_CUT2_PARAM_BLEND_MIN_SHARPNESS = "BLEND_MIN_SHARPNESS";
    public static final String SHADER_UPSCALE_CUT2_PARAM_BLEND_MAX_SHARPNESS = "BLEND_MAX_SHARPNESS";
    public static final String SHADER_UPSCALE_CUT2_PARAM_STATIC_BLEND_SHARPNESS = "STATIC_BLEND_SHARPNESS";
    public static final String SHADER_UPSCALE_CUT2_PARAM_EDGE_USE_FAST_LUMA = "EDGE_USE_FAST_LUMA";
    public static final String SHADER_UPSCALE_CUT2_PARAM_SOFT_EDGES_SHARPENING = "SOFT_EDGES_SHARPENING";
    public static final String SHADER_UPSCALE_CUT2_PARAM_SOFT_EDGES_SHARPENING_AMOUNT = "SOFT_EDGES_SHARPENING_AMOUNT";
    public static final String SHADER_UPSCALE_CUT2_PARAM_HARD_EDGES_SEARCH_MAX_ERROR = "HARD_EDGES_SEARCH_MAX_ERROR";

    public static final String SHADER_UPSCALE_CUT3_PARAM_USE_DYNAMIC_BLEND = "USE_DYNAMIC_BLEND";
    public static final String SHADER_UPSCALE_CUT3_PARAM_BLEND_MIN_CONTRAST_EDGE = "BLEND_MIN_CONTRAST_EDGE";
    public static final String SHADER_UPSCALE_CUT3_PARAM_BLEND_MAX_CONTRAST_EDGE = "BLEND_MAX_CONTRAST_EDGE";
    public static final String SHADER_UPSCALE_CUT3_PARAM_BLEND_MIN_SHARPNESS = "BLEND_MIN_SHARPNESS";
    public static final String SHADER_UPSCALE_CUT3_PARAM_BLEND_MAX_SHARPNESS = "BLEND_MAX_SHARPNESS";
    public static final String SHADER_UPSCALE_CUT3_PARAM_STATIC_BLEND_SHARPNESS = "STATIC_BLEND_SHARPNESS";
    public static final String SHADER_UPSCALE_CUT3_PARAM_EDGE_USE_FAST_LUMA = "EDGE_USE_FAST_LUMA";
    public static final String SHADER_UPSCALE_CUT3_PARAM_SOFT_EDGES_SHARPENING = "SOFT_EDGES_SHARPENING";
    public static final String SHADER_UPSCALE_CUT3_PARAM_SOFT_EDGES_SHARPENING_AMOUNT = "SOFT_EDGES_SHARPENING_AMOUNT";
    public static final String SHADER_UPSCALE_CUT3_PARAM_HARD_EDGES_SEARCH_MAX_ERROR = "HARD_EDGES_SEARCH_MAX_ERROR";
    public static final String SHADER_UPSCALE_CUT3_PARAM_HARD_EDGES_SEARCH_MAX_DISTANCE = "HARD_EDGES_SEARCH_MAX_DISTANCE";

    public static final int ERROR_LOAD_LIBRARY = 0;
    public static final int ERROR_LOAD_GAME = 1;
    public static final int ERROR_GL_NOT_COMPATIBLE = 2;
    public static final int ERROR_SERIALIZATION = 3;
    /** RETRO_MEMORY_SAVE_RAM, the battery-backed save. */
    public static final int MEMORY_SAVE_RAM = 0;

    /** RETRO_MEMORY_SYSTEM_RAM, the console's working RAM. */
    public static final int MEMORY_SYSTEM_RAM = 2;

    /** RETRO_MEMORY_VIDEO_RAM. */
    public static final int MEMORY_VIDEO_RAM = 3;

    public static final int ERROR_CHEAT = 4;
    public static final int ERROR_GENERIC = -1;

    public static native void create(
        int GLESVersion,
        String coreFilePath,
        String systemDir,
        String savesDir,
        Variable[] variables,
        GLRetroShader shaderConfig,
        float refreshRate,
        boolean preferLowLatencyAudio,
        boolean enableVirtualFileSystem,
        boolean enableMicrophone,
        boolean skipDuplicateFrames,
        ImmersiveMode immersiveMode,
        String language
    );

    public static native void loadGameFromPath(String gameFilePath);
    public static native void loadGameFromBytes(byte[] gameFileBytes);
    public static native void loadGameFromVirtualFiles(List<DetachedVirtualFile> virtualFiles);
    public static native void resume();

    public static native void onSurfaceCreated();
    public static native void onSurfaceChanged(int width, int height);

    public static native void pause();
    public static native void destroy();

    public static native void step(GLRetroView retroView);

    public static native void reset();

    public static native void setRumbleEnabled(boolean enabled);
    public static native void setFrameSpeed(int speed);
    public static native void setAudioEnabled(boolean enabled);
    public static native void setShaderConfig(GLRetroShader shader);
    public static native void setViewport(float x, float y, float width, float height);
    public static native void setViewportAlignment(int viewportAlignment);

    public static native byte[] serializeState();
    public static native boolean unserializeState(byte[] state);

    public static native void setCheat(int index, boolean enable, String code);
    public static native void resetCheat();

    /**
     * Activates a RetroAchievements set for the running game.
     *
     * <p>{@code consoleId} is RetroAchievements' own console numbering, and
     * {@code definitions} are the {@code MemAddr} strings from its API, one per id.
     * Returns how many rcheevos accepted — a definition it cannot parse costs that
     * achievement rather than the set.
     *
     * <p>Evaluation then runs on the emulation thread, once per frame, until
     * {@link #unloadAchievements()}. Nothing is sent anywhere: read
     * {@link #pollAchievements()} and submit them yourself.
     */
    public static native int loadAchievements(int consoleId, int[] ids, String[] definitions);

    public static native void unloadAchievements();

    /**
     * Achievement ids triggered since the last call.
     *
     * <p>Drained, so each unlock is reported exactly once. Polled rather than
     * pushed because the alternative is calling into the VM from the emulation
     * thread and blocking emulation on whatever the listener does.
     */
    public static native int[] pollAchievements();

    /** Bytes of console memory the active set is watching. Zero when inactive. */
    public static native long achievementMappedBytes();

    /**
     * A direct, zero-copy view of one of the core's memory regions.
     *
     * <p>{@code id} is a {@code RETRO_MEMORY_*} constant — see
     * {@link #MEMORY_SYSTEM_RAM} and {@link #MEMORY_SAVE_RAM}. Returns null when
     * the core maps nothing there, which is common for save RAM.
     *
     * <p>Valid until the core is unloaded, and readable while the game runs: a
     * value read between frames is consistent, one read mid-frame may be torn.
     */
    public static native java.nio.ByteBuffer getMemoryRegion(int id);

    /** The core's memory map, empty when it published none. */
    public static native MemoryDescriptor[] getMemoryMap();

    public static native byte[] serializeSRAM();
    public static native boolean unserializeSRAM(byte[] sram);

    public static native void updateVariable(Variable variable);
    public static native Variable[] getVariables();

    public static native int availableDisks();
    public static native int currentDisk();
    public static native void changeDisk(int index);

    public static native void onMotionEvent(int port, int motionSource, float xAxis, float yAxis);
    public static native void onTouchEvent(float xAxis, float yAxis);

    public static native void onKeyEvent(int port, int action, int keyCode);

    public static native void refreshAspectRatio();

    public static native Controller[][] getControllers();
    public static native void setControllerType(int port, int type);
}
