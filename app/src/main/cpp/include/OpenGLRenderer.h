#ifndef OPENGL_RENDERER_H
#define OPENGL_RENDERER_H

#include <jni.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>


bool initOpenGL(ANativeWindow* window, int width, int height);

void renderFrame(uint8_t* rgbaData, int width, int height);

void cleanupOpenGL(); // 释放资源

#endif // OPENGL_RENDERER_H