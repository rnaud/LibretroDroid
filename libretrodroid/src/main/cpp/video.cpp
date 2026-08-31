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

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <cstdlib>
#include <string>
#include <cmath>
#include <utility>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <sstream>

#include "log.h"

#include "video.h"
#include "renderers/es3/framebufferrenderer.h"
#include "renderers/es3/imagerendereres3.h"
#include "renderers/es2/imagerendereres2.h"

namespace libretrodroid {

static void printGLString(const char *name, GLenum s) {
    const char *v = (const char *) glGetString(s);
    LOGI("GL %s = %s\n", name, v);
}

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, nullptr);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, nullptr, buf);
                    LOGE("Could not compile shader %d:\n%s\n",
                         shaderType, buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }

    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        glAttachShader(program, pixelShader);
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, nullptr, buf);
                    LOGE("Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

/**
 * Tell the renderer how big the drawn area is, and rebuild if it moved.
 *
 * A `scale_type = viewport` pass is a multiple of this, and the renderer only
 * knows the source frame — so without this every such pass would be sized off
 * the console's resolution instead of the screen's. Measured off the vertices
 * that were actually drawn, so it cannot disagree with what is on screen.
 *
 * Only on a real change: `setShaders` marks the renderer dirty, which recreates
 * every framebuffer, and doing that per frame is the one way this feature could
 * cost frames rather than merely spend them.
 */
void Video::syncViewportSize() {
    auto size = videoLayout.getForegroundSizeInPixels();
    int width = std::max(1, (int) std::lround(size.first));
    int height = std::max(1, (int) std::lround(size.second));
    if (renderer->viewportSize.first == width && renderer->viewportSize.second == height) {
        return;
    }
    renderer->viewportSize = { width, height };
    // Force the chain to be rebuilt at the new size. Only matters for a chain
    // with a viewport-scaled pass, but asking whether it has one is not cheaper
    // than reloading, and this runs on a resize rather than on a frame.
    loadedShaderType = std::nullopt;
}

void Video::updateProgram() {
    if (maxTextureUnits <= 0 || maxTextureUnits == 8) {
        // Asked once, not assumed. GLES2 guarantees eight and ES3 sixteen, and
        // the difference decides whether a chain with LUTs and history can bind
        // everything it declares.
        GLint units = 8;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
        if (units > 0) maxTextureUnits = units;
    }

    if (loadedShaderType.has_value() && loadedShaderType.value() == requestedShaderConfig) {
        return;
    }

    loadedShaderType = requestedShaderConfig;

    auto shaders = ShaderManager::getShader(requestedShaderConfig);

    shadersChain = {};

    for (size_t passIndex = 0; passIndex < shaders.passes.size(); ++passIndex) {
        const auto& item = shaders.passes[passIndex];
        auto shader = ShaderChainEntry { };

        shader.gProgram = createProgram(item.vertex.data(), item.fragment.data());
        if (!shader.gProgram) {
            LOGE("Could not create gl program.");
            throw std::runtime_error("Cannot create gl program");
        }

        shader.gvPositionHandle = glGetAttribLocation(shader.gProgram, "vPosition");

        shader.gvCoordinateHandle = glGetAttribLocation(shader.gProgram, "vCoordinate");

        shader.gTextureHandle = glGetUniformLocation(shader.gProgram, "texture");

        shader.gPreviousPassTextureHandle = glGetUniformLocation(shader.gProgram, "previousPass");

        shader.gTextureSizeHandle = glGetUniformLocation(shader.gProgram, "textureSize");

        shader.gScreenDensityHandle = glGetUniformLocation(shader.gProgram, "screenDensity");

        // RetroArch's contract, looked up alongside LibretroDroid's own.
        //
        // A shader from libretro/glsl-shaders declares these itself — measured
        // across the 158 portable single-pass presets, all 158 declare
        // `MVPMatrix` and 157 declare `TextureSize`/`InputSize`/`OutputSize` —
        // so *renaming* them in the source is the wrong approach: the file has
        // `attribute vec4 VertexCoord;` in it, and a `#define` shim would turn
        // that declaration into nonsense. Answering to the names the shader
        // already uses means a preset ships as data, verbatim, with only
        // `#define VERTEX`/`#define FRAGMENT` prepended — which is how RetroArch
        // compiles them too.
        shader.gVertexCoordHandle = glGetAttribLocation(shader.gProgram, "VertexCoord");
        shader.gTexCoordHandle = glGetAttribLocation(shader.gProgram, "TexCoord");
        shader.gColorHandle = glGetAttribLocation(shader.gProgram, "COLOR");
        shader.gMVPMatrixHandle = glGetUniformLocation(shader.gProgram, "MVPMatrix");
        shader.gRetroTextureHandle = glGetUniformLocation(shader.gProgram, "Texture");
        shader.gRetroTextureSizeHandle = glGetUniformLocation(shader.gProgram, "TextureSize");
        shader.gRetroInputSizeHandle = glGetUniformLocation(shader.gProgram, "InputSize");
        shader.gRetroOutputSizeHandle = glGetUniformLocation(shader.gProgram, "OutputSize");
        shader.gFrameCountHandle = glGetUniformLocation(shader.gProgram, "FrameCount");
        shader.gFrameDirectionHandle = glGetUniformLocation(shader.gProgram, "FrameDirection");

        // Two spellings, because the corpus uses both.
        shader.gOriginalHandle = glGetUniformLocation(shader.gProgram, "OrigTexture");
        if (shader.gOriginalHandle == -1) {
            shader.gOriginalHandle = glGetUniformLocation(shader.gProgram, "Original");
        }
        // Both names, separately. A shader may declare either or both, and one
        // that declares both usually does so in order to divide them.
        shader.gOriginalTextureSizeHandle =
            glGetUniformLocation(shader.gProgram, "OrigTextureSize");
        shader.gOriginalInputSizeHandle =
            glGetUniformLocation(shader.gProgram, "OrigInputSize");

        // **Two spellings, and only one of them is the right one here.**
        // RetroArch's *slang* shaders name these `Pass2` and `Prev1`; its *GLSL*
        // shaders — which is what this pipeline runs — suffix them with
        // `Texture`: `Pass2Texture`, `PrevTexture`, `Prev1Texture`. Binding only
        // the slang names bound nothing at all, and the failure was invisible
        // rather than loud, because **an unset sampler defaults to texture unit
        // 0** — which holds the source frame. So `gb-dot-matrix` read the current
        // frame where it wanted three earlier ones and still drew a plausible
        // picture with no ghosting in it. Both spellings are looked up now; a
        // shader declares one or the other, never both.
        auto findSampler = [&](const std::string& glsl, const std::string& slang) {
            GLint location = glGetUniformLocation(shader.gProgram, glsl.c_str());
            if (location == -1) {
                location = glGetUniformLocation(shader.gProgram, slang.c_str());
            }
            return location;
        };

        // RetroArch numbers PassPrev backwards from the current pass and Pass
        // forwards from the first, so both resolve to an index into the completed
        // list rather than being kept as a name.
        for (int earlier = 0; earlier < (int) shaders.passes.size(); ++earlier) {
            GLint location = findSampler(
                "Pass" + std::to_string(earlier) + "Texture",
                "Pass" + std::to_string(earlier)
            );
            if (location != -1) shader.passSamplers.emplace_back(location, earlier);

            location = findSampler(
                "PassPrev" + std::to_string(earlier + 1) + "Texture",
                "PassPrev" + std::to_string(earlier + 1)
            );
            if (location != -1) {
                // PassPrev1 is the pass immediately before this one.
                shader.passSamplers.emplace_back(location, (int) passIndex - (earlier + 1));
            }
        }

        for (int back = 1; back <= ShaderManager::MAX_HISTORY; ++back) {
            // `PrevTexture` is one frame back; `Prev1Texture` is *two*. That is
            // RetroArch's numbering, off by one from how it reads.
            GLint location = back == 1
                ? findSampler("PrevTexture", "Prev")
                : findSampler(
                      "Prev" + std::to_string(back - 2) + "Texture",
                      "Prev" + std::to_string(back - 1)
                  );
            if (location != -1) shader.historySamplers.emplace_back(location, back);
        }

        for (int index = 0; index < (int) shaders.luts.size(); ++index) {
            GLint location =
                glGetUniformLocation(shader.gProgram, shaders.luts[index].name.c_str());
            if (location != -1) shader.lutSamplers.emplace_back(location, index);
        }

        for (const auto& parameter : item.floats) {
            GLint location = glGetUniformLocation(shader.gProgram, parameter.first.c_str());
            if (location == -1) continue;
            shader.floatUniforms.emplace_back(location, parameter.second);
        }

        shadersChain.push_back(shader);
    }

    loadLuts(shaders);
    releaseHistory(shaders.historyFrames);

    renderer->setShaders(shaders);
}

void Video::renderFrame() {
    if (skipDuplicateFrames && !isDirty) return;
    isDirty = false;

    // Counted per frame, not per pass: a two-pass chain must see one number for
    // both of its passes or an animated effect tears between them.
    frameCount++;

    glDisable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (immersiveModeEnabled) {
        immersiveMode.renderBackground(
            videoLayout.getScreenWidth(),
            videoLayout.getScreenHeight(),
            videoLayout.getBackgroundVertices(),
            videoLayout.getRelativeForegroundBounds(),
            videoLayout.getFramebufferVertices().data(),
            renderer->getTexture()
        );
    }

    syncViewportSize();
    updateProgram();
    // Before the passes run, so `Prev1` is the frame before this one rather than
    // this one. After `updateProgram`, so the ring exists and is the right size.
    updateHistory();
    for (int i = 0; i < shadersChain.size(); ++i) {
        const auto& shader = shadersChain[i];
        auto passData = renderer->getPassData(i);
        auto isLastPass = i == shadersChain.size() - 1;

        glBindFramebuffer(GL_FRAMEBUFFER, passData.framebuffer.value_or(0));

        glViewport(
            0,
            0,
            passData.width.value_or(videoLayout.getScreenWidth()),
            passData.height.value_or(videoLayout.getScreenHeight())
        );

        glUseProgram(shader.gProgram);

        auto vertices = isLastPass ? videoLayout.getForegroundVertices() : videoLayout.getFramebufferVertices();
        auto coordinates = videoLayout.getTextureCoordinates();

        // Guarded on != -1, which the originals were not.
        //
        // The built-in chains always declare `vPosition`, so the unguarded call
        // was safe by accident; a RetroArch shader declares `VertexCoord`
        // instead and passing -1 as an attribute index is GL_INVALID_VALUE with
        // nothing bound — a black screen with no error anywhere a reader would
        // look.
        auto bindArray = [](GLint handle, const float* data) {
            if (handle == -1) return;
            glVertexAttribPointer(handle, 2, GL_FLOAT, GL_FALSE, 0, data);
            glEnableVertexAttribArray(handle);
        };

        bindArray(shader.gvPositionHandle, vertices.data());
        bindArray(shader.gvCoordinateHandle, coordinates.data());
        // The same two arrays under RetroArch's names. Both are declared `vec4`
        // there and supplied as 2 floats here, which GL promotes to (x, y, 0, 1)
        // — exactly what `MVPMatrix * VertexCoord` and `TexCoord.xy` want.
        bindArray(shader.gVertexCoordHandle, vertices.data());
        bindArray(shader.gTexCoordHandle, coordinates.data());

        // COLOR is a constant, not an array, and it has to be fed.
        //
        // 128 of the 158 portable presets read `COL0` — usually as a passthrough
        // multiply — and an attribute whose array is disabled reads its generic
        // value, which defaults to (0, 0, 0, 1). Leaving it alone therefore
        // multiplies the whole picture by black on most of the corpus. RetroArch
        // supplies white; so does this.
        if (shader.gColorHandle != -1) {
            glVertexAttrib4f(shader.gColorHandle, 1.0F, 1.0F, 1.0F, 1.0F);
        }

        if (shader.gMVPMatrixHandle != -1) {
            // Identity, because videolayout.cpp already emits clip-space
            // vertices in [-1, 1] — rotation, letterboxing and the viewport are
            // all baked into the array above rather than into a matrix.
            static const GLfloat identity[16] = {
                1.0F, 0.0F, 0.0F, 0.0F,
                0.0F, 1.0F, 0.0F, 0.0F,
                0.0F, 0.0F, 1.0F, 0.0F,
                0.0F, 0.0F, 0.0F, 1.0F,
            };
            glUniformMatrix4fv(shader.gMVPMatrixHandle, 1, GL_FALSE, identity);
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderer->getTexture());
        glUniform1i(shader.gTextureHandle, 0);

        bool hasPrevious = passData.texture.has_value();
        if (hasPrevious) {
            glActiveTexture(GL_TEXTURE0 + 1);
            glBindTexture(GL_TEXTURE_2D, passData.texture.value());
            if (shader.gPreviousPassTextureHandle != -1) {
                glUniform1i(shader.gPreviousPassTextureHandle, 1);
            }
        }

        // RetroArch's `Texture` is *this* pass's input, which is the previous
        // pass's output — not the source frame. LibretroDroid's own `texture` is
        // always the source, so the two names mean the same thing on pass 0 and
        // different things after it. Getting this backwards would leave a
        // multi-pass preset running every pass on the raw frame and quietly
        // discarding the work of the ones before it.
        if (shader.gRetroTextureHandle != -1) {
            glUniform1i(shader.gRetroTextureHandle, hasPrevious ? 1 : 0);
        }

        // Unit 0 is the source frame on every pass, which is exactly what
        // `Original` means — so this costs one uniform, not a texture unit.
        if (shader.gOriginalHandle != -1) {
            glUniform1i(shader.gOriginalHandle, 0);
        }
        if (shader.gOriginalTextureSizeHandle != -1) {
            glUniform2f(
                shader.gOriginalTextureSizeHandle, getTextureWidth(), getTextureHeight()
            );
        }
        if (shader.gOriginalInputSizeHandle != -1) {
            glUniform2f(
                shader.gOriginalInputSizeHandle, getTextureWidth(), getTextureHeight()
            );
        }

        glUniform2f(shader.gTextureSizeHandle, getTextureWidth(), getTextureHeight());

        glUniform1f(shader.gScreenDensityHandle, getScreenDensity());

        if (shader.gRetroTextureSizeHandle != -1) {
            glUniform2f(shader.gRetroTextureSizeHandle, getTextureWidth(), getTextureHeight());
        }
        if (shader.gRetroInputSizeHandle != -1) {
            glUniform2f(shader.gRetroInputSizeHandle, getTextureWidth(), getTextureHeight());
        }
        if (shader.gRetroOutputSizeHandle != -1) {
            // The last pass draws the letterboxed quad on screen; the ones
            // before it fill their own framebuffer.
            if (isLastPass) {
                auto size = videoLayout.getForegroundSizeInPixels();
                glUniform2f(shader.gRetroOutputSizeHandle, size.first, size.second);
            } else {
                glUniform2f(
                    shader.gRetroOutputSizeHandle,
                    (float) passData.width.value_or(videoLayout.getScreenWidth()),
                    (float) passData.height.value_or(videoLayout.getScreenHeight())
                );
            }
        }
        if (shader.gFrameCountHandle != -1) {
            glUniform1i(shader.gFrameCountHandle, (GLint) frameCount);
        }
        // Units 0 and 1 are the source and the previous pass. Everything else
        // shares what is left, and what is left is a real limit: GLES2 guarantees
        // only eight units in a fragment shader. Binding past the maximum is a GL
        // error and a black sampler, so the loop stops and says so rather than
        // producing a picture nobody can explain.
        int unit = 2;
        auto bindTexture = [&](GLint location, unsigned int texture) {
            if (location == -1) return;
            if (unit >= maxTextureUnits) {
                LOGE("Shader wants more texture units than the driver has (%d).",
                     maxTextureUnits);
                return;
            }
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, texture);
            glUniform1i(location, unit);
            unit++;
        };

        for (const auto& sampler : shader.passSamplers) {
            int wanted = sampler.second;
            if (wanted < 0 || wanted >= (int) passData.completed.size()) continue;
            bindTexture(sampler.first, passData.completed[wanted]);
        }

        for (const auto& sampler : shader.historySamplers) {
            // history is newest-last, so one frame back is the last entry.
            int index = (int) history.size() - sampler.second;
            if (index < 0) continue;
            bindTexture(sampler.first, history[index]->texture);
        }

        for (const auto& sampler : shader.lutSamplers) {
            if (sampler.second >= (int) lutTextures.size()) continue;
            bindTexture(sampler.first, lutTextures[sampler.second]);
        }

        int boundExtras = unit - 2;

        for (const auto& parameter : shader.floatUniforms) {
            glUniform1f(parameter.first, parameter.second);
        }
        if (shader.gFrameDirectionHandle != -1) {
            // Always forwards. This player has no rewind, and a shader reading
            // -1 here would run its animation backwards for ever.
            glUniform1i(shader.gFrameDirectionHandle, 1);
        }

        glDrawArrays(GL_TRIANGLES, 0, 6);

        auto unbindArray = [](GLint handle) {
            if (handle != -1) glDisableVertexAttribArray(handle);
        };
        unbindArray(shader.gvPositionHandle);
        unbindArray(shader.gvCoordinateHandle);
        unbindArray(shader.gVertexCoordHandle);
        unbindArray(shader.gTexCoordHandle);

        for (int extra = 0; extra < boundExtras; ++extra) {
            glActiveTexture(GL_TEXTURE0 + 2 + extra);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (hasPrevious) {
            glActiveTexture(GL_TEXTURE0 + 1);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);

        glUseProgram(0);
    }
}

/**
 * Keep the last few source frames, for a chain that reads `PrevN`.
 *
 * A blit rather than a draw. The source is already a texture, so copying it
 * costs one `glBlitFramebuffer` at source resolution — a 320x224 Mega Drive
 * frame is 280 KB — where re-rendering it through a passthrough shader would
 * cost a full quad and a program change per frame.
 *
 * **The ring rotates by moving the oldest slot to the end**, which is what makes
 * this O(1): the alternative, copying frame N-1 into N-2 and so on, is one blit
 * per frame of history per frame drawn.
 */
void Video::updateHistory() {
    if (history.empty()) return;

    unsigned int width = std::max(1, (int) getTextureWidth());
    unsigned int height = std::max(1, (int) getTextureHeight());

    // The oldest slot becomes the newest. Rebuilt rather than reused when the
    // core changes resolution, which some do mid-game.
    auto slot = std::move(history.front());
    history.erase(history.begin());
    if (slot == nullptr || slot->width != width || slot->height != height) {
        ES3Utils::deleteFramebuffer(std::move(slot));
        slot = ES3Utils::createFramebuffer(width, height, true, false, false, false);
    }

    if (historyReadFramebuffer == 0) {
        glGenFramebuffers(1, &historyReadFramebuffer);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, historyReadFramebuffer);
    glFramebufferTexture2D(
        GL_READ_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        renderer->getTexture(),
        0
    );
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, slot->framebuffer);
        glBlitFramebuffer(
            0, 0, width, height,
            0, 0, width, height,
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST
        );
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    history.push_back(std::move(slot));
}

