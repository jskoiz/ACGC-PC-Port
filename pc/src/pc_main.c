/* pc_main.c - PC entry point: SDL2/GL init and boot sequence */
#include "pc_platform.h"
#include "pc_gx_internal.h"
#include "pc_texture_pack.h"
#include "pc_settings.h"
#include "pc_keybindings.h"
#include "pc_assets.h"
#include "pc_disc.h"
#include "pc_typing.h"
#include "pc_pause_menu.h"
#include "pc_settings_menu.h"
#include "pc_profiler.h"
#include "m_kankyo.h"

#ifdef __APPLE__
#include <stdlib.h>

#include "acgc/graph_submission.h"
#include "acgc/pc_metal_runtime.h"
#endif

/* prefer discrete GPU on laptops */
#ifdef _WIN32
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

SDL_Window*   g_pc_window = NULL;
SDL_GLContext  g_pc_gl_context = NULL;
int           g_pc_running = 1;
int           g_pc_frame_limit_override = -1;
int           g_pc_speedhack_enabled = 0;
int           g_pc_verbose = 0;
int           g_pc_time_override = -1; /* -1=system clock, 0-23=override hour */
int           g_pc_min_override = -1; /* -1=system clock, 0-59=override minute */
int           g_pc_sec_override = -1; /* -1=system clock, 0-59=override second */
int           g_pc_date_month = -1; /* -1=system clock, 1-12=override month */
int           g_pc_date_day = -1; /* -1=system clock, 1-31=override day */
int           g_pc_date_year = -1; /* -1=system clock, else override year */
int           g_pc_weather_override = -1;
int           g_pc_weather_intensity_override = mEnv_WEATHER_INTENSITY_HEAVY;
int           g_pc_window_w = PC_SCREEN_WIDTH;
int           g_pc_window_h = PC_SCREEN_HEIGHT;
int           g_pc_widescreen_stretch = 0;

/* exe image range -- used by seg2k0 to distinguish pointers from segment addresses */
unsigned int pc_image_base = 0;
unsigned int pc_image_end  = 0;

void pc_platform_init(void) {
#ifdef _WIN32
    SetProcessDPIAware();
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
#ifdef PC_ENHANCEMENTS
    if (g_pc_settings.msaa > 0) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, g_pc_settings.msaa);
    }
#endif

    {
        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
        int win_w = g_pc_settings.window_width;
        int win_h = g_pc_settings.window_height;
        if (g_pc_settings.fullscreen == 1) {
            flags |= SDL_WINDOW_FULLSCREEN;
        } else if (g_pc_settings.fullscreen == 2) {
            flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
        g_pc_window = SDL_CreateWindow(
            PC_WINDOW_TITLE,
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            win_w, win_h, flags
        );
    }
    if (!g_pc_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        exit(1);
    }

    g_pc_gl_context = SDL_GL_CreateContext(g_pc_window);
    if (!g_pc_gl_context) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_pc_window);
        SDL_Quit();
        exit(1);
    }

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGL failed\n");
        SDL_GL_DeleteContext(g_pc_gl_context);
        SDL_DestroyWindow(g_pc_window);
        SDL_Quit();
        exit(1);
    }

    SDL_GL_SetSwapInterval(g_pc_settings.vsync);

    pc_platform_update_window_size();

#ifdef PC_ENHANCEMENTS
    if (g_pc_settings.msaa > 0) {
        glEnable(GL_MULTISAMPLE);
    }
#endif

    pc_gx_init();
#ifdef __APPLE__
    pc_metal_runtime_init();
#endif
    pc_texture_pack_init();
#ifdef PC_ENHANCEMENTS
    if (g_pc_settings.preload_textures) {
        pc_texture_pack_preload_all();
    }
#endif
}

extern void PADCleanup(void);

static void pc_speedhack_toggle(void) {
    g_pc_speedhack_enabled = !g_pc_speedhack_enabled;
    if (g_pc_window != NULL) {
        SDL_SetWindowTitle(g_pc_window, g_pc_speedhack_enabled ? "Animal Crossing [5x]" : PC_WINDOW_TITLE);
    }

    if (g_pc_verbose) {
        printf("[PC] speedhack %s\n", g_pc_speedhack_enabled ? "5x" : "off");
    }
}

