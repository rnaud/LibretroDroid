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
    struct Pass {
        std::string vertex;
        std::string fragment;
        bool linear;
        float scale;

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

    struct Chain {
        std::vector<Pass> passes;
        bool linearTexture;

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
    static constexpr const char* PARAM_PASS_SCALE = "_SCALE";
    static constexpr const char* PARAM_PASS_FLOAT = "_FLOAT_";

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

    static std::string buildDefines(
        std::unordered_map<std::string, std::string> baseParams,
        std::unordered_map<std::string, std::string> customParams
    );

public:
    static Chain getShader(const Config& config);
};

}

#endif //LIBRETRODROID_SHADERMANAGER_H