/**
 * Resize the history ring, keeping what is already in it.
 *
 * Called from `updateProgram`, which runs only when the chain changes — so a
 * parameter tweak does not throw away the frames already kept, and nothing here
 * happens per frame. Getting that wrong would mean reallocating a ring of
 * textures sixty times a second, which is the one performance trap this feature
 * has.
 */
void Video::releaseHistory(int wanted) {
    if (wanted < 0) wanted = 0;
    while ((int) history.size() > wanted) {
        ES3Utils::deleteFramebuffer(std::move(history.front()));
        history.erase(history.begin());
    }
    while ((int) history.size() < wanted) {
        unsigned int width = std::max(1, (int) getTextureWidth());
        unsigned int height = std::max(1, (int) getTextureHeight());
        history.insert(
            history.begin(),
            ES3Utils::createFramebuffer(width, height, true, false, false, false)
        );
    }
}

/**
 * Upload the chain's lookup textures.
 *
 * **Raw pixels, not PNG.** The file is `LUT1`, then width and height as
 * little-endian 32-bit ints, then tightly packed RGBA — written by the caller,
 * which already has an image decoder, rather than compiling one in here for a
 * single feature.
 *
 * Loaded when the chain changes and not before: a LUT is a few hundred KB and a
 * chain that declares none pays nothing.
 */
