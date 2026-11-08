#pragma once
#include <SDL.h>
#ifdef DOK_GLES2
#include <SDL_opengles2.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <SDL_opengl.h>
#endif

namespace dok {
inline void configure_gl_context(int depth=24) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#ifdef DOK_GLES2
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    // GLES2 guarantees 16-bit depth; do not require a desktop depth format.
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,depth?16:0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,depth);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
}
}
