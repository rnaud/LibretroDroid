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

#ifndef LIBRETRODROID_RENDERER_H
#define LIBRETRODROID_RENDERER_H

#include <cstdint>
#include <utility>
#include <vector>
#include <optional>

#include "../shadermanager.h"

namespace libretrodroid {

class Renderer {
public:
    struct PassData {
        std::optional<unsigned int> framebuffer = std::nullopt;
        std::optional<unsigned int> texture = std::nullopt;
        std::optional<unsigned int> width = std::nullopt;
        std::optional<unsigned int> height = std::nullopt;

        /**
         * Every completed pass's output texture, oldest first.
         *
         * [texture] is the immediately previous one and stays for the sake of the
         * `previousPass` uniform this library has always had. This is what lets a
         * preset name an *earlier* pass — RetroArch's `Pass0`, `PassPrev2` — which
         * before now resolved to an unbound sampler, and an unbound sampler reads
         * black.
         */
        std::vector<unsigned int> completed;
    };

public:
    virtual uintptr_t getFramebuffer() = 0;
    virtual uintptr_t getTexture() = 0;
    virtual void updateRenderedResolution(unsigned width, unsigned height) = 0;
    virtual void setPixelFormat(int pixelFormat) = 0;
    virtual void onNewFrame(const void *data, unsigned width, unsigned height, size_t pitch);
    virtual bool rendersInVideoCallback() = 0;
    virtual void setShaders(ShaderManager::Chain shaders) = 0;
    virtual PassData getPassData(unsigned int layer) = 0;

    virtual ~Renderer() = default;

public:
    std::pair<int, int> lastFrameSize;

    /**
     * The size of the area actually drawn on screen, in pixels.
     *
     * Set by Video before the shaders are built, because a `scale_type =
     * viewport` pass is a multiple of *this* and the renderer knows only the
     * source frame. Not the screen size: the drawn quad is letterboxed to the
     * core's declared aspect ratio, so on a 4:3 handheld playing a 320x224 Mega
     * Drive game the two differ by the pixel-aspect factor.
     */
    std::pair<int, int> viewportSize = { 0, 0 };
};

}


#endif //LIBRETRODROID_RENDERER_H
