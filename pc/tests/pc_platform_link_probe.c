#include <SDL.h>

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

static void* probe_thread(void* argument) {
    int* ran = (int*)argument;
    *ran = 1;
    return NULL;
}

int main(void) {
    SDL_version sdl_version;
    pthread_t thread;
    int thread_ran = 0;
    void* dl_handle;
    void* dynamic_symbol;
    const GLubyte* (*gl_string)(GLenum) = glGetString;

    SDL_GetVersion(&sdl_version);
    if (pthread_create(&thread, NULL, probe_thread, &thread_ran) != 0 ||
        pthread_join(thread, NULL) != 0 || !thread_ran) {
        fprintf(stderr, "platform link probe: pthread boundary failed\n");
        return 1;
    }

    dl_handle = dlopen(NULL, RTLD_LAZY);
    if (dl_handle == NULL) {
        fprintf(stderr, "platform link probe: dlopen boundary failed\n");
        return 1;
    }
    dynamic_symbol = dlsym(dl_handle, "malloc");
    if (dynamic_symbol == NULL || dlclose(dl_handle) != 0) {
        fprintf(stderr, "platform link probe: dlsym boundary failed\n");
        return 1;
    }

    if (gl_string == NULL) {
        fprintf(stderr, "platform link probe: OpenGL symbol boundary failed\n");
        return 1;
    }

    printf(
        "platform link probe: PASS SDL %u.%u.%u, OpenGL symbol, pthread, dlsym\n",
        (unsigned)sdl_version.major,
        (unsigned)sdl_version.minor,
        (unsigned)sdl_version.patch
    );
    return 0;
}
