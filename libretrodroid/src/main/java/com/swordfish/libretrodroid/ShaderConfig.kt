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

package com.swordfish.libretrodroid

sealed interface ShaderConfig {

    object Default : ShaderConfig
    object CRT : ShaderConfig
    object LCD : ShaderConfig
    object Sharp : ShaderConfig

    /**
     * A chain of passes given as GLSL source, rather than one of the built-ins.
     *
     * This is what makes a RetroArch `.glslp` preset usable: the library answers
     * to RetroArch's own uniform and attribute names — `VertexCoord`, `TexCoord`,
     * `COLOR`, `MVPMatrix`, `Texture`, `TextureSize`, `InputSize`, `OutputSize`,
     * `FrameCount`, `FrameDirection` — alongside its own, so a preset from
     * libretro/glsl-shaders compiles unmodified. The caller prepends
     * `#define VERTEX` or `#define FRAGMENT` to select the half it wants, which
     * is how RetroArch compiles those single-file shaders too.
     *
     * Two limits, and they are the honest boundary of this approach: there is no
     * frame history, so a preset reading `PrevN`, `Feedback` or `PassPrev` cannot
     * run; and an intermediate pass is sized as a multiple of the *source*
     * frame, so `scale_type = viewport` cannot be expressed. Measured against
     * libretro/glsl-shaders, 176 of its 619 presets are inside both limits.
     *
     * @param linearTexture how the source frame is sampled — RetroArch's
     *   `filter_linear0`. False is nearest, which is what RetroArch on Android
     *   defaults to.
     */
    data class Custom(
        val passes: List<Pass>,
        val linearTexture: Boolean = false,
    ) : ShaderConfig {

        /**
         * @param linear how *this* pass's output is sampled by the next one,
         *   which is RetroArch's `filter_linear` for the following pass rather
         *   than for this one.
         * @param scale this pass's framebuffer size as a multiple of the source
         *   frame — RetroArch's `scaleN` with `scale_typeN = source`. Ignored on
         *   the last pass, which draws straight to the screen.
         */
        data class Pass(
            val vertex: String,
            val fragment: String,
            val linear: Boolean = false,
            val scale: Float = 1.0f,
            /**
             * The preset's `#pragma parameter` values, by the shader's own name.
             *
             * Set as float uniforms, which is what the shaders are written to
             * expect: the usual shape is
             * `#ifdef PARAMETER_UNIFORM uniform float FOO; #else #define FOO 0.45 #endif`,
             * so a caller that defines `PARAMETER_UNIFORM` and sets `FOO` here
             * gets the branch the author intended. A name no pass declares is
             * dropped at link rather than being an error, so passing the whole
             * map to every pass is correct.
             */
            val floats: Map<String, Float> = emptyMap(),
        )
    }

    data class CUT(
        val useDynamicBlend: Boolean = true,
        val blendMinContrastEdge: Float = 0.0f,
        val blendMaxContrastEdge: Float = 1.0f,
        val blendMinSharpness: Float = 0.0f,
        val blendMaxSharpness: Float = 1.0f,
        val staticSharpness: Float = 0.5f,
        val edgeUseFastLuma: Boolean = true,
        val edgeMinValue: Float = 0.05f,
        val edgeMinContrast: Float = 2.00f,
    ) : ShaderConfig

    data class CUT2(
        val useDynamicBlend: Boolean = true,
        val blendMinContrastEdge: Float = 0.00f,
        val blendMaxContrastEdge: Float = 0.50f,
        val blendMinSharpness: Float = 0.0f,
        val blendMaxSharpness: Float = 0.75f,
        val staticSharpness: Float = 0.75f,
        val edgeUseFastLuma: Boolean = false,
        val softEdgesSharpening: Boolean = true,
        val softEdgesSharpeningAmount: Float = 1.0f,
        val hardEdgesSearchMaxError: Float = 0.25f,
    ) : ShaderConfig

    data class CUT3(
        val useDynamicBlend: Boolean = true,
        val blendMinContrastEdge: Float = 0.00f,
        val blendMaxContrastEdge: Float = 0.50f,
        val blendMinSharpness: Float = 0.0f,
        val blendMaxSharpness: Float = 0.75f,
        val staticSharpness: Float = 0.75f,
        val edgeUseFastLuma: Boolean = false,
        val softEdgesSharpening: Boolean = true,
        val softEdgesSharpeningAmount: Float = 1.0f,
        val hardEdgesSearchMaxError: Float = 0.25f,
        val hardEdgesSearchMaxDistance: Int = 4,
    ) : ShaderConfig
}