void pc_platform_shutdown(void) {
#ifdef __APPLE__
    pc_metal_runtime_shutdown();
#endif
    pc_audio_shutdown();
    pc_audio_mq_shutdown();
    PADCleanup();
    pc_texture_pack_shutdown();
    pc_gx_shutdown();

    if (g_pc_gl_context) {
        SDL_GL_DeleteContext(g_pc_gl_context);
        g_pc_gl_context = NULL;
    }
    if (g_pc_window) {
        SDL_DestroyWindow(g_pc_window);
        g_pc_window = NULL;
    }
    SDL_Quit();
}

void pc_platform_update_window_size(void) {
    SDL_GL_GetDrawableSize(g_pc_window, &g_pc_window_w, &g_pc_window_h);
    if (g_pc_window_w <= 0) g_pc_window_w = PC_SCREEN_WIDTH;
    if (g_pc_window_h <= 0) g_pc_window_h = PC_SCREEN_HEIGHT;
}

void pc_platform_swap_buffers(void) {
    pc_gx_draw_pending();
    SDL_GL_SwapWindow(g_pc_window);
}

int pc_platform_poll_events(void) {
    SDL_Event event;

    pc_typing_update();

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                g_pc_running = 0;
                return 0;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    pc_platform_update_window_size();
                }
                break;
            case SDL_KEYDOWN:
                /* Keybinding capture eats all input first (works from both
                 * the pause menu and the title Options menu). */
                if (pc_settings_menu_capture_active()) {
                    pc_settings_menu_handle_capture_event(&event);
                    break;
                }
                if (event.key.keysym.sym == SDLK_F3 && !event.key.repeat) {
                    pc_speedhack_toggle();
                    break;
                }
                if (event.key.keysym.sym == SDLK_ESCAPE && !event.key.repeat) {
                    if (g_pc_paused) {
                        pc_pause_menu_handle_event(&event);
                    } else {
                        pc_pause_menu_toggle();
                    }
                    break;
                }
                if (g_pc_paused) {
                    pc_pause_menu_handle_event(&event);
                    break; /* swallow all keys while paused */
                }
                pc_typing_handle_event(&event);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (pc_settings_menu_capture_active()) {
                    pc_settings_menu_handle_capture_event(&event);
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                if (pc_settings_menu_capture_active()) {
                    pc_settings_menu_handle_capture_event(&event);
                    break;
                }
                if (g_pc_paused) {
                    pc_pause_menu_handle_event(&event);
                    break;
                }
                /* Back/Select opens the pause menu (controller Esc). */
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    pc_pause_menu_toggle();
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (pc_settings_menu_capture_active()) {
                    pc_settings_menu_handle_capture_event(&event);
                    break;
                }
                if (g_pc_paused) {
                    pc_pause_menu_handle_event(&event);
                }
                break;
            case SDL_TEXTINPUT:
                if (g_pc_paused) break;
                pc_typing_handle_event(&event);
                break;
        }
    }
    return 1;
}

/* game's main() renamed to ac_entry via -Dmain=ac_entry, boot.c's to boot_main */
extern void ac_entry(void);
extern int boot_main(int argc, const char** argv);

#ifdef __APPLE__
static void pc_graph_submission_capture(
    void* context,
    const GraphTaskSubmissionCapture* capture
) {
    uint32_t i;

    (void)context;
    fprintf(stderr,
            "[GRAPH_CAPTURE] version=%u frame=%u source_capacity=%u captured=%u words=",
            (unsigned)capture->version,
            (unsigned)capture->graph_frame,
            (unsigned)capture->source_word_capacity,
            (unsigned)capture->captured_word_count);
    for (i = 0; i < capture->captured_word_count; ++i) {
        fprintf(stderr, "%s%08x", i == 0 ? "" : ",", (unsigned)capture->words[i]);
    }
    fputc('\n', stderr);
    graph_clear_task_submission_capture_callback();
}

