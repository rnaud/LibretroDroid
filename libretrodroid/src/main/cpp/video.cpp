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

void Video::updateProgram() {
    if (loadedShaderType.has_value() && loadedShaderType.value() == requestedShaderConfig) {
        return;
    }

    loadedShaderType = requestedShaderConfig;

    auto shaders = ShaderManager::getShader(requestedShaderConfig);

    shadersChain = {};

    std::for_each(shaders.passes.begin(), shaders.passes.end(), [&](const auto& item){
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

        for (const auto& parameter : item.floats) {
            GLint location = glGetUniformLocation(shader.gProgram, parameter.first.c_str());
            if (location == -1) continue;
            shader.floatUniforms.emplace_back(location, parameter.second);
        }

        shadersChain.push_back(shader);
    });

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

    updateProgram();
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

        if (hasPrevious) {
            glActiveTexture(GL_TEXTURE0 + 1);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);

        glUseProgram(0);
    }
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
