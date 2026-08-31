/*
 *     Copyright (C) 2022  Filippo Scognamiglio
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

#ifndef LIBRETRODROID_SHADERMANAGER_H
#define LIBRETRODROID_SHADERMANAGER_H

#include <string>
#include <unordered_map>
#include <vector>

namespace libretrodroid {

class ShaderManager {
public:
    /**
     * How a pass's framebuffer is sized — RetroArch's `scale_typeN`.
     *
     * `SOURCE` is a multiple of the frame the core produced; `VIEWPORT` a
     * multiple of the area actually drawn on screen; `ABSOLUTE` a pixel count.
     * The distinction is not cosmetic: a bloom or blur pass written against
     * `viewport` and given `source` runs at the console's resolution instead of
     * the screen's, which is *cheaper* and visibly softer — a wrong picture that
     * looks like a slow one's opposite.
     */
    enum class ScaleType {
        SOURCE,
        VIEWPORT,
        ABSOLUTE,
    };

    struct Pass {
        std::string vertex;
        std::string fragment;
        bool linear = false;

        float scaleX = 1.0F;
        float scaleY = 1.0F;
        ScaleType scaleTypeX = ScaleType::SOURCE;
        ScaleType scaleTypeY = ScaleType::SOURCE;

        /**
         * Ask for more than 8 bits per channel — `float_framebuffer`/
         * `srgb_framebuffer`.
         *
         * A preset wanting these and not getting them does not fail, it *bands*:
         * an intermediate pass that stores light in linear space needs the range,
         * and RGBA8 quantises it. Half float rather than full: `GL_RGBA16F` is
         * what `EXT_color_buffer_half_float` guarantees, it is what these presets
         * actually want, and it is half the bandwidth of `GL_RGBA32F` on a
         * handheld's memory bus.
         */
        bool floatFramebuffer = false;
        bool srgbFramebuffer = false;

        /**
         * The shader's own `#pragma parameter` values, as float uniforms.
         *
         * Uniforms rather than `#define`s, which is the route RetroArch takes
         * and the only one that is safe: a preset written the usual way carries
         * `#ifdef PARAMETER_UNIFORM ... #else #define BLURSCALEX 0.45 #endif`,
         * so injecting a `#define` of our own puts two conflicting definitions of
         * one macro in the same translation unit. Defining `PARAMETER_UNIFORM`
         * and setting the uniform is what the shader is written to expect.
         */
        std::unordered_map<std::string, float> floats;

        bool operator==(const ShaderManager::Pass &other) const;
    };

    /**
     * A lookup texture — RetroArch's `textures = "NAME"`.
     *
     * Arrives as a **raw pixel sidecar rather than a PNG**, and deliberately:
     * decoding a PNG here would mean compiling `rpng` and zlib into this library
     * for one feature, where the caller already has `BitmapFactory`. So the
     * caller decodes, writes `LUT1` + width + height + RGBA bytes, and passes a
     * path. Twenty lines each side against a vendored image decoder.
     */
    struct Lut {
        std::string name;
        std::string path;
        bool linear = true;
        bool repeat = false;

        bool operator==(const ShaderManager::Lut &other) const;
    };

    struct Chain {
        std::vector<Pass> passes;
        bool linearTexture = false;

        /** The chain's lookup textures, bound by name. */
        std::vector<Lut> luts;

        /**
         * How many earlier *source* frames the chain reads — `Prev1`..`PrevN`.
         *
         * Zero for almost everything. Each one costs a texture the size of the
         * frame and one blit per frame to fill, so it is counted from what the
         * shaders actually reference rather than always reserving the seven
         * RetroArch allows.
         */
        int historyFrames = 0;

        bool operator==(const ShaderManager::Chain &other) const;
        bool operator!=(const ShaderManager::Chain &other) const;
    };

    enum class Type {
        SHADER_DEFAULT = 0,
        SHADER_CRT = 1,
        SHADER_LCD = 2,
        SHADER_SHARP = 3,
        SHADER_UPSCALE_CUT = 4,
        SHADER_UPSCALE_CUT2 = 5,
        SHADER_UPSCALE_CUT3 = 6,
        SHADER_CUSTOM = 7,
    };

    /**
     * The keys a SHADER_CUSTOM chain arrives under, in Config::params.
     *
     * The JNI seam is already a `Map<String, String>` (see JavaUtils::shaderFromJava),
     * so a whole shader chain travels as data through the signature that is
     * already there — no new field, no per-shader C++, and nothing new for R8 to
     * rename. `PASS_<i>_VERTEX` and `PASS_<i>_FRAGMENT` are complete GLSL source;
     * the caller is what prepends any `#define`s.
     */
    static constexpr const char* PARAM_PASSES = "PASSES";
    static constexpr const char* PARAM_LINEAR_TEXTURE = "LINEAR_TEXTURE";
    static constexpr const char* PARAM_PASS_VERTEX = "_VERTEX";
    static constexpr const char* PARAM_PASS_FRAGMENT = "_FRAGMENT";
    static constexpr const char* PARAM_PASS_LINEAR = "_LINEAR";
    static constexpr const char* PARAM_PASS_SCALE_X = "_SCALE_X";
    static constexpr const char* PARAM_PASS_SCALE_Y = "_SCALE_Y";
    static constexpr const char* PARAM_PASS_SCALE_TYPE_X = "_SCALE_TYPE_X";
    static constexpr const char* PARAM_PASS_SCALE_TYPE_Y = "_SCALE_TYPE_Y";
    static constexpr const char* PARAM_PASS_FLOAT_FB = "_FLOAT_FB";
    static constexpr const char* PARAM_PASS_SRGB_FB = "_SRGB_FB";
    static constexpr const char* PARAM_PASS_FLOAT = "_FLOAT_";
    static constexpr const char* PARAM_LUTS = "LUTS";
    static constexpr const char* PARAM_LUT_PATH = "_PATH";
    static constexpr const char* PARAM_LUT_LINEAR = "_LINEAR";
    static constexpr const char* PARAM_LUT_REPEAT = "_REPEAT";
    static constexpr const char* PARAM_HISTORY = "HISTORY";

    /**
     * The most previous frames a chain may ask to keep.
     *
     * RetroArch allows seven. Each costs a texture the size of the frame and one
     * blit per frame, and there are only so many texture units: GLES2 guarantees
     * eight in a fragment shader, and this pipeline already spends one on the
     * source, one on the previous pass, up to three on earlier passes and some
     * on LUTs. Three is what fits with room left, and no preset in the bundled
     * set asks for more than one.
     */
    static constexpr int MAX_HISTORY = 3;

    struct Config {
        Type type;
        std::unordered_map<std::string, std::string> params;

        inline bool operator==(const Config& other) {
            return type == other.type && params == other.params;
        }
    };