static void pc_graph_submission_target_capture(
    void* context,
    const GraphTaskSubmissionTargetCapture* capture
) {
    uint32_t i;

    (void)context;
    fprintf(stderr,
            "[GRAPH_TARGET_CAPTURE] version=%u frame=%u target=%08x target_capacity=%u captured=%u classification=%u terminator=%u words=",
            (unsigned)capture->version,
            (unsigned)capture->graph_frame,
            (unsigned)capture->target_identity,
            (unsigned)capture->target_word_capacity,
            (unsigned)capture->captured_word_count,
            (unsigned)capture->classification,
            (unsigned)capture->terminator_word_index);
    for (i = 0; i < capture->captured_word_count; ++i) {
        fprintf(stderr, "%s%08x", i == 0 ? "" : ",", (unsigned)capture->words[i]);
    }
    fputc('\n', stderr);
    graph_clear_task_submission_target_capture_callback();
}

static void pc_enable_graph_submission_capture(void) {
    if (getenv("ACGC_GRAPH_CAPTURE") != NULL) {
        graph_set_task_submission_capture_callback(pc_graph_submission_capture, NULL);
        graph_set_task_submission_target_capture_callback(
            pc_graph_submission_target_capture,
            NULL
        );
        fprintf(stderr, "[GRAPH_CAPTURE] callback=enabled\n");
    }
}
#endif

#ifdef __APPLE__
/* Return vmaddr + slide without relying on signed overflow for a negative slide. */
static int pc_macho_slide_address(uintptr_t vmaddr, intptr_t slide, uintptr_t* address) {
    if (slide < 0) {
        uintptr_t magnitude = (uintptr_t)(-(slide + 1)) + 1u;
        if (vmaddr < magnitude) {
            return 0;
        }
        *address = vmaddr - magnitude;
    } else {
        uintptr_t offset = (uintptr_t)slide;
        if (vmaddr > UINTPTR_MAX - offset) {
            return 0;
        }
        *address = vmaddr + offset;
    }
    return 1;
}

static int pc_macho_image_range(uintptr_t* image_base, uintptr_t* image_end) {
    const struct mach_header* header = _dyld_get_image_header(0);
    if (header == NULL) {
        return 0;
    }

    const intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    uintptr_t base = UINTPTR_MAX;
    uintptr_t end = 0;

#if defined(__LP64__)
    if (header->magic != MH_MAGIC_64) {
        return 0;
    }

    const struct mach_header_64* header64 = (const struct mach_header_64*)header;
    const unsigned char* command_bytes = (const unsigned char*)(header64 + 1);
    size_t command_bytes_left = header64->sizeofcmds;

    for (uint32_t i = 0; i < header64->ncmds; i++) {
        if (command_bytes_left < sizeof(struct load_command)) {
            return 0;
        }

        const struct load_command* command = (const struct load_command*)command_bytes;
        if (command->cmdsize < sizeof(struct load_command) ||
            command->cmdsize > command_bytes_left) {
            return 0;
        }

        if (command->cmd == LC_SEGMENT_64) {
            if (command->cmdsize < sizeof(struct segment_command_64)) {
                return 0;
            }

            const struct segment_command_64* segment =
                (const struct segment_command_64*)command;
            if (strncmp(segment->segname, "__PAGEZERO", sizeof(segment->segname)) == 0) {
                command_bytes += command->cmdsize;
                command_bytes_left -= command->cmdsize;
                continue;
            }

            uintptr_t segment_base;
            if (!pc_macho_slide_address((uintptr_t)segment->vmaddr, slide, &segment_base) ||
                segment->vmsize > UINTPTR_MAX - segment_base) {
                return 0;
            }

            const uintptr_t segment_end = segment_base + (uintptr_t)segment->vmsize;
            if (segment_base < base) {
                base = segment_base;
            }
            if (segment_end > end) {
                end = segment_end;
            }
        }

        command_bytes += command->cmdsize;
        command_bytes_left -= command->cmdsize;
    }
#else
    if (header->magic != MH_MAGIC) {
        return 0;
    }

    const unsigned char* command_bytes = (const unsigned char*)(header + 1);
    size_t command_bytes_left = header->sizeofcmds;

    for (uint32_t i = 0; i < header->ncmds; i++) {
        if (command_bytes_left < sizeof(struct load_command)) {
            return 0;
        }

        const struct load_command* command = (const struct load_command*)command_bytes;
        if (command->cmdsize < sizeof(struct load_command) ||
            command->cmdsize > command_bytes_left) {
            return 0;
        }

        if (command->cmd == LC_SEGMENT) {
            if (command->cmdsize < sizeof(struct segment_command)) {
                return 0;
            }

            const struct segment_command* segment = (const struct segment_command*)command;
            if (strncmp(segment->segname, "__PAGEZERO", sizeof(segment->segname)) == 0) {
                command_bytes += command->cmdsize;
                command_bytes_left -= command->cmdsize;
                continue;
            }

            uintptr_t segment_base;
            if (!pc_macho_slide_address((uintptr_t)segment->vmaddr, slide, &segment_base) ||
                segment->vmsize > UINTPTR_MAX - segment_base) {
                return 0;
            }

            const uintptr_t segment_end = segment_base + (uintptr_t)segment->vmsize;
            if (segment_base < base) {
                base = segment_base;
            }
            if (segment_end > end) {
                end = segment_end;
            }
        }

        command_bytes += command->cmdsize;
        command_bytes_left -= command->cmdsize;
    }
#endif

    if (base == UINTPTR_MAX || end <= base) {
        return 0;
    }

    *image_base = base;
    *image_end = end;
    return 1;
}
#endif

