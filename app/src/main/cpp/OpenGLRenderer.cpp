#include "OpenGLRenderer.h"
#include <stdlib.h>
#include <string.h>


#define LOG_TAG "OpenGLRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)


//
static EGLDisplay eglDisplay = EGL_NO_DISPLAY;
static EGLSurface eglSurface = EGL_NO_SURFACE;
static EGLContext eglContext = EGL_NO_CONTEXT;
static GLuint programObject = 0;
static GLuint textureId = 0;
static GLint attrPosition = -1;
static GLint attrTexCoord = -1;
static GLint uniTexture = -1;
static GLuint vbo = 0;


static const GLfloat vertices[] = {
        // 位置           // 纹理坐标
        -1.0f,  1.0f, 0.0f,   0.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,   0.0f, 1.0f,
        1.0f,  1.0f, 0.0f,   1.0f, 0.0f,
        1.0f, -1.0f, 0.0f,   1.0f, 1.0f,
};


// 纹理
static GLuint loadShader(GLenum shaderType, const char* source) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            char info[512] = {0};
            glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
            LOGE("Could not compile shader %d: %s", shaderType, info);
            glDeleteShader(shader);
            shader = 0;
        }
    }
    return shader;
}

static bool initEGL(ANativeWindow* window) {
    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY) {
        LOGE("无法获取 EGLDisplay");
        return false;
    }
    if (eglInitialize(eglDisplay, nullptr, nullptr) != EGL_TRUE) {
        LOGE("无法初始化 EGL");
        return false;
    }
    const EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
            EGL_RED_SIZE,        8,
            EGL_GREEN_SIZE,      8,
            EGL_BLUE_SIZE,       8,
            EGL_ALPHA_SIZE,      8,
            EGL_DEPTH_SIZE,      8,
            EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    if (eglChooseConfig(eglDisplay, configAttribs, &config, 1, &numConfigs) != EGL_TRUE) {
        LOGE("无法选择 EGLConfig");
        return false;
    }
    eglSurface = eglCreateWindowSurface(eglDisplay, config, window, nullptr);
    if (eglSurface == EGL_NO_SURFACE) {
        LOGE("无法创建 EGLSurface");
        return false;
    }
    const EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
    };
    eglContext = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT, contextAttribs);
    if (eglContext == EGL_NO_CONTEXT) {
        LOGE("无法创建 EGLContext");
        return false;
    }
    if (eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext) != EGL_TRUE) {
        LOGE("无法设置当前 EGLContext");
        return false;
    }
    return true;
}

static bool initGL(int width, int height) {
    // 顶点着色器
    const char* vShaderStr =
            "attribute vec4 aPosition;    \n"
            "attribute vec2 aTexCoord;    \n"
            "varying vec2 vTexCoord;      \n"
            "void main()                \n"
            "{                          \n"
            "   gl_Position = aPosition;\n"
            "   vTexCoord = aTexCoord;  \n"
            "}                          \n";
    // 片段着色器
    const char* fShaderStr =
            "precision mediump float;                           \n"
            "varying vec2 vTexCoord;                              \n"
            "uniform sampler2D sTexture;                          \n"
            "void main()                                        \n"
            "{                                                  \n"
            "  gl_FragColor = texture2D(sTexture, vTexCoord);    \n"
            "}                                                  \n";

    // 编译顶点着色器与片段着色器源码
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vShaderStr);
    GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, fShaderStr);
    if (!vertexShader || !fragmentShader) {
        return false;
    }

    programObject = glCreateProgram(); // 创建程序对象
    if (programObject == 0) {
        return false;
    }
    glAttachShader(programObject, vertexShader); // 添加顶点着色器与片段着色器
    glAttachShader(programObject, fragmentShader);
    glLinkProgram(programObject); // 链接程序对象

    GLint linked;
    glGetProgramiv(programObject, GL_LINK_STATUS, &linked);
    if (!linked) {
        char info[512] = {0};
        glGetProgramInfoLog(programObject, sizeof(info), nullptr, info);
        LOGE("Could not link program: %s", info);
        glDeleteProgram(programObject);
        return false;
    }

    // 获取attribute与uniform 位置
    attrPosition = glGetAttribLocation(programObject, "aPosition");
    attrTexCoord = glGetAttribLocation(programObject, "aTexCoord");
    uniTexture = glGetUniformLocation(programObject, "sTexture");

    // 创建纹理对象
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // 预分配纹理内存（GL_RGBA 格式）
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 创建VBO存放顶点数据
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glViewport(0, 0, width, height);
    return true;
}

// 初始化 OpenGL
bool initOpenGL(ANativeWindow* window, int width, int height) {
    if (!initEGL(window)) {
        return false;
    }
    if (!initGL(width, height)) {
        return false;
    }
    return true;
}

// 渲染一帧视频帧
void renderFrame(uint8_t* rgbaData, int width, int height) {
    glBindTexture(GL_TEXTURE_2D, textureId);     // 更新纹理数据
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    width, height,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgbaData);

    glClear(GL_COLOR_BUFFER_BIT);     // 清除画面，开始绘制
    glUseProgram(programObject);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(attrPosition);
    glVertexAttribPointer(attrPosition, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (const void*)0);
    glEnableVertexAttribArray(attrTexCoord);
    glVertexAttribPointer(attrTexCoord, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (const void*)(3 * sizeof(GLfloat)));

    // 绑定纹理采样器到纹理单元
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glUniform1i(uniTexture, 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);     // 绘制四边形

    eglSwapBuffers(eglDisplay, eglSurface);     // 刷新屏幕
}

// 释放 OpenGL 相关资源
void cleanupOpenGL() {
    if (vbo) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (textureId) {
        glDeleteTextures(1, &textureId);
        textureId = 0;
    }
    if (programObject) {
        glDeleteProgram(programObject);
        programObject = 0;
    }
    if (eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglContext != EGL_NO_CONTEXT) {
            eglDestroyContext(eglDisplay, eglContext);
            eglContext = EGL_NO_CONTEXT;
        }
        if (eglSurface != EGL_NO_SURFACE) {
            eglDestroySurface(eglDisplay, eglSurface);
            eglSurface = EGL_NO_SURFACE;
        }
        eglTerminate(eglDisplay);
        eglDisplay = EGL_NO_DISPLAY;
    }
}
