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

#ifndef LIBRETRODROID_ES3UTILS_H
#define LIBRETRODROID_ES3UTILS_H

#include <optional>
#include <memory>

#include "GLES3/gl3.h"
#include "GLES3/gl3ext.h"

#include "../../shadermanager.h"

namespace libretrodroid {

class ES3Utils {

public:
    struct Framebuffer {
        unsigned int framebuffer = 0;
        unsigned int texture = 0;
        std::optional<unsigned int> depth = std::nullopt;
        unsigned int width = 0;
        unsigned int height = 0;
    };

public:
    using Framebuffers = std::vector<std::unique_ptr<ES3Utils::Framebuffer>>;

    /**
     * How many bits a framebuffer keeps per channel.
     *
     * `HALF_FLOAT` needs `EXT_color_buffer_half_float`, which every ES3 driver
     * this app has met provides; the fallback is not an error but a *band*, so
     * it is logged rather than thrown.
     */
    enum class Format {
        RGBA8,
        HALF_FLOAT,
        SRGB,
    };

    /**
     * One framebuffer per intermediate pass.
     *
     * @param viewportWidth the size of the area actually drawn on screen, which
     *   is what a `scale_type = viewport` pass is a multiple of. Not the same as
     *   the screen: the drawn quad is letterboxed to the core's aspect ratio.
     */
    static std::unique_ptr<Framebuffers> buildShaderPasses(
        unsigned int width,
        unsigned int height,
        unsigned int viewportWidth,
        unsigned int viewportHeight,
        const ShaderManager::Chain &shaders
    );

    static std::unique_ptr<ES3Utils::Framebuffer> createFramebuffer(
        unsigned int width,
        unsigned int height,
        bool linear,
        bool repeat,
        bool includeDepth,
        bool includeStencil,
        Format format = Format::RGBA8
    );

    static void deleteFramebuffer(std::unique_ptr<Framebuffer> data);
};

}

#endif //LIBRETRODROID_ES3UTILS_H