static int pc_parse_rain_intensity(const char* text) {
    if (strcmp(text, "light") == 0) {
        return mEnv_WEATHER_INTENSITY_LIGHT;
    }

    if (strcmp(text, "normal") == 0) {
        return mEnv_WEATHER_INTENSITY_NORMAL;
    }

    if (strcmp(text, "heavy") == 0) {
        return mEnv_WEATHER_INTENSITY_HEAVY;
    }

    return -1;
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: AnimalCrossing [options]\n");
            printf("  --verbose, -v       Enable diagnostic output\n");
            printf("  --no-framelimit     Alias for --framelimit 0 (uncapped)\n");
            printf("  --framelimit N      Set the target frame rate (default 60, 0 = uncapped)\n");
            printf("  --profile [N]       Print frame profiler summary every N frames (default 120)\n");
            printf("  --model-viewer [N]  Launch model viewer (optional start index)\n");
            printf("  --time H[:M[:S]]    Override in-game time (e.g. 5, 17:30, 5:55:00)\n");
            printf("  --date M/D[/Y]      Override in-game date (e.g. 7/4, 12/24/2026)\n");
            printf("  --rain [intensity]  Force rainy weather; intensity is light, normal, or heavy\n");
            printf("  --uber-shader       Disable shader specialization (single uber shader)\n");
            printf("  --help, -h          Show this help message\n");
            return 0;
        } else if (strcmp(argv[i], "--framelimit") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                int v = atoi(argv[i + 1]);
                if (v > 0) {
                    g_pc_frame_limit_override = v;
                } else if (v == 0) {
                    g_pc_frame_limit_override = 0;
                }
                i++;
            }
        } else if (strcmp(argv[i], "--no-framelimit") == 0) {
            g_pc_frame_limit_override = 0;
        } else if (strcmp(argv[i], "--uber-shader") == 0) {
            extern int g_pc_uber_shader_only;
            g_pc_uber_shader_only = 1;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            g_pc_verbose = 1;
        } else if (strcmp(argv[i], "--profile") == 0) {
            g_pc_profile_enabled = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                int interval = atoi(argv[i + 1]);
                if (interval > 0) g_pc_profile_interval = interval;
                i++;
            }
        } else if (strcmp(argv[i], "--model-viewer") == 0) {
            g_pc_model_viewer = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                g_pc_model_viewer_start = atoi(argv[i + 1]);
                i++;
            }
        } else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            int h = -1, m = -1, s = -1;
            sscanf(argv[i + 1], "%d:%d:%d", &h, &m, &s);
            if (h >= 0 && h <= 23) g_pc_time_override = h;
            if (m >= 0 && m <= 59) g_pc_min_override = m;
            if (s >= 0 && s <= 59) g_pc_sec_override = s;
            i++;
        } else if (strcmp(argv[i], "--date") == 0 && i + 1 < argc) {
            int mo = -1, d = -1, y = -1;
            sscanf(argv[i + 1], "%d/%d/%d", &mo, &d, &y);
            if (mo >= 1 && mo <= 12 && d >= 1 && d <= 31) {
                g_pc_date_month = mo;
                g_pc_date_day = d;
                if (y >= 2000) g_pc_date_year = y;
            }
            i++;
        } else if (strcmp(argv[i], "--rain") == 0) {
            g_pc_weather_override = mEnv_WEATHER_RAIN;
            g_pc_weather_intensity_override = mEnv_WEATHER_INTENSITY_HEAVY;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                int intensity = pc_parse_rain_intensity(argv[i + 1]);
                if (intensity >= 0) {
                    g_pc_weather_intensity_override = intensity;
                    i++;
                }
            }
        }
    }

    /* Redirect stdout/stderr to NUL unless verbose — unbuffered terminal writes
     * are extremely slow on Windows and tank FPS. */
    if (!g_pc_verbose && !g_pc_profile_enabled) {
#ifdef _WIN32
        freopen("NUL", "w", stdout);
        freopen("NUL", "w", stderr);
#else
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
#endif
    } else {
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }

    /* exe image range for seg2k0 — BSS can overlap N64 segment addresses */
