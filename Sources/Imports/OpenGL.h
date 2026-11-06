//
//  OpenGL.h
//  OpenSpades
//
//  Created by yvt on 7/11/13.
//  Copyright (c) 2013 yvt.jp. All rights reserved.
//

#pragma once

// Supported platforms: macOS, Android (GLES3), and Desktop (GLEW)
#if __APPLE__
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#elif defined(__ANDROID__) || defined(ANDROID)
#define GL_GLEXT_PROTOTYPES 1
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

#ifndef GL_ANY_SAMPLES_PASSED
#  define GL_ANY_SAMPLES_PASSED 0x8C2F
#endif

#ifndef GL_CLAMP
#  define GL_CLAMP GL_CLAMP_TO_EDGE
#endif
#ifndef GL_POLYGON
#  define GL_POLYGON GL_TRIANGLE_FAN
#endif
#ifndef GL_BGRA
#  ifdef GL_BGRA_EXT
#    define GL_BGRA GL_BGRA_EXT
#  else
#    define GL_BGRA 0x80E1
#  endif
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#  define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#  define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_GEOMETRY_SHADER
#  ifdef GL_GEOMETRY_SHADER_EXT
#    define GL_GEOMETRY_SHADER GL_GEOMETRY_SHADER_EXT
#  else
#    define GL_GEOMETRY_SHADER 0x8DD9
#  endif
#endif
#define glClearDepth glClearDepthf
#define glDepthRange glDepthRangef
#else
#include <GL/glew.h>

// v3.3 / GL_ARB_occlusion_query2
#ifndef GL_ANY_SAMPLES_PASSED
#  define GL_ANY_SAMPLES_PASSED 0x8C2F
#endif

#endif