private:
    static const std::string defaultShaderVertex;

    static const std::string defaultShaderFragment;
    static const std::string defaultSharpFragment;
    static const std::string crtShaderFragment;
    static const std::string lcdShaderFragment;

    static const std::unordered_map<std::string, std::string> cutUpscaleParams;
    static const std::string cutUpscaleVertex;
    static const std::string cutUpscaleFragment;

    static const std::unordered_map<std::string, std::string> cut2UpscaleParams;
    static const std::string cut2UpscalePass0Vertex;
    static const std::string cut2UpscalePass0Fragment;
    static const std::string cut2UpscalePass1Vertex;
    static const std::string cut2UpscalePass1Fragment;

    static const std::unordered_map<std::string, std::string> cut3UpscaleParams;
    static const std::string cut3UpscalePass0Vertex;
    static const std::string cut3UpscalePass0Fragment;
    static const std::string cut3UpscalePass1Vertex;
    static const std::string cut3UpscalePass1Fragment;
    static const std::string cut3UpscalePass2Vertex;
    static const std::string cut3UpscalePass2Fragment;

private:
    static Chain buildCustomChain(const std::unordered_map<std::string, std::string>& params);

    /** A one-pass chain with everything else at its default. */
    static Pass plainPass(const std::string& vertex, const std::string& fragment, bool linear);

    static std::string buildDefines(
        std::unordered_map<std::string, std::string> baseParams,
        std::unordered_map<std::string, std::string> customParams
    );

public:
    static Chain getShader(const Config& config);
};

}

#endif //LIBRETRODROID_SHADERMANAGER_H
