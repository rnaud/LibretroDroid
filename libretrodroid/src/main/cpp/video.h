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

#ifndef LIBRETRODROID_VIDEO_H
#define LIBRETRODROID_VIDEO_H

#include <GLES2/gl2.h>
#include <optional>
#include <array>
#include <utility>
#include <vector>

#include "renderers/es3/es3utils.h"
#include "renderers/renderer.h"
#include "shadermanager.h"
#include "utils/rect.h"
#include "immersivemode.h"
#include "videolayout.h"

namespace libretrodroid {

class Video {
public:

    struct RenderingOptions {
        bool hardwareAccelerated = false;
        unsigned int width;
        unsigned int height;
        bool useDepth;
        bool useStencil;
        int openglESVersion;
        int pixelFormat;
    };

    struct ShaderChainEntry {
        GLint gProgram = 0;
        GLint gvPositionHandle = 0;
        GLint gvCoordinateHandle = 0;
        GLint gTextureHandle = 0;
        GLint gPreviousPassTextureHandle = 0;
        GLint gScreenDensityHandle = 0;
        GLint gTextureSizeHandle = 0;

        // RetroArch's own names for the same things, so a shader written for
        // RetroArch compiles here unmodified. Every one is -1 when the shader
        // does not declare it, which is what the built-in chains all are.
        GLint gVertexCoordHandle = -1;
        GLint gTexCoordHandle = -1;
        GLint gColorHandle = -1;
        GLint gMVPMatrixHandle = -1;
        GLint gRetroTextureHandle = -1;
        GLint gRetroTextureSizeHandle = -1;
        GLint gRetroInputSizeHandle = -1;
        GLint gRetroOutputSizeHandle = -1;
        GLint gFrameCountHandle = -1;
        GLint gFrameDirectionHandle = -1;

        /**
         * `Original` / `OrigTexture` — the frame the core produced, before any
         * pass touched it.
         *
         * Free to supply, because unit 0 already holds exactly that. Not
         * supplying it is not free: `crt-interlaced-halation` blends its blurred
         * passes back against the original, and with the sampler unbound reading
         * black the blend saturated and the whole screen came out **pure white**.
         * It compiled, linked and ran at full frame rate doing it.
         */
        GLint gOriginalHandle = -1;

        /**
         * `OrigTextureSize` and `OrigInputSize` — **both**, not either.
         *
         * These were one handle taking whichever name resolved, and that was a
         * bug with a spectacular symptom: `crt-interlaced-halation` declares both
         * and *divides one by the other*
         * (`cd *= OrigTextureSize / OrigInputSize`), so leaving the second at zero
         * gave it infinity, the composite saturated, and the shader rendered
         * **pure white**. It compiled, linked and ran at full frame rate doing it,
         * and was dropped from the bundled set for two releases as unexplainable.
         *
         * For this pipeline the original frame *is* the source texture, so both
         * are the frame size and the ratio is 1 — which is exactly what the shader
         * expects when the texture is not padded.
         */
        GLint gOriginalTextureSizeHandle = -1;
        GLint gOriginalInputSizeHandle = -1;

        /**
         * `Pass0`..`PassN` and `PassPrev1`..`PassPrevN`, resolved once at link.
         *
         * Location and the index of the completed pass it wants, so the bind
         * loop needs no arithmetic and a name the shader does not declare costs
         * nothing per frame.
         */
        std::vector<std::pair<GLint, int>> passSamplers;

        /** `Prev1`..`PrevN` — location and how many frames back. */
        std::vector<std::pair<GLint, int>> historySamplers;

        /** The chain's LUTs — location and index into [lutTextures]. */
        std::vector<std::pair<GLint, int>> lutSamplers;

        /**
         * The preset's own `#pragma parameter` uniforms, resolved once at link.
         *
         * Location and value together, because the names are the shader's and
         * there is nothing to look them up against later. A location of -1 is
         * dropped here rather than checked every frame: a preset commonly
         * declares a parameter it only uses in one of the two stages, or behind
         * an `#ifdef` that is off.
         */
        std::vector<std::pair<GLint, float>> floatUniforms;
    };

    Video(
        RenderingOptions renderingOptions,
        ShaderManager::Config shaderConfig,
        bool bottomLeftOrigin,
        float rotation,
        bool skipDuplicateFrames,
        bool immersiveMode,
        Rect viewportRect,
        ImmersiveMode::Config immersiveModeConfig,
        unsigned int viewportAlignment
    );

    VideoLayout& getLayout() { return videoLayout; }

    void updateAspectRatio(float aspectRatio);
    void updateScreenSize(unsigned screenWidth, unsigned screenHeight);
    void updateViewportSize(Rect viewportRect);
    void updateViewportAlignment(unsigned int viewportAlignment);
    void updateRendererSize(unsigned width, unsigned height);
    void updateRotation(float rotation);
    void updateShaderType(ShaderManager::Config shaderConfig);

    void renderFrame();

    void onNewFrame(const void *data, unsigned width, unsigned height, size_t pitch);

    uintptr_t getCurrentFramebuffer() {
        return renderer->getFramebuffer();
    };

    bool rendersInVideoCallback() {
        return renderer->rendersInVideoCallback();
    }

private:
    void updateProgram();

    float getScreenDensity();
    float getTextureWidth();
    float getTextureHeight();

    void initializeRenderer(RenderingOptions renderingOptions);

private:
    ShaderManager::Config requestedShaderConfig = ShaderManager::Config {
        ShaderManager::Type::SHADER_DEFAULT
    };
    std::optional<ShaderManager::Config> loadedShaderType = std::nullopt;

    bool isDirty = false;
    bool skipDuplicateFrames = false;

    /**
     * Frames drawn since the view was created — RetroArch's `FrameCount`.
     *
     * Only 11 of the 158 portable single-pass presets actually read it (the rest
     * merely declare it in the shared boilerplate), but the ones that do are the
     * animated ones — interlacing, flicker, dithering — and without it they are
     * a static pattern rather than the effect they describe.
     */
    unsigned int frameCount = 0;

    /**
     * Earlier source frames, newest last, for a chain that reads `PrevN`.
     *
     * Empty for almost everything. Filled by blitting the source into the oldest
     * slot each frame and rotating, rather than by copying textures around: one
     * `glBlitFramebuffer` at source resolution is the cheapest way to keep a
     * frame, and the alternative is an extra full-screen draw.
     */
    std::vector<std::unique_ptr<ES3Utils::Framebuffer>> history;

    /**
     * A framebuffer that exists only to be read from.
     *
     * The source is a plain texture on the image renderers, not a framebuffer,
     * so there is nothing to blit *from* without one. The source texture is
     * attached to this each frame — one call, and it avoids a second code path
     * for the two renderer kinds.
     */
    unsigned int historyReadFramebuffer = 0;

    /** The chain's lookup textures, in the order the chain declared them. */
    std::vector<unsigned int> lutTextures;

    /** How many texture units the driver will let a fragment shader sample. */
    int maxTextureUnits = 8;

    void syncViewportSize();
    void updateHistory();
    void loadLuts(const ShaderManager::Chain& chain);
    void releaseLuts();
    void releaseHistory(int wanted);

    std::vector<ShaderChainEntry> shadersChain;

    bool immersiveModeEnabled = false;
    ImmersiveMode immersiveMode;
    VideoLayout videoLayout;

    Renderer* renderer;
};

}

#endif //LIBRETRODROID_VIDEO_H
