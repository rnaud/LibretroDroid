/*
 *     Copyright (C) 2025  Filippo Scognamiglio
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

#ifndef LIBRETRODROID_VIDEOLAYOUT_H
#define LIBRETRODROID_VIDEOLAYOUT_H

#include <algorithm>
#include <array>
#include <utility>

#include "utils/rect.h"

#define V_ALIGN_CENTER  0
#define V_ALIGN_TOP     1
#define V_ALIGN_BOTTOM  2

namespace libretrodroid {

class VideoLayout {
public:
    VideoLayout(bool bottomLeftOrigin, float rotation, Rect viewportRect, unsigned int viewportAlignment);

    void updateAspectRatio(float aspectRatio);

    void updateScreenSize(unsigned screenWidth, unsigned screenHeight);

    void updateViewportSize(Rect viewportRect);

    void updateViewportAlignment(unsigned int viewportAlignment);

    void updateRotation(float rotation);

    std::array<float, 12>& getForegroundVertices() { return foregroundVertices; }
    std::array<float, 12>& getBackgroundVertices() { return backgroundVertices; }
    std::array<float, 12>& getFramebufferVertices() { return framebufferVertices; }
    std::array<float, 12>& getTextureCoordinates() { return textureCoordinates; }
    std::array<float, 4>& getRelativeForegroundBounds() { return relativeForegroundBounds; }

    /**
     * The drawn quad's size in pixels — RetroArch's `OutputSize`.
     *
     * Not `screenSize`, and not `textureSize * screenDensity` either. The
     * foreground quad is letterboxed to the *core's* declared aspect ratio,
     * which for a Mega Drive's 320x224 at 4:3 is nothing like the texture's own
     * ratio — so the two shortcuts are both wrong by the pixel-aspect factor,
     * and a CRT shader's scanline count is computed straight off this number.
     *
     * Measured off the vertices rather than recomputed, so it cannot drift from
     * what was actually drawn, and taken as their bounding box so a rotated
     * quad reports the screen footprint it really covers.
     */
    std::pair<float, float> getForegroundSizeInPixels() {
        float minX = foregroundVertices[0], maxX = foregroundVertices[0];
        float minY = foregroundVertices[1], maxY = foregroundVertices[1];
        for (int i = 0; i < 6; ++i) {
            minX = std::min(minX, foregroundVertices[i * 2]);
            maxX = std::max(maxX, foregroundVertices[i * 2]);
            minY = std::min(minY, foregroundVertices[i * 2 + 1]);
            maxY = std::max(maxY, foregroundVertices[i * 2 + 1]);
        }
        return {
            (maxX - minX) * 0.5F * (float) screenWidth,
            (maxY - minY) * 0.5F * (float) screenHeight
        };
    }

    int getScreenWidth() { return screenWidth; }

    int getScreenHeight() { return screenHeight; }

    std::pair<float, float> getRelativePosition(float touchX, float touchY);

private:
    void updateBuffers();

    void updateForegroundVertices();

    void updateBackgroundVertices();

    void updateRelativeForegroundBounds();

private:
    std::array<float, 12> foregroundVertices = {
        -1.0F,
        -1.0F,

        -1.0F,
        +1.0F,

        +1.0F,
        -1.0F,

        +1.0F,
        -1.0F,

        -1.0F,
        +1.0F,

        +1.0F,
        +1.0F,
    };

    std::array<float, 12> textureCoordinates {
        0.0F,
        0.0F,

        0.0F,
        1.0F,

        1.0F,
        0.0F,

        1.0F,
        0.0F,

        0.0F,
        1.0F,

        1.0F,
        1.0F,
    };

    std::array<float, 12> backgroundVertices = {
        -1.0F,
        -1.0F,

        -1.0F,
        +1.0F,

        +1.0F,
        -1.0F,

        +1.0F,
        -1.0F,

        -1.0F,
        +1.0F,

        +1.0F,
        +1.0F,
    };

    std::array<float, 12> framebufferVertices = {
        -1.0F,
        -1.0F,

        -1.0F,
        +1.0F,

        +1.0F,
        -1.0F,

        +1.0F,
        -1.0F,

        -1.0F,
        +1.0F,

        +1.0F,
        +1.0F,
    };

    std::array<float, 4> relativeForegroundBounds = {
        +0.0F,
        +0.0F,
        +1.0F,
        +1.0F,
    };

    bool bottomLeftOrigin = false;
    float rotation = 0.0F;
    float aspectRatio = 1;
    Rect viewportRect = Rect(0.0F, 0.0F, 1.0F, 1.0F);

    unsigned screenWidth = 0;
    unsigned screenHeight = 0;

    unsigned viewportAlignment = V_ALIGN_CENTER;
};

} // namespace libretrodroid

#endif //LIBRETRODROID_VIDEOLAYOUT_H