#ifdef _WIN32
    {
        HMODULE exe = GetModuleHandle(NULL);
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)exe;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((char*)exe + dos->e_lfanew);
        pc_image_base = (unsigned int)(uintptr_t)exe;
        pc_image_end = pc_image_base + nt->OptionalHeader.SizeOfImage;
    }
#else
#ifdef __APPLE__
    {
        uintptr_t image_base;
        uintptr_t image_end;
        if (pc_macho_image_range(&image_base, &image_end) &&
            image_base <= UINT32_MAX && image_end <= UINT32_MAX) {
            /* The current seg2k0 ABI exposes 32-bit bounds. Do not truncate
             * real arm64 addresses until that ABI is migrated deliberately. */
            pc_image_base = (unsigned int)image_base;
            pc_image_end = (unsigned int)image_end;
        }
    }
#elif defined(__linux__)
    {
        Dl_info dl;
        if (dladdr((void*)main, &dl) && dl.dli_fbase) {
            pc_image_base = (unsigned int)(uintptr_t)dl.dli_fbase;
            Elf32_Ehdr* ehdr = (Elf32_Ehdr*)dl.dli_fbase;
            Elf32_Phdr* phdr = (Elf32_Phdr*)((char*)dl.dli_fbase + ehdr->e_phoff);
            unsigned int max_end = 0;
            for (int i = 0; i < ehdr->e_phnum; i++) {
                if (phdr[i].p_type == PT_LOAD) {
                    unsigned int seg_end = phdr[i].p_vaddr + phdr[i].p_memsz;
                    if (seg_end > max_end) max_end = seg_end;
                }
            }
            /* ET_EXEC: p_vaddr is absolute. ET_DYN (PIE): relative to load address. */
            if (ehdr->e_type == ET_DYN) {
                pc_image_end = pc_image_base + max_end;
            } else {
                pc_image_end = max_end;
            }
        }
    }
#else
#error "Unsupported non-Windows/non-Darwin platform"
#endif
#endif

    SDL_SetMainReady();
    pc_settings_load();
    pc_keybindings_load();
    pc_platform_init();
    pc_disc_init();
    if (!pc_assets_init()) {
        const char* msg =
            "No game data found.\n\n"
            "Animal Crossing needs the original GameCube ROM to run.\n"
            "Place a disc image (.iso, .gcm, or .ciso) to the \"rom\" subfolder.";
        fprintf(stderr, "[PC] %s\n", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "Animal Crossing - Missing ROM", msg, g_pc_window);
        pc_platform_shutdown();
        return 1;
    }

#ifdef __APPLE__
    pc_enable_graph_submission_capture();
#endif
    ac_entry();                         /* sets HotStartEntry = &entry */
    boot_main(argc, (const char**)argv); /* full init → HotStartEntry → game loop */

    pc_disc_shutdown();
    pc_platform_shutdown();
    return 0;
}
