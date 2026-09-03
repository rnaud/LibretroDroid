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

#include "framebufferrenderer.h"
#include "es3utils.h"
#include "../../log.h"

namespace libretrodroid {

FramebufferRenderer::FramebufferRenderer(
    unsigned width,
    unsigned height,
    bool depth,
    bool stencil,
    ShaderManager::Chain shaders
) {
    this->depth = depth;
    this->stencil = stencil;
    this->width = width;
    this->height = height;
    this->shaders = std::move(shaders);

    initializeBuffers();
}

void FramebufferRenderer::onNewFrame(const void *data, unsigned width, unsigned height, size_t pitch) {
    Renderer::onNewFrame(data, width, height, pitch);

    if (isDirty) {
        initializeBuffers();
        isDirty = false;
    }
}

void FramebufferRenderer::initializeBuffers() {
    // The intermediate pass buffers are ours, so they are rebuilt freely — and
    // deleted first, which they were not: assigning over the vector drops the
    // handles without asking GL to release them, so every shader change leaked
    // one framebuffer and one texture per pass.
    if (framebuffers != nullptr) {
        for (auto& pass : *framebuffers) {
            ES3Utils::deleteFramebuffer(std::move(pass));
        }
    }
    framebuffers = ES3Utils::buildShaderPasses(
        width, height, viewportSize.first, viewportSize.second, shaders
    );

    /*
     * **The core's render target is not ours to recreate for a shader change.**
     *
     * This framebuffer is what `get_current_framebuffer` hands a
     * hardware-rendered core, and the core is told to rebuild its GL state only
     * when the whole [Video] is built — `hw_context_reset` is called from
     * `onSurfaceCreated` and nowhere else. So deleting this FBO to answer a
     * shader change left the core drawing into a name GL no longer knows, and
     * every frame after that composited an empty texture: **a black screen at a
     * healthy 60 fps, for the rest of the session.**
     *
     * Measured, N64 on `mupen64plus_next_gles3`: mean luma 23.9 before Select +
     * R1 and 0.00 after it, for both of the next two shaders, while the frame
     * counter stayed at 59-60. The same walk on `genesis_plus_gx` — a software
     * core, so [ImageRendererES3] and no core-owned framebuffer at all — kept
     * its picture throughout, which is what made this specific to the hardware
     * path rather than to the shaders.
     *
     * A single-pass chain makes it plain: `buildShaderPasses` allocates nothing
     * for one pass, so switching between two single-pass shaders changed
     * *nothing else here* — the recreation below was the whole of the damage.
     *
     * Size is the only thing that still forces it, because an FBO cannot be
     * resized. That path needs the core's `context_reset` to follow it and does
     * not have it; it is reachable only through `updateRenderedResolution`,
     * which is a geometry change rather than anything the user does.
     */
    auto size = std::pair<unsigned, unsigned> { width, height };
    if (framebuffer == nullptr || framebufferSize != size) {
        ES3Utils::deleteFramebuffer(std::move(framebuffer));
        framebuffer = ES3Utils::createFramebuffer(
            width,
            height,
            shaders.linearTexture,
            false,
            depth,
            stencil
        );
        framebufferSize = size;
        framebufferLinear = shaders.linearTexture;
        return;
    }

    // Whether the core's frame is sampled linearly is the one thing a shader
    // does get to say about this texture — `ShaderConfig.Default` asks for
    // linear where `Nearest` does not. It is a texture parameter, so it is set
    // on the texture rather than paid for with a new one.
    if (framebufferLinear != shaders.linearTexture) {
        framebufferLinear = shaders.linearTexture;
        GLint filter = framebufferLinear ? GL_LINEAR : GL_NEAREST;
        glBindTexture(GL_TEXTURE_2D, framebuffer->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

uintptr_t FramebufferRenderer::getTexture() {
    return framebuffer->texture;
}

uintptr_t FramebufferRenderer::getFramebuffer() {
    return framebuffer->framebuffer;
}

void FramebufferRenderer::setPixelFormat(int pixelFormat) {
    // TODO... Here we should handle 32bit framebuffers.
}

void FramebufferRenderer::updateRenderedResolution(unsigned int width, unsigned int height) {
    if (this->width != width || this->height != height) {
        this->width = width;
        this->height = height;
        isDirty = true;
    }
}

bool FramebufferRenderer::rendersInVideoCallback() {
    return true;
}

void FramebufferRenderer::setShaders(ShaderManager::Chain shaders) {
    if (shaders != this->shaders) {
        this->shaders = shaders;
        isDirty = true;
    }
}

Renderer::PassData FramebufferRenderer::getPassData(unsigned int layer) {
    PassData result;

    if (layer >= 0 && layer < framebuffers->size()) {
        result.framebuffer = framebuffers->at(layer)->framebuffer;
        result.width = framebuffers->at(layer)->width;
        result.height = framebuffers->at(layer)->height;
    }

    if (layer > 0 && layer < framebuffers->size() + 1) {
        result.texture = framebuffers->at(layer - 1)->texture;
    }

    // Every pass that has already run this frame, so a preset can name one by
    // number rather than only reaching the one immediately before it.
    for (unsigned int done = 0; done < layer && done < framebuffers->size(); ++done) {
        result.completed.push_back(framebuffers->at(done)->texture);
    }

    return result;
}

} //namespace libretrodroid