void Video::loadLuts(const ShaderManager::Chain& chain) {
    releaseLuts();
    if (chain.luts.empty()) return;

    for (const auto& lut : chain.luts) {
        FILE* file = fopen(lut.path.c_str(), "rb");
        if (file == nullptr) {
            LOGE("Cannot open LUT %s at %s", lut.name.c_str(), lut.path.c_str());
            continue;
        }

        char magic[4] = { 0 };
        int32_t width = 0;
        int32_t height = 0;
        bool header = fread(magic, 1, 4, file) == 4 &&
            fread(&width, sizeof(int32_t), 1, file) == 1 &&
            fread(&height, sizeof(int32_t), 1, file) == 1 &&
            std::string(magic, 4) == "LUT1" &&
            width > 0 && height > 0 && width <= 4096 && height <= 4096;
        if (!header) {
            LOGE("LUT %s has a bad header", lut.name.c_str());
            fclose(file);
            continue;
        }

        size_t bytes = (size_t) width * (size_t) height * 4;
        std::vector<uint8_t> pixels(bytes);
        size_t read = fread(pixels.data(), 1, bytes, file);
        fclose(file);
        if (read != bytes) {
            LOGE("LUT %s is short: %zu of %zu bytes", lut.name.c_str(), read, bytes);
            continue;
        }

        unsigned int texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
            pixels.data()
        );
        GLint wrap = lut.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        GLint filter = lut.linear ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glBindTexture(GL_TEXTURE_2D, 0);

        lutTextures.push_back(texture);
        LOGI("Loaded LUT %s, %dx%d", lut.name.c_str(), width, height);
    }
}

void Video::releaseLuts() {
    for (auto texture : lutTextures) {
        glDeleteTextures(1, &texture);
    }
    lutTextures.clear();
}

float Video::getScreenDensity() {
    return std::min(videoLayout.getScreenWidth() / getTextureWidth(), videoLayout.getScreenHeight() / getTextureHeight());
}

float Video::getTextureWidth() {
    return renderer->lastFrameSize.first;
}

float Video::getTextureHeight() {
    return renderer->lastFrameSize.second;
}

void Video::onNewFrame(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (data != nullptr) {
        renderer->onNewFrame(data, width, height, pitch);
        isDirty = true;
    }
}

void Video::updateScreenSize(unsigned width, unsigned height) {
    videoLayout.updateScreenSize(width, height);
}

void Video::updateViewportSize(Rect viewportRect) {
    videoLayout.updateViewportSize(viewportRect);
}

void Video::updateViewportAlignment(unsigned int viewportAlignment) {
    videoLayout.updateViewportAlignment(viewportAlignment);
}

void Video::updateRendererSize(unsigned int width, unsigned int height) {
    LOGD("Updating renderer size: %d x %d", width, height);
    renderer->updateRenderedResolution(width, height);
}

void Video::updateRotation(float rotation) {
    videoLayout.updateRotation(rotation);
}

Video::Video(
    RenderingOptions renderingOptions,
    ShaderManager::Config shaderConfig,
    bool bottomLeftOrigin,
    float rotation,
    bool skipDuplicateFrames,
    bool immersiveModeEnabled,
    Rect viewportRect,
    ImmersiveMode::Config immersiveModeConfig,
    unsigned int viewportAlignment
) :
    requestedShaderConfig(std::move(shaderConfig)),
    skipDuplicateFrames(skipDuplicateFrames),
    immersiveModeEnabled(immersiveModeEnabled),
    immersiveMode(immersiveModeConfig),
    videoLayout(bottomLeftOrigin, rotation, viewportRect, viewportAlignment) {

    printGLString("Version", GL_VERSION);
    printGLString("Vendor", GL_VENDOR);
    printGLString("Renderer", GL_RENDERER);
    printGLString("Extensions", GL_EXTENSIONS);
    initializeGLESLogCallbackIfNeeded();

    LOGI("Initializing graphics");

    glViewport(0, 0, videoLayout.getScreenWidth(), videoLayout.getScreenHeight());

    glUseProgram(0);

    initializeRenderer(renderingOptions);
}

void Video::updateShaderType(ShaderManager::Config shaderConfig) {
    requestedShaderConfig = std::move(shaderConfig);
}

void Video::initializeRenderer(RenderingOptions renderingOptions) {
    auto shaders = ShaderManager::getShader(requestedShaderConfig);

    if (renderingOptions.hardwareAccelerated) {
        renderer = new FramebufferRenderer(
            renderingOptions.width,
            renderingOptions.height,
            renderingOptions.useDepth,
            renderingOptions.useStencil,
            std::move(shaders)
        );
    } else {
        if (renderingOptions.openglESVersion >= 3) {
            renderer = new ImageRendererES3();
        } else {
            renderer = new ImageRendererES2();
        }
    }

    renderer->setPixelFormat(renderingOptions.pixelFormat);
    updateProgram();
}

void Video::updateAspectRatio(float aspectRatio) {
    videoLayout.updateAspectRatio(aspectRatio);
}

} //namespace libretrodroid
