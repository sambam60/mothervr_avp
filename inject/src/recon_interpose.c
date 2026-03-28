#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <float.h>
#include <mach/mach.h>
#include <math.h>
#include <sys/mman.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const void *replacement;
    const void *replacee;
} interpose_t;

extern void dyld_dynamic_interpose(
    const struct mach_header *mh,
    const interpose_t array[],
    size_t count
) __attribute__((weak_import));

#define INTERPOSE(replacement, replacee)                                                     \
    __attribute__((used)) static const interpose_t interpose_##replacee                      \
        __attribute__((section("__DATA,__interpose"))) = {                                   \
            (const void *)(uintptr_t)&replacement, (const void *)(uintptr_t)&replacee        \
        }

#define MAX_TRACKED_BUFFERS 4096
#define MAX_BACKEND_WATCHED_BUFFERS 32
#define BACKEND_BUFFER_SAMPLE_BYTES 64
#define MAX_BACKEND_BUFFER_BYTES 0xD14
#define BACKEND_WINDOW_LOG_LIMIT 12
#define BACKEND_FLOAT_LOG_LIMIT 32
#define MAX_UNIFORM_NAME_RECORDS 256
#define MAX_UNIFORM_WATCHES 256
#define MAX_UNIFORM_NAME_LENGTH 64
#define MAX_DISPATCH_PATCH_RECORDS 8
#define DISPATCH_TABLE_SCAN_SLOTS 1024
#define BACKEND_LOG_BUDGET_UNLIMITED UINT32_MAX
#define MAX_RENDER_UNIFORM_BLOCK_WATCHES 64
#define RENDER_UNIFORM_BLOCK_SAMPLE_BYTES 256
#define APPLE_METAL_OPENGL_RENDERER_UPDATE_UNIFORM_BINDINGS_X86_64_OFFSET 0x3ae9aULL
#define APPLE_METAL_OPENGL_RENDERER_SET_RENDER_UNIFORM_BUFFERS_X86_64_OFFSET 0x5619cULL
#define APPLE_METAL_OPENGL_RENDERER_SET_RENDER_PROGRAM_UNIFORMS_X86_64_OFFSET 0x5658cULL

typedef struct RenderUniformBlockWatch RenderUniformBlockWatch;

typedef void (*glUseProgram_fn)(GLuint program);
typedef GLint (*glGetUniformLocation_fn)(GLuint program, const GLchar *name);
typedef void (*glBindBuffer_fn)(GLenum target, GLuint buffer);
typedef void (*glBindBufferBase_fn)(GLenum target, GLuint index, GLuint buffer);
typedef void (*glBindBufferRange_fn)(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
typedef void (*glBufferSubData_fn)(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
typedef void *(*glMapBufferRange_fn)(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
typedef void (*glUniformMatrix4fv_fn)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (*glProgramUniformMatrix4fv_fn)(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (*glUniform4fv_fn)(GLint location, GLsizei count, const GLfloat *value);
typedef void (*glProgramUniform4fv_fn)(GLuint program, GLint location, GLsizei count, const GLfloat *value);
typedef CGLError (*CGLFlushDrawable_fn)(CGLContextObj context);
typedef void *(*dlsym_fn)(void *handle, const char *symbol);
typedef uintptr_t (*backend_probe6_fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

void hooked_glUseProgram(GLuint program);
GLint hooked_glGetUniformLocation(GLuint program, const GLchar *name);
void hooked_glBindBuffer(GLenum target, GLuint buffer);
void hooked_glBindBufferBase(GLenum target, GLuint index, GLuint buffer);
void hooked_glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
void hooked_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
void *hooked_glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
void hooked_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void hooked_glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void hooked_glUniform4fv(GLint location, GLsizei count, const GLfloat *value);
void hooked_glProgramUniform4fv(GLuint program, GLint location, GLsizei count, const GLfloat *value);
CGLError hooked_CGLFlushDrawable(CGLContextObj context);
void *hooked_dlsym(void *handle, const char *symbol);
uintptr_t hooked_gldUpdateDispatch(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);
uintptr_t hooked_gldPresentFramebufferData(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);
uintptr_t hooked_gldBufferSubData(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);
uintptr_t hooked_gldFlushContext(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);
uintptr_t hooked_gliAttachDrawable(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);
uintptr_t hooked_ctx_updateUniformBindings(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);
uintptr_t hooked_ctx_setRenderUniformBuffers(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);
uintptr_t hooked_ctx_setRenderProgramUniforms(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);
static void *resolve_visible_symbol(const char *symbol);
static void resolve_symbols(void);
static uint64_t hash_backend_buffer_bytes(const unsigned char *bytes, size_t count);
static RenderUniformBlockWatch *find_render_uniform_block_watch(uintptr_t identity_a, uintptr_t identity_b, size_t byte_count);
static void log_backend_buffer_float_deltas_with_label(
    const char *label,
    const unsigned char *before,
    const unsigned char *after,
    size_t byte_count
);

typedef struct {
    bool occupied;
    GLuint buffer;
    GLenum target;
    GLuint index;
} IndexedBindingRecord;

typedef struct {
    bool occupied;
    size_t size;
    uintptr_t buffer_handle;
    uint64_t last_hash;
    uint32_t observed_changes;
    uint32_t logged_changes;
    uint32_t remaining_logs;
    unsigned char last_bytes[BACKEND_BUFFER_SAMPLE_BYTES];
    unsigned char last_full_bytes[MAX_BACKEND_BUFFER_BYTES];
} BackendWatchedBuffer;

typedef struct {
    bool occupied;
    GLuint program;
    GLint location;
    char name[MAX_UNIFORM_NAME_LENGTH];
} UniformNameRecord;

typedef struct {
    bool occupied;
    GLuint program;
    GLint location;
    uint32_t float_count;
    uint64_t last_hash;
    uint32_t observed_changes;
    uint32_t logged_changes;
    float last_values[16];
} UniformWatchRecord;

typedef struct RenderUniformBlockWatch {
    bool occupied;
    uintptr_t identity_a;
    uintptr_t identity_b;
    size_t byte_count;
    uint64_t last_hash;
    uint32_t observed_changes;
    uint32_t logged_changes;
    unsigned char last_bytes[RENDER_UNIFORM_BLOCK_SAMPLE_BYTES];
} RenderUniformBlockWatch;

typedef struct {
    const char *symbol;
    const void *original;
    const void *replacement;
} DispatchPatchTarget;

typedef struct {
    bool occupied;
    uintptr_t table_ptr;
    uint64_t last_attempt_hit;
    bool logged_no_matches;
    bool logged_slot_dump;
    uint32_t total_slots_patched;
} DispatchPatchRecord;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_log_file = NULL;
static bool g_initialized = false;
static bool g_runtime_interpose_attempted = false;
static uint64_t g_frame_counter = 0;
static bool g_have_last_look78_eye = false;
static float g_last_look78_eye[3] = {0.0f, 0.0f, 0.0f};
static bool g_have_last_d14_pos = false;
static float g_last_d14_pos[3] = {0.0f, 0.0f, 0.0f};
static bool g_have_camera_proxy_last_yaw = false;
static float g_camera_proxy_last_wrapped_yaw_deg = 0.0f;
static float g_camera_proxy_unwrapped_yaw_deg = 0.0f;
static bool g_have_camera_proxy_baseline = false;
static float g_camera_proxy_baseline_yaw_deg = 0.0f;
static float g_camera_proxy_baseline_pitch_deg = 0.0f;
static GLuint g_current_program = 0;
static GLuint g_current_uniform_buffer = 0;
static GLuint g_current_array_buffer = 0;
static IndexedBindingRecord g_indexed_bindings[MAX_TRACKED_BUFFERS];
static BackendWatchedBuffer g_backend_watched_buffers[MAX_BACKEND_WATCHED_BUFFERS] = {0};
static UniformNameRecord g_uniform_name_records[MAX_UNIFORM_NAME_RECORDS] = {0};
static UniformWatchRecord g_uniform_watches[MAX_UNIFORM_WATCHES] = {0};
static RenderUniformBlockWatch g_render_uniform_block_watches[MAX_RENDER_UNIFORM_BLOCK_WATCHES] = {0};
static DispatchPatchRecord g_dispatch_patch_records[MAX_DISPATCH_PATCH_RECORDS] = {0};

static glUseProgram_fn real_glUseProgram = NULL;
static glGetUniformLocation_fn real_glGetUniformLocation = NULL;
static glBindBuffer_fn real_glBindBuffer = NULL;
static glBindBufferBase_fn real_glBindBufferBase = NULL;
static glBindBufferRange_fn real_glBindBufferRange = NULL;
static glBufferSubData_fn real_glBufferSubData = NULL;
static glMapBufferRange_fn real_glMapBufferRange = NULL;
static glUniformMatrix4fv_fn real_glUniformMatrix4fv = NULL;
static glProgramUniformMatrix4fv_fn real_glProgramUniformMatrix4fv = NULL;
static glUniform4fv_fn real_glUniform4fv = NULL;
static glProgramUniform4fv_fn real_glProgramUniform4fv = NULL;
static CGLFlushDrawable_fn real_CGLFlushDrawable = NULL;
static dlsym_fn real_dlsym = NULL;
static backend_probe6_fn real_gldUpdateDispatch = NULL;
static backend_probe6_fn real_gldPresentFramebufferData = NULL;
static backend_probe6_fn real_gldBufferSubData = NULL;
static backend_probe6_fn real_gldFlushContext = NULL;
static backend_probe6_fn real_gliAttachDrawable = NULL;
static backend_probe6_fn real_ctx_updateUniformBindings = NULL;
static backend_probe6_fn real_ctx_setRenderUniformBuffers = NULL;
static backend_probe6_fn real_ctx_setRenderProgramUniforms = NULL;
static uint32_t g_last_interposed_image_count = 0;
static bool g_backend_poller_started = false;
static const char *const g_backend_symbols[] = {
    "gldUpdateDispatch",
    "gldPresentFramebufferData",
    "gldBufferSubData",
    "gldFlushContext",
    "gliCreateContext",
    "gliAttachDrawable",
};
static void *g_last_backend_symbol_addresses[sizeof(g_backend_symbols) / sizeof(g_backend_symbols[0])] = {0};
static void *g_last_backend_probe_targets[5] = {0};
static uint64_t g_gldUpdateDispatch_hits = 0;
static uint64_t g_gldPresentFramebufferData_hits = 0;
static uint64_t g_gldBufferSubData_hits = 0;
static uint64_t g_gldFlushContext_hits = 0;
static uint64_t g_gliAttachDrawable_hits = 0;
static uint64_t g_ctx_updateUniformBindings_hits = 0;
static uint64_t g_ctx_setRenderUniformBuffers_hits = 0;
static uint64_t g_ctx_setRenderProgramUniforms_hits = 0;
static bool g_inline_present_attempted = false;
static bool g_inline_present_installed = false;
static void *g_inline_present_target = NULL;
static void *g_inline_present_trampoline = NULL;
static size_t g_inline_present_patch_size = 0;
static bool g_inline_update_attempted = false;
static bool g_inline_update_installed = false;
static void *g_inline_update_target = NULL;
static void *g_inline_update_trampoline = NULL;
static size_t g_inline_update_patch_size = 0;
static bool g_inline_flush_attempted = false;
static bool g_inline_flush_installed = false;
static void *g_inline_flush_target = NULL;
static void *g_inline_flush_trampoline = NULL;
static size_t g_inline_flush_patch_size = 0;
static bool g_inline_buffer_attempted = false;
static bool g_inline_buffer_installed = false;
static void *g_inline_buffer_target = NULL;
static void *g_inline_buffer_trampoline = NULL;
static size_t g_inline_buffer_patch_size = 0;
static bool g_inline_update_uniform_bindings_attempted = false;
static bool g_inline_update_uniform_bindings_installed = false;
static void *g_inline_update_uniform_bindings_target = NULL;
static void *g_inline_update_uniform_bindings_trampoline = NULL;
static size_t g_inline_update_uniform_bindings_patch_size = 0;
static bool g_inline_render_uniform_buffers_attempted = false;
static bool g_inline_render_uniform_buffers_installed = false;
static void *g_inline_render_uniform_buffers_target = NULL;
static void *g_inline_render_uniform_buffers_trampoline = NULL;
static size_t g_inline_render_uniform_buffers_patch_size = 0;
static bool g_inline_render_program_uniforms_attempted = false;
static bool g_inline_render_program_uniforms_installed = false;
static void *g_inline_render_program_uniforms_target = NULL;
static void *g_inline_render_program_uniforms_trampoline = NULL;
static size_t g_inline_render_program_uniforms_patch_size = 0;

static const char *fallback_log_path(void) {
    static char derived_path[4096];
    static bool attempted = false;

    if (!attempted) {
        attempted = true;

        Dl_info info;
        if (dladdr((const void *)&fallback_log_path, &info) && info.dli_fname && info.dli_fname[0]) {
            size_t source_len = strlen(info.dli_fname);
            if (source_len + 1 < sizeof(derived_path)) {
                memcpy(derived_path, info.dli_fname, source_len + 1);
                char *last_slash = strrchr(derived_path, '/');
                if (last_slash) {
                    const char *filename = "recon.log";
                    size_t prefix_len = (size_t)(last_slash - derived_path) + 1;
                    size_t filename_len = strlen(filename);
                    if (prefix_len + filename_len + 1 < sizeof(derived_path)) {
                        memcpy(last_slash + 1, filename, filename_len + 1);
                    } else {
                        derived_path[0] = '\0';
                    }
                } else {
                    derived_path[0] = '\0';
                }
            }
        }
    }

    if (derived_path[0]) {
        return derived_path;
    }
    return "/tmp/mothervr_avp_recon.log";
}

static const char *active_log_path(void) {
    const char *log_path = getenv("MOTHERVR_AVP_LOG");
    if (log_path && log_path[0]) {
        return log_path;
    }
    return fallback_log_path();
}

static void raw_log_line(const char *message) {
    const char *path = active_log_path();
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        return;
    }

    write(fd, message, strlen(message));
    write(fd, "\n", 1);
    close(fd);
}

static int env_int_or_default(const char *name, int default_value) {
    const char *value = getenv(name);
    if (!value || !value[0]) {
        return default_value;
    }

    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || (end && *end != '\0') || parsed <= 0 || parsed > 86400000L) {
        return default_value;
    }
    return (int)parsed;
}

static void log_inline_bytes(const char *symbol, const uint8_t *bytes, size_t count) {
    fprintf(g_log_file, "[mothervr-avp] inline bytes %s:", symbol);
    for (size_t i = 0; i < count; ++i) {
        fprintf(g_log_file, " %02x", bytes[i]);
    }
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
}

static bool decode_modrm_operand(const uint8_t *code, size_t maxlen, size_t *len_out, bool *rip_relative, uint8_t *modrm_out) {
    if (maxlen < 1) {
        return false;
    }

    uint8_t modrm = code[0];
    uint8_t mod = (uint8_t)(modrm >> 6);
    uint8_t rm = (uint8_t)(modrm & 7);
    size_t len = 1;

    if (modrm_out) {
        *modrm_out = modrm;
    }
    if (rip_relative) {
        *rip_relative = false;
    }

    if (mod != 3 && rm == 4) {
        if (maxlen < len + 1) {
            return false;
        }
        uint8_t sib = code[len++];
        uint8_t base = (uint8_t)(sib & 7);
        if (mod == 0 && base == 5) {
            if (maxlen < len + 4) {
                return false;
            }
            len += 4;
        } else if (mod == 1) {
            if (maxlen < len + 1) {
                return false;
            }
            len += 1;
        } else if (mod == 2) {
            if (maxlen < len + 4) {
                return false;
            }
            len += 4;
        }
    } else if (mod == 0 && rm == 5) {
        if (maxlen < len + 4) {
            return false;
        }
        len += 4;
        if (rip_relative) {
            *rip_relative = true;
        }
    } else if (mod == 1) {
        if (maxlen < len + 1) {
            return false;
        }
        len += 1;
    } else if (mod == 2) {
        if (maxlen < len + 4) {
            return false;
        }
        len += 4;
    }

    *len_out = len;
    return true;
}

static bool decode_instruction_for_trampoline(const uint8_t *code, size_t maxlen, size_t *len_out, bool *unsupported) {
    size_t idx = 0;
    bool rex_w = false;
    *unsupported = false;

    while (idx < maxlen) {
        uint8_t prefix = code[idx];
        if (prefix == 0x66 || prefix == 0x67 || prefix == 0xF2 || prefix == 0xF3 ||
            prefix == 0x2E || prefix == 0x36 || prefix == 0x3E || prefix == 0x26 ||
            prefix == 0x64 || prefix == 0x65 || (prefix >= 0x40 && prefix <= 0x4F)) {
            if ((prefix & 0xF8) == 0x48) {
                rex_w = true;
            }
            idx += 1;
            continue;
        }
        break;
    }

    if (idx >= maxlen) {
        return false;
    }

    uint8_t opcode = code[idx++];
    if (opcode == 0x0F) {
        if (idx >= maxlen) {
            return false;
        }
        uint8_t opcode2 = code[idx++];
        if (opcode2 >= 0x80 && opcode2 <= 0x8F) {
            *unsupported = true;
            if (idx + 4 > maxlen) {
                return false;
            }
            *len_out = idx + 4;
            return true;
        }

        size_t modrm_len = 0;
        bool rip_relative = false;
        if (!decode_modrm_operand(code + idx, maxlen - idx, &modrm_len, &rip_relative, NULL)) {
            return false;
        }
        if (rip_relative) {
            *unsupported = true;
        }
        idx += modrm_len;
        *len_out = idx;
        return true;
    }

    if ((opcode >= 0x70 && opcode <= 0x7F) || opcode == 0xE8 || opcode == 0xE9 || opcode == 0xEB) {
        *unsupported = true;
        size_t extra = (opcode == 0xE8 || opcode == 0xE9) ? 4 : 1;
        if (idx + extra > maxlen) {
            return false;
        }
        *len_out = idx + extra;
        return true;
    }

    if ((opcode >= 0x50 && opcode <= 0x5F) || (opcode >= 0x90 && opcode <= 0x97) ||
        opcode == 0x98 || opcode == 0x99 || opcode == 0x9C || opcode == 0x9D ||
        opcode == 0xC3 || opcode == 0xCB || opcode == 0xC9) {
        *len_out = idx;
        return true;
    }

    if (opcode == 0x6A) {
        if (idx + 1 > maxlen) {
            return false;
        }
        *len_out = idx + 1;
        return true;
    }
    if (opcode == 0x68) {
        if (idx + 4 > maxlen) {
            return false;
        }
        *len_out = idx + 4;
        return true;
    }
    if (opcode == 0xC2 || opcode == 0xCA) {
        if (idx + 2 > maxlen) {
            return false;
        }
        *len_out = idx + 2;
        return true;
    }
    if (opcode >= 0xB8 && opcode <= 0xBF) {
        size_t imm_len = rex_w ? 8 : 4;
        if (idx + imm_len > maxlen) {
            return false;
        }
        *len_out = idx + imm_len;
        return true;
    }

    bool needs_modrm = false;
    size_t imm_len = 0;
    switch (opcode) {
        case 0x01: case 0x03: case 0x09: case 0x0B:
        case 0x11: case 0x13: case 0x19: case 0x1B:
        case 0x21: case 0x23: case 0x29: case 0x2B:
        case 0x31: case 0x33: case 0x39: case 0x3B:
        case 0x63: case 0x84: case 0x85: case 0x86:
        case 0x87: case 0x88: case 0x89: case 0x8A:
        case 0x8B: case 0x8D: case 0x8F: case 0xD1:
        case 0xD3: case 0xFF:
            needs_modrm = true;
            break;
        case 0x69:
            needs_modrm = true;
            imm_len = 4;
            break;
        case 0x6B: case 0x80: case 0x83: case 0xC0: case 0xC1: case 0xC6:
            needs_modrm = true;
            imm_len = 1;
            break;
        case 0x81: case 0xC7:
            needs_modrm = true;
            imm_len = 4;
            break;
        case 0xF6:
        case 0xF7:
            needs_modrm = true;
            break;
        default:
            return false;
    }
    if (!needs_modrm) {
        return false;
    }

    size_t modrm_len = 0;
    bool rip_relative = false;
    uint8_t modrm = 0;
    if (!decode_modrm_operand(code + idx, maxlen - idx, &modrm_len, &rip_relative, &modrm)) {
        return false;
    }
    if (rip_relative) {
        *unsupported = true;
    }
    idx += modrm_len;

    if (opcode == 0xF6) {
        uint8_t ext = (uint8_t)((modrm >> 3) & 7);
        if (ext == 0 || ext == 1) {
            imm_len = 1;
        }
    } else if (opcode == 0xF7) {
        uint8_t ext = (uint8_t)((modrm >> 3) & 7);
        if (ext == 0 || ext == 1) {
            imm_len = 4;
        }
    }

    if (idx + imm_len > maxlen) {
        return false;
    }
    *len_out = idx + imm_len;
    return true;
}

static bool determine_patch_size(const char *symbol, const uint8_t *target, size_t minimum, size_t *patch_size_out) {
    size_t total = 0;
    while (total < minimum) {
        size_t instruction_len = 0;
        bool unsupported = false;
        if (!decode_instruction_for_trampoline(target + total, 32, &instruction_len, &unsupported)) {
            fprintf(g_log_file, "[mothervr-avp] inline hook decode failed at offset=%zu\n", total);
            fflush(g_log_file);
            return false;
        }
        if (unsupported) {
            fprintf(g_log_file, "[mothervr-avp] inline hook unsupported prologue instruction at offset=%zu\n", total);
            log_inline_bytes(symbol, target, 24);
            return false;
        }
        total += instruction_len;
    }
    *patch_size_out = total;
    return true;
}

static void write_absolute_jump(uint8_t *dest, const void *target) {
    dest[0] = 0xFF;
    dest[1] = 0x25;
    dest[2] = 0x00;
    dest[3] = 0x00;
    dest[4] = 0x00;
    dest[5] = 0x00;
    memcpy(dest + 6, &target, sizeof(target));
}

static bool protect_code_region(void *address, size_t length, vm_prot_t protection) {
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return false;
    }

    uintptr_t start_addr = (uintptr_t)address;
    uintptr_t page_start = start_addr & ~((uintptr_t)page_size - 1U);
    uintptr_t page_end = (start_addr + length + (uintptr_t)page_size - 1U) & ~((uintptr_t)page_size - 1U);
    kern_return_t kr = vm_protect(
        mach_task_self(),
        (vm_address_t)page_start,
        (vm_size_t)(page_end - page_start),
        false,
        protection
    );
    return kr == KERN_SUCCESS;
}

static bool install_inline_present_hook(void *resolved_target) {
    if (g_inline_present_installed && g_inline_present_target == resolved_target && g_inline_present_trampoline) {
        real_gldPresentFramebufferData = (backend_probe6_fn)g_inline_present_trampoline;
        return true;
    }
    if (g_inline_present_attempted) {
        return false;
    }

    g_inline_present_attempted = true;
    g_inline_present_target = resolved_target;

    const uint8_t *target_bytes = (const uint8_t *)resolved_target;
    log_inline_bytes("gldPresentFramebufferData", target_bytes, 24);

    size_t patch_size = 0;
    if (!determine_patch_size("gldPresentFramebufferData", target_bytes, 14, &patch_size)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook patch sizing failed for gldPresentFramebufferData\n");
        fflush(g_log_file);
        return false;
    }

    size_t trampoline_size = patch_size + 14;
    uint8_t *trampoline = mmap(NULL, trampoline_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (trampoline == MAP_FAILED) {
        fprintf(g_log_file, "[mothervr-avp] inline hook trampoline allocation failed\n");
        fflush(g_log_file);
        return false;
    }

    memcpy(trampoline, resolved_target, patch_size);
    write_absolute_jump(trampoline + patch_size, (const uint8_t *)resolved_target + patch_size);
    __builtin___clear_cache((char *)trampoline, (char *)(trampoline + trampoline_size));

    if (!protect_code_region(resolved_target, patch_size, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE | VM_PROT_COPY)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect writable failed\n");
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }

    uint8_t patch[64];
    if (patch_size > sizeof(patch)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook patch buffer too small size=%zu\n", patch_size);
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }
    memset(patch, 0x90, patch_size);
    write_absolute_jump(patch, (const void *)(uintptr_t)&hooked_gldPresentFramebufferData);
    memcpy(resolved_target, patch, patch_size);
    __builtin___clear_cache((char *)resolved_target, (char *)resolved_target + patch_size);

    if (!protect_code_region(resolved_target, patch_size, VM_PROT_READ | VM_PROT_EXECUTE)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect restore failed\n");
        fflush(g_log_file);
    }

    g_inline_present_installed = true;
    g_inline_present_trampoline = trampoline;
    g_inline_present_patch_size = patch_size;
    real_gldPresentFramebufferData = (backend_probe6_fn)trampoline;
    fprintf(
        g_log_file,
        "[mothervr-avp] inline hook installed gldPresentFramebufferData target=%p trampoline=%p patch_size=%zu\n",
        resolved_target,
        trampoline,
        patch_size
    );
    fflush(g_log_file);
    return true;
}

static bool install_inline_update_hook(void *resolved_target) {
    if (g_inline_update_installed && g_inline_update_target == resolved_target && g_inline_update_trampoline) {
        real_gldUpdateDispatch = (backend_probe6_fn)g_inline_update_trampoline;
        return true;
    }
    if (g_inline_update_attempted) {
        return false;
    }

    g_inline_update_attempted = true;
    g_inline_update_target = resolved_target;

    const uint8_t *target_bytes = (const uint8_t *)resolved_target;
    log_inline_bytes("gldUpdateDispatch", target_bytes, 24);

    size_t patch_size = 0;
    if (!determine_patch_size("gldUpdateDispatch", target_bytes, 14, &patch_size)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook patch sizing failed for gldUpdateDispatch\n");
        fflush(g_log_file);
        return false;
    }

    size_t trampoline_size = patch_size + 14;
    uint8_t *trampoline = mmap(NULL, trampoline_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (trampoline == MAP_FAILED) {
        fprintf(g_log_file, "[mothervr-avp] inline hook trampoline allocation failed for gldUpdateDispatch\n");
        fflush(g_log_file);
        return false;
    }

    memcpy(trampoline, resolved_target, patch_size);
    write_absolute_jump(trampoline + patch_size, (const uint8_t *)resolved_target + patch_size);
    __builtin___clear_cache((char *)trampoline, (char *)(trampoline + trampoline_size));

    if (!protect_code_region(resolved_target, patch_size, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE | VM_PROT_COPY)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect writable failed for gldUpdateDispatch\n");
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }

    uint8_t patch[64];
    if (patch_size > sizeof(patch)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook patch buffer too small for gldUpdateDispatch size=%zu\n", patch_size);
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }
    memset(patch, 0x90, patch_size);
    write_absolute_jump(patch, (const void *)(uintptr_t)&hooked_gldUpdateDispatch);
    memcpy(resolved_target, patch, patch_size);
    __builtin___clear_cache((char *)resolved_target, (char *)resolved_target + patch_size);

    if (!protect_code_region(resolved_target, patch_size, VM_PROT_READ | VM_PROT_EXECUTE)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect restore failed for gldUpdateDispatch\n");
        fflush(g_log_file);
    }

    g_inline_update_installed = true;
    g_inline_update_trampoline = trampoline;
    g_inline_update_patch_size = patch_size;
    real_gldUpdateDispatch = (backend_probe6_fn)trampoline;
    fprintf(
        g_log_file,
        "[mothervr-avp] inline hook installed gldUpdateDispatch target=%p trampoline=%p patch_size=%zu\n",
        resolved_target,
        trampoline,
        patch_size
    );
    fflush(g_log_file);
    return true;
}

static bool install_inline_buffer_hook(void *resolved_target) {
    if (g_inline_buffer_installed && g_inline_buffer_target == resolved_target && g_inline_buffer_trampoline) {
        real_gldBufferSubData = (backend_probe6_fn)g_inline_buffer_trampoline;
        return true;
    }
    if (g_inline_buffer_attempted) {
        return false;
    }

    g_inline_buffer_attempted = true;
    g_inline_buffer_target = resolved_target;

    const uint8_t *target_bytes = (const uint8_t *)resolved_target;
    log_inline_bytes("gldBufferSubData", target_bytes, 24);

    size_t patch_size = 0;
    if (!determine_patch_size("gldBufferSubData", target_bytes, 14, &patch_size)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook patch sizing failed for gldBufferSubData\n");
        fflush(g_log_file);
        return false;
    }

    size_t trampoline_size = patch_size + 14;
    uint8_t *trampoline = mmap(NULL, trampoline_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (trampoline == MAP_FAILED) {
        fprintf(g_log_file, "[mothervr-avp] inline hook trampoline allocation failed for gldBufferSubData\n");
        fflush(g_log_file);
        return false;
    }

    memcpy(trampoline, resolved_target, patch_size);
    write_absolute_jump(trampoline + patch_size, (const uint8_t *)resolved_target + patch_size);
    __builtin___clear_cache((char *)trampoline, (char *)(trampoline + trampoline_size));

    if (!protect_code_region(resolved_target, patch_size, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE | VM_PROT_COPY)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect writable failed for gldBufferSubData\n");
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }

    uint8_t patch[64];
    if (patch_size > sizeof(patch)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook patch buffer too small for gldBufferSubData size=%zu\n", patch_size);
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }
    memset(patch, 0x90, patch_size);
    write_absolute_jump(patch, (const void *)(uintptr_t)&hooked_gldBufferSubData);
    memcpy(resolved_target, patch, patch_size);
    __builtin___clear_cache((char *)resolved_target, (char *)resolved_target + patch_size);

    if (!protect_code_region(resolved_target, patch_size, VM_PROT_READ | VM_PROT_EXECUTE)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect restore failed for gldBufferSubData\n");
        fflush(g_log_file);
    }

    g_inline_buffer_installed = true;
    g_inline_buffer_trampoline = trampoline;
    g_inline_buffer_patch_size = patch_size;
    real_gldBufferSubData = (backend_probe6_fn)trampoline;
    fprintf(
        g_log_file,
        "[mothervr-avp] inline hook installed gldBufferSubData target=%p trampoline=%p patch_size=%zu\n",
        resolved_target,
        trampoline,
        patch_size
    );
    fflush(g_log_file);
    return true;
}

static bool resolve_flush_jump_target(void *resolved_target, void **jump_target_out) {
    const uint8_t *bytes = (const uint8_t *)resolved_target;
    static const uint8_t prefix[] = {
        0x55, 0x48, 0x89, 0xE5, 0x48, 0x83, 0xBF, 0x00,
        0x0C, 0x00, 0x00, 0x00, 0x74, 0x06, 0x5D, 0xE9,
    };

    if (memcmp(bytes, prefix, sizeof(prefix)) != 0 || bytes[20] != 0x5D || bytes[21] != 0xC3) {
        return false;
    }

    int32_t rel32 = 0;
    memcpy(&rel32, bytes + 16, sizeof(rel32));
    *jump_target_out = (void *)(bytes + 20 + rel32);
    return true;
}

static bool install_inline_flush_hook(void *resolved_target) {
    if (g_inline_flush_installed && g_inline_flush_target == resolved_target && g_inline_flush_trampoline) {
        real_gldFlushContext = (backend_probe6_fn)g_inline_flush_trampoline;
        return true;
    }
    if (g_inline_flush_attempted) {
        return false;
    }

    g_inline_flush_attempted = true;
    g_inline_flush_target = resolved_target;

    const uint8_t *wrapper_bytes = (const uint8_t *)resolved_target;
    log_inline_bytes("gldFlushContext", wrapper_bytes, 24);

    void *hook_target = resolved_target;
    void *jump_target = NULL;
    if (resolve_flush_jump_target(resolved_target, &jump_target) && jump_target) {
        hook_target = jump_target;
        fprintf(
            g_log_file,
            "[mothervr-avp] gldFlushContext tail target=%p from wrapper=%p\n",
            jump_target,
            resolved_target
        );
        log_inline_bytes("gldFlushContext.tail", (const uint8_t *)jump_target, 24);
        fflush(g_log_file);
    }

    const uint8_t *target_bytes = (const uint8_t *)hook_target;
    size_t patch_size = 0;
    if (!determine_patch_size("gldFlushContext", target_bytes, 14, &patch_size)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook patch sizing failed for gldFlushContext\n");
        fflush(g_log_file);
        return false;
    }

    size_t trampoline_size = patch_size + 14;
    uint8_t *trampoline = mmap(NULL, trampoline_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (trampoline == MAP_FAILED) {
        fprintf(g_log_file, "[mothervr-avp] inline hook trampoline allocation failed for gldFlushContext\n");
        fflush(g_log_file);
        return false;
    }

    memcpy(trampoline, hook_target, patch_size);
    write_absolute_jump(trampoline + patch_size, (const uint8_t *)hook_target + patch_size);
    __builtin___clear_cache((char *)trampoline, (char *)(trampoline + trampoline_size));

    if (!protect_code_region(hook_target, patch_size, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE | VM_PROT_COPY)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect writable failed for gldFlushContext\n");
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }

    uint8_t patch[64];
    if (patch_size > sizeof(patch)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook patch buffer too small for gldFlushContext size=%zu\n", patch_size);
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }
    memset(patch, 0x90, patch_size);
    write_absolute_jump(patch, (const void *)(uintptr_t)&hooked_gldFlushContext);
    memcpy(hook_target, patch, patch_size);
    __builtin___clear_cache((char *)hook_target, (char *)hook_target + patch_size);

    if (!protect_code_region(hook_target, patch_size, VM_PROT_READ | VM_PROT_EXECUTE)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect restore failed for gldFlushContext\n");
        fflush(g_log_file);
    }

    g_inline_flush_installed = true;
    g_inline_flush_trampoline = trampoline;
    g_inline_flush_patch_size = patch_size;
    real_gldFlushContext = (backend_probe6_fn)trampoline;
    fprintf(
        g_log_file,
        "[mothervr-avp] inline hook installed gldFlushContext target=%p trampoline=%p patch_size=%zu\n",
        hook_target,
        trampoline,
        patch_size
    );
    fflush(g_log_file);
    return true;
}

static bool install_inline_render_uniform_buffers_hook(void *resolved_target) {
    if (g_inline_render_uniform_buffers_installed &&
        g_inline_render_uniform_buffers_target == resolved_target &&
        g_inline_render_uniform_buffers_trampoline) {
        real_ctx_setRenderUniformBuffers = (backend_probe6_fn)g_inline_render_uniform_buffers_trampoline;
        return true;
    }
    if (g_inline_render_uniform_buffers_attempted) {
        return false;
    }

    g_inline_render_uniform_buffers_attempted = true;
    g_inline_render_uniform_buffers_target = resolved_target;

    const uint8_t *target_bytes = (const uint8_t *)resolved_target;
    log_inline_bytes("GLDContextRec::setRenderUniformBuffers", target_bytes, 24);

    size_t patch_size = 0;
    if (!determine_patch_size("GLDContextRec::setRenderUniformBuffers", target_bytes, 14, &patch_size)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook patch sizing failed for GLDContextRec::setRenderUniformBuffers\n");
        fflush(g_log_file);
        return false;
    }

    size_t trampoline_size = patch_size + 14;
    uint8_t *trampoline = mmap(NULL, trampoline_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (trampoline == MAP_FAILED) {
        fprintf(g_log_file, "[mothervr-avp] inline hook trampoline allocation failed for GLDContextRec::setRenderUniformBuffers\n");
        fflush(g_log_file);
        return false;
    }

    memcpy(trampoline, resolved_target, patch_size);
    write_absolute_jump(trampoline + patch_size, (const uint8_t *)resolved_target + patch_size);
    __builtin___clear_cache((char *)trampoline, (char *)(trampoline + trampoline_size));

    if (!protect_code_region(resolved_target, patch_size, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE | VM_PROT_COPY)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect writable failed for GLDContextRec::setRenderUniformBuffers\n");
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }

    uint8_t patch[64];
    if (patch_size > sizeof(patch)) {
        fprintf(
            g_log_file,
            "[mothervr-avp] inline hook patch buffer too small for GLDContextRec::setRenderUniformBuffers size=%zu\n",
            patch_size
        );
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }
    memset(patch, 0x90, patch_size);
    write_absolute_jump(patch, (const void *)(uintptr_t)&hooked_ctx_setRenderUniformBuffers);
    memcpy(resolved_target, patch, patch_size);
    __builtin___clear_cache((char *)resolved_target, (char *)resolved_target + patch_size);

    if (!protect_code_region(resolved_target, patch_size, VM_PROT_READ | VM_PROT_EXECUTE)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect restore failed for GLDContextRec::setRenderUniformBuffers\n");
        fflush(g_log_file);
    }

    g_inline_render_uniform_buffers_installed = true;
    g_inline_render_uniform_buffers_trampoline = trampoline;
    g_inline_render_uniform_buffers_patch_size = patch_size;
    real_ctx_setRenderUniformBuffers = (backend_probe6_fn)trampoline;
    fprintf(
        g_log_file,
        "[mothervr-avp] inline hook installed GLDContextRec::setRenderUniformBuffers target=%p trampoline=%p patch_size=%zu\n",
        resolved_target,
        trampoline,
        patch_size
    );
    fflush(g_log_file);
    return true;
}

static bool install_inline_update_uniform_bindings_hook(void *resolved_target) {
    if (g_inline_update_uniform_bindings_installed &&
        g_inline_update_uniform_bindings_target == resolved_target &&
        g_inline_update_uniform_bindings_trampoline) {
        real_ctx_updateUniformBindings = (backend_probe6_fn)g_inline_update_uniform_bindings_trampoline;
        return true;
    }
    if (g_inline_update_uniform_bindings_attempted) {
        return false;
    }

    g_inline_update_uniform_bindings_attempted = true;
    g_inline_update_uniform_bindings_target = resolved_target;

    const uint8_t *target_bytes = (const uint8_t *)resolved_target;
    log_inline_bytes("GLDContextRec::updateUniformBindings", target_bytes, 24);

    size_t patch_size = 0;
    if (!determine_patch_size("GLDContextRec::updateUniformBindings", target_bytes, 14, &patch_size)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook patch sizing failed for GLDContextRec::updateUniformBindings\n");
        fflush(g_log_file);
        return false;
    }

    size_t trampoline_size = patch_size + 14;
    uint8_t *trampoline = mmap(NULL, trampoline_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (trampoline == MAP_FAILED) {
        fprintf(g_log_file, "[mothervr-avp] inline hook trampoline allocation failed for GLDContextRec::updateUniformBindings\n");
        fflush(g_log_file);
        return false;
    }

    memcpy(trampoline, resolved_target, patch_size);
    write_absolute_jump(trampoline + patch_size, (const uint8_t *)resolved_target + patch_size);
    __builtin___clear_cache((char *)trampoline, (char *)(trampoline + trampoline_size));

    if (!protect_code_region(resolved_target, patch_size, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE | VM_PROT_COPY)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect writable failed for GLDContextRec::updateUniformBindings\n");
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }

    uint8_t patch[64];
    if (patch_size > sizeof(patch)) {
        fprintf(
            g_log_file,
            "[mothervr-avp] inline hook patch buffer too small for GLDContextRec::updateUniformBindings size=%zu\n",
            patch_size
        );
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }
    memset(patch, 0x90, patch_size);
    write_absolute_jump(patch, (const void *)(uintptr_t)&hooked_ctx_updateUniformBindings);
    memcpy(resolved_target, patch, patch_size);
    __builtin___clear_cache((char *)resolved_target, (char *)resolved_target + patch_size);

    if (!protect_code_region(resolved_target, patch_size, VM_PROT_READ | VM_PROT_EXECUTE)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect restore failed for GLDContextRec::updateUniformBindings\n");
        fflush(g_log_file);
    }

    g_inline_update_uniform_bindings_installed = true;
    g_inline_update_uniform_bindings_trampoline = trampoline;
    g_inline_update_uniform_bindings_patch_size = patch_size;
    real_ctx_updateUniformBindings = (backend_probe6_fn)trampoline;
    fprintf(
        g_log_file,
        "[mothervr-avp] inline hook installed GLDContextRec::updateUniformBindings target=%p trampoline=%p patch_size=%zu\n",
        resolved_target,
        trampoline,
        patch_size
    );
    fflush(g_log_file);
    return true;
}

static bool install_inline_render_program_uniforms_hook(void *resolved_target) {
    if (g_inline_render_program_uniforms_installed &&
        g_inline_render_program_uniforms_target == resolved_target &&
        g_inline_render_program_uniforms_trampoline) {
        real_ctx_setRenderProgramUniforms = (backend_probe6_fn)g_inline_render_program_uniforms_trampoline;
        return true;
    }
    if (g_inline_render_program_uniforms_attempted) {
        return false;
    }

    g_inline_render_program_uniforms_attempted = true;
    g_inline_render_program_uniforms_target = resolved_target;

    const uint8_t *target_bytes = (const uint8_t *)resolved_target;
    log_inline_bytes("GLDContextRec::setRenderProgramUniforms", target_bytes, 24);

    size_t patch_size = 0;
    if (!determine_patch_size("GLDContextRec::setRenderProgramUniforms", target_bytes, 14, &patch_size)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook patch sizing failed for GLDContextRec::setRenderProgramUniforms\n");
        fflush(g_log_file);
        return false;
    }

    size_t trampoline_size = patch_size + 14;
    uint8_t *trampoline = mmap(NULL, trampoline_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (trampoline == MAP_FAILED) {
        fprintf(g_log_file, "[mothervr-avp] inline hook trampoline allocation failed for GLDContextRec::setRenderProgramUniforms\n");
        fflush(g_log_file);
        return false;
    }

    memcpy(trampoline, resolved_target, patch_size);
    write_absolute_jump(trampoline + patch_size, (const uint8_t *)resolved_target + patch_size);
    __builtin___clear_cache((char *)trampoline, (char *)(trampoline + trampoline_size));

    if (!protect_code_region(resolved_target, patch_size, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE | VM_PROT_COPY)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect writable failed for GLDContextRec::setRenderProgramUniforms\n");
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }

    uint8_t patch[64];
    if (patch_size > sizeof(patch)) {
        fprintf(
            g_log_file,
            "[mothervr-avp] inline hook patch buffer too small for GLDContextRec::setRenderProgramUniforms size=%zu\n",
            patch_size
        );
        fflush(g_log_file);
        munmap(trampoline, trampoline_size);
        return false;
    }
    memset(patch, 0x90, patch_size);
    write_absolute_jump(patch, (const void *)(uintptr_t)&hooked_ctx_setRenderProgramUniforms);
    memcpy(resolved_target, patch, patch_size);
    __builtin___clear_cache((char *)resolved_target, (char *)resolved_target + patch_size);

    if (!protect_code_region(resolved_target, patch_size, VM_PROT_READ | VM_PROT_EXECUTE)) {
        fprintf(g_log_file, "[mothervr-avp] inline hook vm_protect restore failed for GLDContextRec::setRenderProgramUniforms\n");
        fflush(g_log_file);
    }

    g_inline_render_program_uniforms_installed = true;
    g_inline_render_program_uniforms_trampoline = trampoline;
    g_inline_render_program_uniforms_patch_size = patch_size;
    real_ctx_setRenderProgramUniforms = (backend_probe6_fn)trampoline;
    fprintf(
        g_log_file,
        "[mothervr-avp] inline hook installed GLDContextRec::setRenderProgramUniforms target=%p trampoline=%p patch_size=%zu\n",
        resolved_target,
        trampoline,
        patch_size
    );
    fflush(g_log_file);
    return true;
}

static bool should_log_render_program_uniforms_hit(uint64_t hit_count) {
    return hit_count <= 4 || (hit_count % 4096ULL) == 0;
}

static bool should_emit_render_uniform_block_log(uint32_t observed_changes, size_t byte_count) {
    if (observed_changes <= 4U) {
        return true;
    }

    uint32_t interval = 256U;
    if (byte_count <= 64U) {
        interval = 32U;
    } else if (byte_count <= 256U) {
        interval = 64U;
    } else if (byte_count <= 1024U) {
        interval = 128U;
    }

    return (observed_changes % interval) == 0U;
}

static bool has_meaningful_render_uniform_change(
    const unsigned char *before,
    const unsigned char *after,
    size_t byte_count,
    float epsilon
) {
    for (size_t offset = 0; offset + sizeof(float) <= byte_count; offset += sizeof(float)) {
        float before_value = 0.0f;
        float after_value = 0.0f;
        memcpy(&before_value, before + offset, sizeof(before_value));
        memcpy(&after_value, after + offset, sizeof(after_value));

        bool before_finite = isfinite(before_value);
        bool after_finite = isfinite(after_value);
        if (before_finite != after_finite) {
            return true;
        }
        if (!before_finite) {
            continue;
        }
        if (fabsf(after_value - before_value) >= epsilon) {
            return true;
        }
    }

    return false;
}

static bool read_render_uniform_float(
    const unsigned char *bytes,
    size_t byte_count,
    size_t float_index,
    float *out_value
) {
    size_t offset = float_index * sizeof(float);
    if (!out_value || offset + sizeof(float) > byte_count) {
        return false;
    }

    memcpy(out_value, bytes + offset, sizeof(*out_value));
    return isfinite(*out_value);
}

static float distance3f(const float a[3], const float b[3]);

static bool extract_render_program_pose_candidate(
    const unsigned char *bytes,
    size_t byte_count,
    bool have_reference_translation,
    const float reference_translation[3],
    float rows[3][3],
    float translation[3],
    size_t *matched_base_index,
    size_t *matched_row_stride
) {
    static const size_t row_strides[] = {4, 5};
    float best_rows[3][3] = {{0}};
    float best_translation[3] = {0};
    size_t best_base_index = 0;
    size_t best_row_stride = 0;
    float best_pose_score = FLT_MAX;
    float best_reference_distance = FLT_MAX;
    bool found = false;

    if (!bytes || byte_count < (12 * sizeof(float))) {
        return false;
    }
    size_t total_float_count = byte_count / sizeof(float);
    for (size_t stride_index = 0; stride_index < (sizeof(row_strides) / sizeof(row_strides[0])); ++stride_index) {
        size_t row_stride = row_strides[stride_index];
        size_t translation_offset = row_stride - 1;
        size_t minimum_float_count = (row_stride * 2) + translation_offset + 1;
        if (total_float_count < minimum_float_count) {
            continue;
        }

        size_t last_base_index = total_float_count - minimum_float_count;
        for (size_t base_index = 0; base_index <= last_base_index; ++base_index) {
            float candidate_rows[3][3] = {{0}};
            float candidate_translation[3] = {0};
            float lengths[3] = {0};
            bool valid = true;
            for (size_t row = 0; row < 3; ++row) {
                size_t row_base = base_index + (row * row_stride);
                if (!read_render_uniform_float(bytes, byte_count, row_base + 0, &candidate_rows[row][0]) ||
                    !read_render_uniform_float(bytes, byte_count, row_base + 1, &candidate_rows[row][1]) ||
                    !read_render_uniform_float(bytes, byte_count, row_base + 2, &candidate_rows[row][2]) ||
                    !read_render_uniform_float(
                        bytes,
                        byte_count,
                        row_base + translation_offset,
                        &candidate_translation[row]
                    )) {
                    valid = false;
                    break;
                }

                lengths[row] = sqrtf(
                    candidate_rows[row][0] * candidate_rows[row][0] +
                    candidate_rows[row][1] * candidate_rows[row][1] +
                    candidate_rows[row][2] * candidate_rows[row][2]
                );
                if (!isfinite(lengths[row]) || lengths[row] < 0.4f || lengths[row] > 1.6f) {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                continue;
            }

            float dot01 =
                candidate_rows[0][0] * candidate_rows[1][0] +
                candidate_rows[0][1] * candidate_rows[1][1] +
                candidate_rows[0][2] * candidate_rows[1][2];
            float dot02 =
                candidate_rows[0][0] * candidate_rows[2][0] +
                candidate_rows[0][1] * candidate_rows[2][1] +
                candidate_rows[0][2] * candidate_rows[2][2];
            float dot12 =
                candidate_rows[1][0] * candidate_rows[2][0] +
                candidate_rows[1][1] * candidate_rows[2][1] +
                candidate_rows[1][2] * candidate_rows[2][2];
            if (!isfinite(dot01) || !isfinite(dot02) || !isfinite(dot12)) {
                continue;
            }
            if (fabsf(dot01) > 0.35f || fabsf(dot02) > 0.35f || fabsf(dot12) > 0.35f) {
                continue;
            }

            float pose_score =
                fabsf(lengths[0] - 1.0f) +
                fabsf(lengths[1] - 1.0f) +
                fabsf(lengths[2] - 1.0f) +
                fabsf(dot01) +
                fabsf(dot02) +
                fabsf(dot12);
            float reference_distance = 0.0f;
            if (have_reference_translation) {
                reference_distance = distance3f(candidate_translation, reference_translation);
            }

            bool should_replace = false;
            if (!found) {
                should_replace = true;
            } else if (have_reference_translation) {
                if (reference_distance + 0.0001f < best_reference_distance) {
                    should_replace = true;
                } else if (fabsf(reference_distance - best_reference_distance) <= 0.0001f &&
                           pose_score < best_pose_score) {
                    should_replace = true;
                }
            } else if (pose_score < best_pose_score) {
                should_replace = true;
            }

            if (!should_replace) {
                continue;
            }

            memcpy(best_rows, candidate_rows, sizeof(best_rows));
            memcpy(best_translation, candidate_translation, sizeof(best_translation));
            best_base_index = base_index;
            best_row_stride = row_stride;
            best_pose_score = pose_score;
            best_reference_distance = reference_distance;
            found = true;
        }
    }

    if (!found) {
        return false;
    }

    memcpy(rows, best_rows, sizeof(best_rows));
    memcpy(translation, best_translation, sizeof(best_translation));
    if (matched_base_index) {
        *matched_base_index = best_base_index;
    }
    if (matched_row_stride) {
        *matched_row_stride = best_row_stride;
    }
    return true;
}

static float distance3f(const float a[3], const float b[3]) {
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static float shortest_angle_delta_degrees(float previous_deg, float current_deg) {
    float delta_deg = current_deg - previous_deg;
    while (delta_deg > 180.0f) {
        delta_deg -= 360.0f;
    }
    while (delta_deg < -180.0f) {
        delta_deg += 360.0f;
    }
    return delta_deg;
}

typedef struct {
    const char *label;
    size_t resource_offset;
    size_t data_offset;
} UniformBindingBank;

static void maybe_log_render_uniform_slot_sample(
    uint64_t hit_count,
    uintptr_t ctx_ptr,
    const char *bank_label,
    uint32_t slot_index,
    uintptr_t resource_ptr,
    uintptr_t data_ptr
) {
    if (!data_ptr) {
        return;
    }

    size_t sample_byte_count = 64;
    unsigned char sample_bytes[RENDER_UNIFORM_BLOCK_SAMPLE_BYTES] = {0};
    memcpy(sample_bytes, (const void *)data_ptr, sample_byte_count);
    uint64_t hash = hash_backend_buffer_bytes(sample_bytes, sample_byte_count);
    size_t float_count = sample_byte_count / sizeof(float);
    float floats[16] = {0};
    memcpy(floats, (const void *)data_ptr, sample_byte_count);

    pthread_mutex_lock(&g_lock);
    uintptr_t watch_identity_b = resource_ptr ^ (((uintptr_t)slot_index) << 32);
    RenderUniformBlockWatch *watch =
        find_render_uniform_block_watch(data_ptr, watch_identity_b, sample_byte_count);
    if (!watch) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (watch->last_hash == hash) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (watch->last_hash != 0 &&
        !has_meaningful_render_uniform_change(watch->last_bytes, sample_bytes, sample_byte_count, 0.005f)) {
        memcpy(watch->last_bytes, sample_bytes, sample_byte_count);
        watch->last_hash = hash;
        pthread_mutex_unlock(&g_lock);
        return;
    }

    watch->observed_changes += 1;
    if (!should_emit_render_uniform_block_log(watch->observed_changes, sample_byte_count)) {
        memcpy(watch->last_bytes, sample_bytes, sample_byte_count);
        watch->last_hash = hash;
        pthread_mutex_unlock(&g_lock);
        return;
    }

    watch->logged_changes += 1;
    fprintf(
        g_log_file,
        "[mothervr-avp] render_uniform_slot bank=%s slot=%u observed=%u logged=%u hit=%llu ctx=%p resource=%p data=%p sampled=%zu\n",
        bank_label,
        slot_index,
        watch->observed_changes,
        watch->logged_changes,
        (unsigned long long)hit_count,
        (void *)ctx_ptr,
        (void *)resource_ptr,
        (void *)data_ptr,
        sample_byte_count
    );
    fprintf(g_log_file, "  render_uniform_floats:");
    for (size_t i = 0; i < float_count; ++i) {
        fprintf(g_log_file, " %.5f", floats[i]);
    }
    fprintf(g_log_file, "\n");
    log_backend_buffer_float_deltas_with_label("render_uniform_delta", watch->last_bytes, sample_bytes, sample_byte_count);
    memcpy(watch->last_bytes, sample_bytes, sample_byte_count);
    watch->last_hash = hash;
    fflush(g_log_file);
    pthread_mutex_unlock(&g_lock);
}

static void maybe_log_render_uniform_buffers_sample(uint64_t hit_count, uintptr_t ctx_ptr) {
    if (!ctx_ptr) {
        return;
    }

    static const UniformBindingBank banks[] = {
        {"renderA", 0x95a8, 0x9848},
        {"renderB", 0x9628, 0x98c8},
        {"renderC", 0x96a8, 0x9948},
        {"renderD", 0x9728, 0x99c8},
        {"renderE", 0x97a8, 0x9a48},
    };

    const uint8_t *ctx = (const uint8_t *)ctx_ptr;
    for (size_t bank_index = 0; bank_index < sizeof(banks) / sizeof(banks[0]); ++bank_index) {
        const UniformBindingBank *bank = &banks[bank_index];
        for (uint32_t slot = 0; slot < 16; ++slot) {
            uintptr_t resource_ptr = 0;
            uintptr_t data_ptr = 0;
            memcpy(&resource_ptr, ctx + bank->resource_offset + (size_t)slot * sizeof(uintptr_t), sizeof(uintptr_t));
            memcpy(&data_ptr, ctx + bank->data_offset + (size_t)slot * sizeof(uintptr_t), sizeof(uintptr_t));
            if (resource_ptr || data_ptr) {
                maybe_log_render_uniform_slot_sample(hit_count, ctx_ptr, bank->label, slot, resource_ptr, data_ptr);
            }
        }
    }
}

static bool should_log_backend_hit(const char *symbol, uint64_t hit_count) {
    if (symbol && strcmp(symbol, "gldBufferSubData") == 0) {
        return hit_count <= 20 || (hit_count % 1000) == 0;
    }
    return hit_count <= 5 || (hit_count % 5000) == 0;
}

static void log_backend_hit(
    const char *symbol,
    uint64_t hit_count,
    uintptr_t a0,
    uintptr_t a1,
    uintptr_t a2,
    uintptr_t a3,
    uintptr_t a4,
    uintptr_t a5
) {
    char line[512];
    if (symbol && strcmp(symbol, "gldBufferSubData") == 0) {
        snprintf(
            line,
            sizeof(line),
            "[mothervr-avp] backend hit %s count=%llu a0=%p a1=%p a2=%p a3=%p a4=%p a5=%p",
            symbol,
            (unsigned long long)hit_count,
            (void *)a0,
            (void *)a1,
            (void *)a2,
            (void *)a3,
            (void *)a4,
            (void *)a5
        );
    } else {
        snprintf(
            line,
            sizeof(line),
            "[mothervr-avp] backend hit %s count=%llu a0=%p a1=%p",
            symbol,
            (unsigned long long)hit_count,
            (void *)a0,
            (void *)a1
        );
    }
    raw_log_line(line);
}

static DispatchPatchRecord *find_dispatch_patch_record(uintptr_t table_ptr) {
    DispatchPatchRecord *empty_slot = NULL;
    for (size_t i = 0; i < MAX_DISPATCH_PATCH_RECORDS; ++i) {
        DispatchPatchRecord *slot = &g_dispatch_patch_records[i];
        if (slot->occupied) {
            if (slot->table_ptr == table_ptr) {
                return slot;
            }
            continue;
        }
        if (!empty_slot) {
            empty_slot = slot;
        }
    }

    if (!empty_slot) {
        return NULL;
    }

    empty_slot->occupied = true;
    empty_slot->table_ptr = table_ptr;
    empty_slot->last_attempt_hit = 0;
    empty_slot->logged_no_matches = false;
    empty_slot->logged_slot_dump = false;
    empty_slot->total_slots_patched = 0;
    return empty_slot;
}

static void maybe_patch_dispatch_table(uintptr_t table_ptr, uint64_t hit_count) {
    if (!table_ptr) {
        return;
    }

    pthread_mutex_lock(&g_lock);
    resolve_symbols();

    DispatchPatchRecord *record = find_dispatch_patch_record(table_ptr);
    if (!record) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (record->total_slots_patched > 0 || record->last_attempt_hit == hit_count) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    record->last_attempt_hit = hit_count;

    const DispatchPatchTarget targets[] = {
        {"glUseProgram", (const void *)(uintptr_t)real_glUseProgram, (const void *)(uintptr_t)&hooked_glUseProgram},
        {"glGetUniformLocation", (const void *)(uintptr_t)real_glGetUniformLocation, (const void *)(uintptr_t)&hooked_glGetUniformLocation},
        {"glBindBuffer", (const void *)(uintptr_t)real_glBindBuffer, (const void *)(uintptr_t)&hooked_glBindBuffer},
        {"glBindBufferBase", (const void *)(uintptr_t)real_glBindBufferBase, (const void *)(uintptr_t)&hooked_glBindBufferBase},
        {"glBindBufferRange", (const void *)(uintptr_t)real_glBindBufferRange, (const void *)(uintptr_t)&hooked_glBindBufferRange},
        {"glBufferSubData", (const void *)(uintptr_t)real_glBufferSubData, (const void *)(uintptr_t)&hooked_glBufferSubData},
        {"glMapBufferRange", (const void *)(uintptr_t)real_glMapBufferRange, (const void *)(uintptr_t)&hooked_glMapBufferRange},
        {"glUniformMatrix4fv", (const void *)(uintptr_t)real_glUniformMatrix4fv, (const void *)(uintptr_t)&hooked_glUniformMatrix4fv},
        {"glProgramUniformMatrix4fv", (const void *)(uintptr_t)real_glProgramUniformMatrix4fv, (const void *)(uintptr_t)&hooked_glProgramUniformMatrix4fv},
        {"glUniform4fv", (const void *)(uintptr_t)real_glUniform4fv, (const void *)(uintptr_t)&hooked_glUniform4fv},
        {"glProgramUniform4fv", (const void *)(uintptr_t)real_glProgramUniform4fv, (const void *)(uintptr_t)&hooked_glProgramUniform4fv},
    };
    uint32_t patched_counts[sizeof(targets) / sizeof(targets[0])] = {0};
    uintptr_t *slots = (uintptr_t *)table_ptr;
    uint32_t total_patched = 0;

    for (size_t slot_idx = 0; slot_idx < DISPATCH_TABLE_SCAN_SLOTS; ++slot_idx) {
        uintptr_t current = slots[slot_idx];
        for (size_t target_idx = 0; target_idx < sizeof(targets) / sizeof(targets[0]); ++target_idx) {
            uintptr_t original = (uintptr_t)targets[target_idx].original;
            uintptr_t replacement = (uintptr_t)targets[target_idx].replacement;
            if (!original || current != original || current == replacement) {
                continue;
            }
            slots[slot_idx] = replacement;
            patched_counts[target_idx] += 1;
            total_patched += 1;
            break;
        }
    }

    if (total_patched > 0) {
        record->total_slots_patched += total_patched;
        fprintf(
            g_log_file,
            "[mothervr-avp] dispatch table patched table=%p hit=%llu slots=%u",
            (void *)table_ptr,
            (unsigned long long)hit_count,
            total_patched
        );
        for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
            if (patched_counts[i] == 0) {
                continue;
            }
            fprintf(g_log_file, " %s=%u", targets[i].symbol, patched_counts[i]);
        }
        fprintf(g_log_file, "\n");
        fflush(g_log_file);
    } else if (!record->logged_no_matches) {
        fprintf(
            g_log_file,
            "[mothervr-avp] dispatch table scan table=%p hit=%llu found no cached GL entry points\n",
            (void *)table_ptr,
            (unsigned long long)hit_count
        );
        if (!record->logged_slot_dump) {
            uint32_t dumped = 0;
            for (size_t slot_idx = 0; slot_idx < DISPATCH_TABLE_SCAN_SLOTS && dumped < 48; ++slot_idx) {
                uintptr_t current = slots[slot_idx];
                if (!current) {
                    continue;
                }

                Dl_info info = {0};
                const char *image_name = "<unknown>";
                const char *symbol_name = "<none>";
                if (dladdr((const void *)current, &info)) {
                    if (info.dli_fname && info.dli_fname[0]) {
                        image_name = info.dli_fname;
                    }
                    if (info.dli_sname && info.dli_sname[0]) {
                        symbol_name = info.dli_sname;
                    }
                }

                bool interesting_image = strstr(image_name, "AppleMetalOpenGLRenderer") ||
                                         strstr(image_name, "GLEngine") ||
                                         strstr(image_name, "OpenGL.framework");
                bool interesting_symbol = strstr(symbol_name, "Program") ||
                                          strstr(symbol_name, "Uniform") ||
                                          strstr(symbol_name, "Shader") ||
                                          strstr(symbol_name, "Matrix") ||
                                          strstr(symbol_name, "State") ||
                                          strstr(symbol_name, "Texture") ||
                                          strstr(symbol_name, "Framebuffer") ||
                                          strstr(symbol_name, "Dispatch") ||
                                          strstr(symbol_name, "Flush") ||
                                          strstr(symbol_name, "Present") ||
                                          strstr(symbol_name, "Attach") ||
                                          strstr(symbol_name, "Bind") ||
                                          strstr(symbol_name, "MapBuffer") ||
                                          strstr(symbol_name, "CopyBuffer") ||
                                          strstr(symbol_name, "BufferSubData") ||
                                          strstr(symbol_name, "BufferData");
                bool noisy_symbol = strstr(symbol_name, "Render") ||
                                    strstr(symbol_name, "DrawPixels") ||
                                    strstr(symbol_name, "CopyPixels") ||
                                    strstr(symbol_name, "PrimitiveBuffer") ||
                                    strstr(symbol_name, "ElementBuffer") ||
                                    strstr(symbol_name, "VertexArray") ||
                                    strstr(symbol_name, "Clear");
                if (!interesting_image || !interesting_symbol || noisy_symbol) {
                    continue;
                }

                fprintf(
                    g_log_file,
                    "[mothervr-avp] dispatch slot table=%p slot=%zu fn=%p image=%s symbol=%s\n",
                    (void *)table_ptr,
                    slot_idx,
                    (const void *)current,
                    image_name,
                    symbol_name
                );
                dumped += 1;
            }
            if (dumped == 0) {
                fprintf(
                    g_log_file,
                    "[mothervr-avp] dispatch slot scan table=%p found no state/program/buffer-like symbols in first %u slots\n",
                    (void *)table_ptr,
                    (unsigned)DISPATCH_TABLE_SCAN_SLOTS
                );
            }
            record->logged_slot_dump = true;
        }
        fflush(g_log_file);
        record->logged_no_matches = true;
    }

    pthread_mutex_unlock(&g_lock);
}

static void maybe_note_backend_probe_target(size_t slot, const char *symbol, void *resolved) {
    if (!resolved || slot >= (sizeof(g_last_backend_probe_targets) / sizeof(g_last_backend_probe_targets[0]))) {
        return;
    }
    if (g_last_backend_probe_targets[slot] == resolved) {
        return;
    }

    Dl_info info = {0};
    const char *image_name = "<unknown>";
    if (dladdr(resolved, &info) && info.dli_fname && info.dli_fname[0]) {
        image_name = info.dli_fname;
    }

    char line[512];
    snprintf(
        line,
        sizeof(line),
        "[mothervr-avp] backend probe armed %s -> %p (%s)",
        symbol,
        resolved,
        image_name
    );
    raw_log_line(line);
    g_last_backend_probe_targets[slot] = resolved;
}

static uintptr_t dispatch_backend_probe(
    const char *symbol,
    backend_probe6_fn *real_slot,
    const void *replacement,
    uint64_t *counter,
    uintptr_t a0,
    uintptr_t a1,
    uintptr_t a2,
    uintptr_t a3,
    uintptr_t a4,
    uintptr_t a5
) {
    uint64_t hit_count = __sync_add_and_fetch(counter, 1);
    if (should_log_backend_hit(symbol, hit_count)) {
        log_backend_hit(symbol, hit_count, a0, a1, a2, a3, a4, a5);
    }

    backend_probe6_fn real = *real_slot;
    if (!real) {
        void *resolved = resolve_visible_symbol(symbol);
        if (!resolved || resolved == replacement) {
            return 0;
        }
        real = (backend_probe6_fn)resolved;
        *real_slot = real;
    }

    return real(a0, a1, a2, a3, a4, a5);
}

static const char *target_name(GLenum target) {
    switch (target) {
        case GL_ARRAY_BUFFER:
            return "GL_ARRAY_BUFFER";
        case GL_ELEMENT_ARRAY_BUFFER:
            return "GL_ELEMENT_ARRAY_BUFFER";
        case GL_UNIFORM_BUFFER:
            return "GL_UNIFORM_BUFFER";
        case GL_PIXEL_PACK_BUFFER:
            return "GL_PIXEL_PACK_BUFFER";
        case GL_PIXEL_UNPACK_BUFFER:
            return "GL_PIXEL_UNPACK_BUFFER";
        default:
            return "GL_UNKNOWN_TARGET";
    }
}

static const void *replacement_for_symbol_name(const char *symbol) {
    if (!symbol) {
        return NULL;
    }
    if (strcmp(symbol, "glUseProgram") == 0) {
        return (const void *)(uintptr_t)&hooked_glUseProgram;
    }
    if (strcmp(symbol, "glGetUniformLocation") == 0) {
        return (const void *)(uintptr_t)&hooked_glGetUniformLocation;
    }
    if (strcmp(symbol, "glBindBuffer") == 0) {
        return (const void *)(uintptr_t)&hooked_glBindBuffer;
    }
    if (strcmp(symbol, "glBindBufferBase") == 0) {
        return (const void *)(uintptr_t)&hooked_glBindBufferBase;
    }
    if (strcmp(symbol, "glBindBufferRange") == 0) {
        return (const void *)(uintptr_t)&hooked_glBindBufferRange;
    }
    if (strcmp(symbol, "glBufferSubData") == 0) {
        return (const void *)(uintptr_t)&hooked_glBufferSubData;
    }
    if (strcmp(symbol, "glMapBufferRange") == 0) {
        return (const void *)(uintptr_t)&hooked_glMapBufferRange;
    }
    if (strcmp(symbol, "glUniformMatrix4fv") == 0) {
        return (const void *)(uintptr_t)&hooked_glUniformMatrix4fv;
    }
    if (strcmp(symbol, "glProgramUniformMatrix4fv") == 0) {
        return (const void *)(uintptr_t)&hooked_glProgramUniformMatrix4fv;
    }
    if (strcmp(symbol, "glUniform4fv") == 0) {
        return (const void *)(uintptr_t)&hooked_glUniform4fv;
    }
    if (strcmp(symbol, "glProgramUniform4fv") == 0) {
        return (const void *)(uintptr_t)&hooked_glProgramUniform4fv;
    }
    if (strcmp(symbol, "CGLFlushDrawable") == 0) {
        return (const void *)(uintptr_t)&hooked_CGLFlushDrawable;
    }
    if (strcmp(symbol, "dlsym") == 0) {
        return (const void *)(uintptr_t)&hooked_dlsym;
    }
    return NULL;
}

static bool should_trace_backend_symbol(const char *symbol) {
    if (!symbol) {
        return false;
    }
    return strcmp(symbol, "gldUpdateDispatch") == 0 ||
           strcmp(symbol, "gldPresentFramebufferData") == 0 ||
           strcmp(symbol, "gldBufferSubData") == 0 ||
           strcmp(symbol, "gldFlushContext") == 0 ||
           strcmp(symbol, "gliCreateContext") == 0 ||
           strcmp(symbol, "gliAttachDrawable") == 0;
}

static bool should_log_dynamic_symbol(const char *symbol) {
    if (!symbol) {
        return false;
    }
    if (replacement_for_symbol_name(symbol)) {
        return true;
    }
    if (should_trace_backend_symbol(symbol)) {
        return true;
    }
    return strncmp(symbol, "gl", 2) == 0 || strncmp(symbol, "CGL", 3) == 0;
}

static void *resolve_next_symbol(const char *symbol) {
    if (real_dlsym) {
        return real_dlsym(RTLD_NEXT, symbol);
    }
    return dlsym(RTLD_NEXT, symbol);
}

static void *resolve_visible_symbol(const char *symbol) {
    if (real_dlsym) {
        return real_dlsym(RTLD_DEFAULT, symbol);
    }
    return dlsym(RTLD_DEFAULT, symbol);
}

static void *resolve_symbol_in_loaded_image(const char *image_fragment, const char *symbol) {
    if (!image_fragment || !symbol || !symbol[0]) {
        return NULL;
    }

    uint32_t image_count = _dyld_image_count();
    for (uint32_t i = 0; i < image_count; ++i) {
        const char *image_name = _dyld_get_image_name(i);
        if (!image_name || !strstr(image_name, image_fragment)) {
            continue;
        }

        void *handle = dlopen(image_name, RTLD_LAZY | RTLD_NOLOAD);
        if (!handle) {
            continue;
        }

        void *resolved = real_dlsym ? real_dlsym(handle, symbol) : dlsym(handle, symbol);
        if (resolved) {
            return resolved;
        }
    }

    return NULL;
}

static void *resolve_offset_in_loaded_image(const char *image_fragment, uintptr_t symbol_offset) {
    if (!image_fragment || !symbol_offset) {
        return NULL;
    }

    uint32_t image_count = _dyld_image_count();
    for (uint32_t i = 0; i < image_count; ++i) {
        const char *image_name = _dyld_get_image_name(i);
        if (!image_name || !strstr(image_name, image_fragment)) {
            continue;
        }

        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        return (void *)(uintptr_t)(symbol_offset + (uintptr_t)slide);
    }

    return NULL;
}

static bool should_log_image_name(const char *image_name) {
    if (!image_name) {
        return false;
    }
    return strstr(image_name, "OpenGL") != NULL ||
           strstr(image_name, "GLRenderer") != NULL ||
           strstr(image_name, "MetalOpenGL") != NULL ||
           strstr(image_name, "libGL") != NULL ||
           strstr(image_name, "CGL") != NULL ||
           strstr(image_name, "IOSurface") != NULL;
}

static void log_new_graphics_images(uint32_t start_index, uint32_t end_index) {
    bool wrote_anything = false;

    for (uint32_t i = start_index; i < end_index; ++i) {
        const char *image_name = _dyld_get_image_name(i);
        if (!should_log_image_name(image_name)) {
            continue;
        }
        fprintf(g_log_file, "[mothervr-avp] new image[%u] %s\n", i, image_name);
        wrote_anything = true;
    }

    if (wrote_anything) {
        fflush(g_log_file);
    }
}

static void log_backend_symbol_snapshot(const char *reason, bool only_changes) {
    bool wrote_anything = false;

    for (size_t i = 0; i < sizeof(g_backend_symbols) / sizeof(g_backend_symbols[0]); ++i) {
        void *resolved = resolve_visible_symbol(g_backend_symbols[i]);
        if (!only_changes || resolved != g_last_backend_symbol_addresses[i]) {
            Dl_info info = {0};
            const char *image_name = "<unknown>";
            if (resolved && dladdr(resolved, &info) && info.dli_fname && info.dli_fname[0]) {
                image_name = info.dli_fname;
            }
            fprintf(
                g_log_file,
                "[mothervr-avp] backend snapshot[%s] %s -> %p (%s)\n",
                reason,
                g_backend_symbols[i],
                resolved,
                image_name
            );
            wrote_anything = true;
        }
        g_last_backend_symbol_addresses[i] = resolved;
    }

    if (wrote_anything) {
        fflush(g_log_file);
    }
}

static void resolve_symbols(void) {
    if (g_initialized) {
        return;
    }

    g_log_file = fopen(active_log_path(), "a");
    if (!g_log_file) {
        g_log_file = stderr;
    }

    real_dlsym = (dlsym_fn)dlsym(RTLD_NEXT, "dlsym");
    real_glUseProgram = (glUseProgram_fn)resolve_next_symbol("glUseProgram");
    real_glGetUniformLocation = (glGetUniformLocation_fn)resolve_next_symbol("glGetUniformLocation");
    real_glBindBuffer = (glBindBuffer_fn)resolve_next_symbol("glBindBuffer");
    real_glBindBufferBase = (glBindBufferBase_fn)resolve_next_symbol("glBindBufferBase");
    real_glBindBufferRange = (glBindBufferRange_fn)resolve_next_symbol("glBindBufferRange");
    real_glBufferSubData = (glBufferSubData_fn)resolve_next_symbol("glBufferSubData");
    real_glMapBufferRange = (glMapBufferRange_fn)resolve_next_symbol("glMapBufferRange");
    real_glUniformMatrix4fv = (glUniformMatrix4fv_fn)resolve_next_symbol("glUniformMatrix4fv");
    real_glProgramUniformMatrix4fv = (glProgramUniformMatrix4fv_fn)resolve_next_symbol("glProgramUniformMatrix4fv");
    real_glUniform4fv = (glUniform4fv_fn)resolve_next_symbol("glUniform4fv");
    real_glProgramUniform4fv = (glProgramUniform4fv_fn)resolve_next_symbol("glProgramUniform4fv");
    real_CGLFlushDrawable = (CGLFlushDrawable_fn)resolve_next_symbol("CGLFlushDrawable");

    fprintf(g_log_file, "[mothervr-avp] recon interposer initialized (pid=%d)\n", getpid());
    fflush(g_log_file);
    g_initialized = true;
}

static void apply_runtime_interpose_if_possible(void) {
    resolve_symbols();

    if (!dyld_dynamic_interpose) {
        if (!g_runtime_interpose_attempted) {
            fprintf(g_log_file, "[mothervr-avp] dyld_dynamic_interpose unavailable\n");
            fflush(g_log_file);
        }
        g_runtime_interpose_attempted = true;
        return;
    }

    uint32_t image_count = _dyld_image_count();
    uint32_t previous_image_count = g_last_interposed_image_count;
    if (g_runtime_interpose_attempted && image_count == previous_image_count) {
        return;
    }

    interpose_t tuples[24];
    size_t tuple_count = 0;
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_glUseProgram, (const void *)(uintptr_t)&glUseProgram};
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_glGetUniformLocation, (const void *)(uintptr_t)&glGetUniformLocation};
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_glBindBuffer, (const void *)(uintptr_t)&glBindBuffer};
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_glBindBufferBase, (const void *)(uintptr_t)&glBindBufferBase};
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_glBindBufferRange, (const void *)(uintptr_t)&glBindBufferRange};
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_glBufferSubData, (const void *)(uintptr_t)&glBufferSubData};
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_glMapBufferRange, (const void *)(uintptr_t)&glMapBufferRange};
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_glUniformMatrix4fv, (const void *)(uintptr_t)&glUniformMatrix4fv};
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_glProgramUniformMatrix4fv, (const void *)(uintptr_t)&glProgramUniformMatrix4fv};
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_glUniform4fv, (const void *)(uintptr_t)&glUniform4fv};
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_glProgramUniform4fv, (const void *)(uintptr_t)&glProgramUniform4fv};
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_CGLFlushDrawable, (const void *)(uintptr_t)&CGLFlushDrawable};
    tuples[tuple_count++] = (interpose_t){(const void *)(uintptr_t)&hooked_dlsym, (const void *)(uintptr_t)&dlsym};

    const struct {
        const char *symbol;
        const void *replacement;
        backend_probe6_fn *real_slot;
        size_t probe_slot;
    } backend_specs[] = {
        {"gldUpdateDispatch", (const void *)(uintptr_t)&hooked_gldUpdateDispatch, &real_gldUpdateDispatch, 0},
        {"gldPresentFramebufferData", (const void *)(uintptr_t)&hooked_gldPresentFramebufferData, &real_gldPresentFramebufferData, 1},
        {"gldBufferSubData", (const void *)(uintptr_t)&hooked_gldBufferSubData, &real_gldBufferSubData, 2},
        {"gldFlushContext", (const void *)(uintptr_t)&hooked_gldFlushContext, &real_gldFlushContext, 3},
        {"gliAttachDrawable", (const void *)(uintptr_t)&hooked_gliAttachDrawable, &real_gliAttachDrawable, 4},
    };

    for (size_t i = 0; i < sizeof(backend_specs) / sizeof(backend_specs[0]); ++i) {
        void *resolved = resolve_visible_symbol(backend_specs[i].symbol);
        if (strncmp(backend_specs[i].symbol, "gld", 3) == 0) {
            void *metal_resolved = resolve_symbol_in_loaded_image("AppleMetalOpenGLRenderer", backend_specs[i].symbol);
            if (metal_resolved) {
                resolved = metal_resolved;
            }
        }
        if (!resolved || resolved == backend_specs[i].replacement) {
            continue;
        }
        maybe_note_backend_probe_target(backend_specs[i].probe_slot, backend_specs[i].symbol, resolved);

        if (strcmp(backend_specs[i].symbol, "gldUpdateDispatch") == 0 && install_inline_update_hook(resolved)) {
            continue;
        }
        if (strcmp(backend_specs[i].symbol, "gldPresentFramebufferData") == 0 && install_inline_present_hook(resolved)) {
            continue;
        }
        if (strcmp(backend_specs[i].symbol, "gldBufferSubData") == 0 && install_inline_buffer_hook(resolved)) {
            continue;
        }
        if (strcmp(backend_specs[i].symbol, "gldFlushContext") == 0 && install_inline_flush_hook(resolved)) {
            continue;
        }

        *backend_specs[i].real_slot = (backend_probe6_fn)resolved;
        tuples[tuple_count++] = (interpose_t){backend_specs[i].replacement, resolved};
    }

    void *update_uniform_bindings = resolve_offset_in_loaded_image(
        "AppleMetalOpenGLRenderer",
        APPLE_METAL_OPENGL_RENDERER_UPDATE_UNIFORM_BINDINGS_X86_64_OFFSET
    );
    if (update_uniform_bindings && update_uniform_bindings != g_inline_update_uniform_bindings_target) {
        fprintf(
            g_log_file,
            "[mothervr-avp] backend probe armed GLDContextRec::updateUniformBindings -> %p (/System/Library/Extensions/AppleMetalOpenGLRenderer.bundle/Contents/MacOS/AppleMetalOpenGLRenderer)\n",
            update_uniform_bindings
        );
        fflush(g_log_file);
    }
    if (update_uniform_bindings) {
        install_inline_update_uniform_bindings_hook(update_uniform_bindings);
    }

    void *render_uniform_buffers = resolve_offset_in_loaded_image(
        "AppleMetalOpenGLRenderer",
        APPLE_METAL_OPENGL_RENDERER_SET_RENDER_UNIFORM_BUFFERS_X86_64_OFFSET
    );
    if (render_uniform_buffers && render_uniform_buffers != g_inline_render_uniform_buffers_target) {
        fprintf(
            g_log_file,
            "[mothervr-avp] backend probe armed GLDContextRec::setRenderUniformBuffers -> %p (/System/Library/Extensions/AppleMetalOpenGLRenderer.bundle/Contents/MacOS/AppleMetalOpenGLRenderer)\n",
            render_uniform_buffers
        );
        fflush(g_log_file);
    }
    if (render_uniform_buffers) {
        install_inline_render_uniform_buffers_hook(render_uniform_buffers);
    }

    void *render_program_uniforms = resolve_offset_in_loaded_image(
        "AppleMetalOpenGLRenderer",
        APPLE_METAL_OPENGL_RENDERER_SET_RENDER_PROGRAM_UNIFORMS_X86_64_OFFSET
    );
    if (render_program_uniforms && render_program_uniforms != g_inline_render_program_uniforms_target) {
        fprintf(
            g_log_file,
            "[mothervr-avp] backend probe armed GLDContextRec::setRenderProgramUniforms -> %p (/System/Library/Extensions/AppleMetalOpenGLRenderer.bundle/Contents/MacOS/AppleMetalOpenGLRenderer)\n",
            render_program_uniforms
        );
        fflush(g_log_file);
    }
    if (render_program_uniforms) {
        install_inline_render_program_uniforms_hook(render_program_uniforms);
    }

    for (uint32_t i = 0; i < image_count; ++i) {
        const struct mach_header *header = _dyld_get_image_header(i);
        if (!header) {
            continue;
        }
        dyld_dynamic_interpose(header, tuples, tuple_count);
    }

    if (image_count > previous_image_count) {
        log_new_graphics_images(previous_image_count, image_count);
    }

    g_runtime_interpose_attempted = true;
    g_last_interposed_image_count = image_count;
    fprintf(
        g_log_file,
        "[mothervr-avp] runtime interpose applied to %u images (previously %u) with %zu tuples\n",
        image_count,
        previous_image_count,
        tuple_count
    );
    fflush(g_log_file);
}

static void *backend_probe_thread(void *unused) {
    (void)unused;

    int max_attempts = env_int_or_default("MOTHERVR_BACKEND_POLL_ITERATIONS", 3600);
    int poll_interval_us = env_int_or_default("MOTHERVR_BACKEND_POLL_INTERVAL_US", 500000);

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        usleep((useconds_t)poll_interval_us);
        pthread_mutex_lock(&g_lock);
        resolve_symbols();
        apply_runtime_interpose_if_possible();
        log_backend_symbol_snapshot("poll", true);
        pthread_mutex_unlock(&g_lock);
    }

    pthread_mutex_lock(&g_lock);
    fprintf(
        g_log_file,
        "[mothervr-avp] backend poller finished after %d iterations interval_us=%d\n",
        max_attempts,
        poll_interval_us
    );
    fflush(g_log_file);
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

__attribute__((constructor)) static void recon_constructor(void) {
    raw_log_line("[mothervr-avp] constructor entered");
    pthread_mutex_lock(&g_lock);
    resolve_symbols();
    log_backend_symbol_snapshot("constructor", false);
    apply_runtime_interpose_if_possible();
    if (!g_backend_poller_started) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, backend_probe_thread, NULL) == 0) {
            pthread_detach(thread);
            g_backend_poller_started = true;
            fprintf(
                g_log_file,
                "[mothervr-avp] backend poller started iterations=%d interval_us=%d\n",
                env_int_or_default("MOTHERVR_BACKEND_POLL_ITERATIONS", 3600),
                env_int_or_default("MOTHERVR_BACKEND_POLL_INTERVAL_US", 500000)
            );
            fflush(g_log_file);
        } else {
            fprintf(g_log_file, "[mothervr-avp] backend poller failed to start\n");
            fflush(g_log_file);
        }
    }
    pthread_mutex_unlock(&g_lock);
}

void *hooked_dlsym(void *handle, const char *symbol) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();

    const void *replacement = replacement_for_symbol_name(symbol);
    void *resolved = replacement ? (void *)(uintptr_t)replacement : (real_dlsym ? real_dlsym(handle, symbol) : NULL);

    if (should_trace_backend_symbol(symbol)) {
        fprintf(
            g_log_file,
            "[mothervr-avp] backend symbol %s resolved -> %p%s\n",
            symbol ? symbol : "<null>",
            resolved,
            replacement ? " [wrapped]" : ""
        );
    }

    if (should_log_dynamic_symbol(symbol)) {
        fprintf(
            g_log_file,
            "[mothervr-avp] dlsym(handle=%p, symbol=%s) -> %p%s\n",
            handle,
            symbol ? symbol : "<null>",
            resolved,
            replacement ? " [wrapped]" : ""
        );
        fflush(g_log_file);
    }

    pthread_mutex_unlock(&g_lock);
    return resolved;
}

static void update_indexed_binding(GLenum target, GLuint index, GLuint buffer) {
    size_t first_free = MAX_TRACKED_BUFFERS;

    for (size_t i = 0; i < MAX_TRACKED_BUFFERS; ++i) {
        if (!g_indexed_bindings[i].occupied) {
            if (first_free == MAX_TRACKED_BUFFERS) {
                first_free = i;
            }
            continue;
        }

        if (g_indexed_bindings[i].buffer == buffer && g_indexed_bindings[i].target == target) {
            g_indexed_bindings[i].index = index;
            return;
        }
    }

    if (first_free < MAX_TRACKED_BUFFERS) {
        g_indexed_bindings[first_free].occupied = true;
        g_indexed_bindings[first_free].buffer = buffer;
        g_indexed_bindings[first_free].target = target;
        g_indexed_bindings[first_free].index = index;
    }
}

static int lookup_indexed_binding(GLenum target, GLuint buffer) {
    for (size_t i = 0; i < MAX_TRACKED_BUFFERS; ++i) {
        if (!g_indexed_bindings[i].occupied) {
            continue;
        }
        if (g_indexed_bindings[i].buffer == buffer && g_indexed_bindings[i].target == target) {
            return (int)g_indexed_bindings[i].index;
        }
    }
    return -1;
}

static void maybe_dump_floats(const void *data, GLsizeiptr size) {
    if (!data || size < (GLsizeiptr)(sizeof(float) * 4) || size > (GLsizeiptr)(sizeof(float) * 32)) {
        return;
    }

    const float *values = (const float *)data;
    size_t count = (size_t)size / sizeof(float);

    fprintf(g_log_file, "  floats:");
    for (size_t i = 0; i < count; ++i) {
        fprintf(g_log_file, " %.5f", values[i]);
    }
    fprintf(g_log_file, "\n");
}

static bool ascii_char_equal_insensitive(char lhs, char rhs) {
    if (lhs >= 'A' && lhs <= 'Z') {
        lhs = (char)(lhs - 'A' + 'a');
    }
    if (rhs >= 'A' && rhs <= 'Z') {
        rhs = (char)(rhs - 'A' + 'a');
    }
    return lhs == rhs;
}

static bool contains_case_insensitive(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) {
        return false;
    }

    for (size_t i = 0; haystack[i]; ++i) {
        size_t j = 0;
        while (needle[j] && haystack[i + j] && ascii_char_equal_insensitive(haystack[i + j], needle[j])) {
            j += 1;
        }
        if (!needle[j]) {
            return true;
        }
    }
    return false;
}

static bool is_interesting_uniform_name(const char *name) {
    if (!name || !name[0]) {
        return false;
    }
    return contains_case_insensitive(name, "view") ||
           contains_case_insensitive(name, "proj") ||
           contains_case_insensitive(name, "camera") ||
           contains_case_insensitive(name, "mvp") ||
           contains_case_insensitive(name, "modelview") ||
           contains_case_insensitive(name, "model_view") ||
           contains_case_insensitive(name, "vp");
}

static UniformNameRecord *remember_uniform_name(GLuint program, GLint location, const char *name) {
    UniformNameRecord *empty_slot = NULL;
    for (size_t i = 0; i < MAX_UNIFORM_NAME_RECORDS; ++i) {
        UniformNameRecord *slot = &g_uniform_name_records[i];
        if (slot->occupied) {
            if (slot->program == program && slot->location == location) {
                if (name && name[0]) {
                    snprintf(slot->name, sizeof(slot->name), "%s", name);
                }
                return slot;
            }
            continue;
        }
        if (!empty_slot) {
            empty_slot = slot;
        }
    }

    if (!empty_slot) {
        return NULL;
    }

    empty_slot->occupied = true;
    empty_slot->program = program;
    empty_slot->location = location;
    empty_slot->name[0] = 0;
    if (name && name[0]) {
        snprintf(empty_slot->name, sizeof(empty_slot->name), "%s", name);
    }
    return empty_slot;
}

static const char *lookup_uniform_name(GLuint program, GLint location) {
    for (size_t i = 0; i < MAX_UNIFORM_NAME_RECORDS; ++i) {
        UniformNameRecord *slot = &g_uniform_name_records[i];
        if (!slot->occupied) {
            continue;
        }
        if (slot->program == program && slot->location == location) {
            return slot->name;
        }
    }
    return NULL;
}

static UniformWatchRecord *find_uniform_watch(GLuint program, GLint location, uint32_t float_count) {
    UniformWatchRecord *empty_slot = NULL;
    for (size_t i = 0; i < MAX_UNIFORM_WATCHES; ++i) {
        UniformWatchRecord *slot = &g_uniform_watches[i];
        if (slot->occupied) {
            if (slot->program == program && slot->location == location && slot->float_count == float_count) {
                return slot;
            }
            continue;
        }
        if (!empty_slot) {
            empty_slot = slot;
        }
    }

    if (!empty_slot) {
        return NULL;
    }

    empty_slot->occupied = true;
    empty_slot->program = program;
    empty_slot->location = location;
    empty_slot->float_count = float_count;
    empty_slot->last_hash = 0;
    empty_slot->observed_changes = 0;
    empty_slot->logged_changes = 0;
    memset(empty_slot->last_values, 0, sizeof(empty_slot->last_values));
    return empty_slot;
}

static bool should_emit_uniform_log(const char *name, uint32_t observed_changes) {
    if (is_interesting_uniform_name(name)) {
        return observed_changes <= 16 || (observed_changes % 4U) == 0;
    }
    return observed_changes <= 3 || (observed_changes % 32U) == 0;
}

static void log_uniform_value_deltas(const float *before, const float *after, size_t float_count) {
    fprintf(g_log_file, "  uniform_delta:");
    size_t logged = 0;
    for (size_t i = 0; i < float_count; ++i) {
        uint32_t before_bits = 0;
        uint32_t after_bits = 0;
        memcpy(&before_bits, before + i, sizeof(before_bits));
        memcpy(&after_bits, after + i, sizeof(after_bits));
        if (before_bits == after_bits) {
            continue;
        }
        fprintf(g_log_file, " [%zu]=%.5f->%.5f", i, before[i], after[i]);
        logged += 1;
        if (logged >= 12) {
            break;
        }
    }
    if (logged == 0) {
        fprintf(g_log_file, " none");
    }
    fprintf(g_log_file, "\n");
}

static void maybe_log_uniform_upload_locked(
    const char *api,
    GLuint program,
    GLint location,
    GLsizei count,
    GLboolean transpose,
    const GLfloat *value,
    size_t floats_per_item
) {
    if (!value || count <= 0 || count > 4) {
        return;
    }

    size_t float_count = (size_t)count * floats_per_item;
    if (float_count == 0 || float_count > 16) {
        return;
    }

    const char *name = lookup_uniform_name(program, location);
    if (floats_per_item != 16 && !is_interesting_uniform_name(name) && count != 4) {
        return;
    }

    UniformWatchRecord *watch = find_uniform_watch(program, location, (uint32_t)float_count);
    if (!watch) {
        return;
    }

    uint64_t hash = hash_backend_buffer_bytes((const unsigned char *)value, float_count * sizeof(GLfloat));
    if (watch->last_hash == hash) {
        return;
    }

    watch->observed_changes += 1;
    if (!should_emit_uniform_log(name, watch->observed_changes)) {
        memset(watch->last_values, 0, sizeof(watch->last_values));
        memcpy(watch->last_values, value, float_count * sizeof(GLfloat));
        watch->last_hash = hash;
        return;
    }

    watch->logged_changes += 1;
    fprintf(
        g_log_file,
        "[frame=%llu] %s program=%u location=%d",
        (unsigned long long)g_frame_counter,
        api,
        program,
        location
    );
    if (name && name[0]) {
        fprintf(g_log_file, " name=%s", name);
    }
    fprintf(
        g_log_file,
        " count=%d observed=%u logged=%u",
        count,
        watch->observed_changes,
        watch->logged_changes
    );
    if (floats_per_item == 16) {
        fprintf(g_log_file, " transpose=%u", (unsigned)transpose);
    }
    fprintf(g_log_file, " values:");
    for (size_t i = 0; i < float_count; ++i) {
        fprintf(g_log_file, " %.5f", value[i]);
    }
    fprintf(g_log_file, "\n");
    log_uniform_value_deltas(watch->last_values, value, float_count);
    memset(watch->last_values, 0, sizeof(watch->last_values));
    memcpy(watch->last_values, value, float_count * sizeof(GLfloat));
    watch->last_hash = hash;
    fflush(g_log_file);
}

static bool is_candidate_backend_buffer_size(size_t size) {
    switch (size) {
        case 0x78:
        case 0x80:
        case 0x188:
        case 0x1C0:
        case 0x1D0:
        case 0x2B8:
        case 0xAF0:
        case 0xD14:
            return true;
        default:
            return false;
    }
}

static uint64_t hash_backend_buffer_bytes(const unsigned char *bytes, size_t count) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < count; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint32_t backend_watch_log_budget(size_t size) {
    switch (size) {
        case 0xD14:
            return 192;
        case 0x188:
        case 0x1C0:
        case 0x2B8:
        case 0xAF0:
            return 32;
        case 0x78:
            return BACKEND_LOG_BUDGET_UNLIMITED;
        case 0x80:
        case 0x1D0:
            return 32;
        default:
            return 12;
    }
}

static size_t backend_window_count(size_t size) {
    size_t count = size / BACKEND_BUFFER_SAMPLE_BYTES;
    if ((size % BACKEND_BUFFER_SAMPLE_BYTES) != 0) {
        count += 1;
    }
    return count;
}

static void log_backend_buffer_float_deltas_with_label(
    const char *label,
    const unsigned char *before,
    const unsigned char *after,
    size_t byte_count
) {
    size_t logged = 0;
    fprintf(g_log_file, "  %s:", label);
    for (size_t offset = 0; offset + sizeof(float) <= byte_count; offset += sizeof(float)) {
        uint32_t before_bits = 0;
        uint32_t after_bits = 0;
        memcpy(&before_bits, before + offset, sizeof(before_bits));
        memcpy(&after_bits, after + offset, sizeof(after_bits));
        if (before_bits == after_bits) {
            continue;
        }

        float before_value = 0.0f;
        float after_value = 0.0f;
        memcpy(&before_value, before + offset, sizeof(before_value));
        memcpy(&after_value, after + offset, sizeof(after_value));
        fprintf(
            g_log_file,
            " [%zu]=%.5f->%.5f",
            offset / sizeof(float),
            before_value,
            after_value
        );
        logged += 1;
        if (logged >= 12) {
            break;
        }
    }
    if (logged == 0) {
        fprintf(g_log_file, " none");
    }
    fprintf(g_log_file, "\n");
}

static void log_backend_buffer_float_deltas(const unsigned char *before, const unsigned char *after, size_t byte_count) {
    log_backend_buffer_float_deltas_with_label("delta_floats", before, after, byte_count);
}

static void log_backend_window_sample(
    const char *label,
    size_t offset,
    const unsigned char *before,
    const unsigned char *after,
    size_t byte_count
) {
    float floats[16] = {0};
    char delta_label[64];
    size_t float_count = byte_count / sizeof(float);
    if (float_count > 16) {
        float_count = 16;
    }
    if (float_count > 0) {
        memcpy(floats, after, float_count * sizeof(float));
    }

    fprintf(g_log_file, "  %s offset=0x%zx\n", label, offset);
    snprintf(delta_label, sizeof(delta_label), "%s_delta", label);
    log_backend_buffer_float_deltas_with_label(delta_label, before, after, byte_count);
    if (float_count > 0) {
        fprintf(g_log_file, "  %s_floats offset=0x%zx:", label, offset);
        for (size_t i = 0; i < float_count; ++i) {
            fprintf(g_log_file, " %.5f", floats[i]);
        }
        fprintf(g_log_file, "\n");
    }
}

static bool uses_full_buffer_window_tracking(size_t size) {
    switch (size) {
        case 0x78:
        case 0x188:
        case 0x1C0:
        case 0x2B8:
        case 0xD14:
        case 0xAF0:
            return true;
        default:
            return false;
    }
}

static bool should_emit_windowed_watch_log(
    size_t size,
    uint32_t observed_changes,
    size_t total_changed_windows,
    size_t sampled_window_offset
) {
    if (observed_changes <= 4) {
        return true;
    }
    if (total_changed_windows > 1 || sampled_window_offset > 0) {
        return true;
    }
    uint32_t interval = (size == 0xD14) ? 8U : 4U;
    return (observed_changes % interval) == 0;
}

static BackendWatchedBuffer *find_backend_watched_buffer(size_t size, uintptr_t buffer_handle) {
    BackendWatchedBuffer *empty_slot = NULL;
    for (size_t i = 0; i < MAX_BACKEND_WATCHED_BUFFERS; ++i) {
        BackendWatchedBuffer *slot = &g_backend_watched_buffers[i];
        if (slot->occupied) {
            if (slot->size == size && slot->buffer_handle == buffer_handle) {
                return slot;
            }
            continue;
        }
        if (!empty_slot) {
            empty_slot = slot;
        }
    }

    if (!empty_slot) {
        return NULL;
    }

    empty_slot->occupied = true;
    empty_slot->size = size;
    empty_slot->buffer_handle = buffer_handle;
    empty_slot->last_hash = 0;
    empty_slot->observed_changes = 0;
    empty_slot->logged_changes = 0;
    empty_slot->remaining_logs = backend_watch_log_budget(size);
    memset(empty_slot->last_bytes, 0, sizeof(empty_slot->last_bytes));
    memset(empty_slot->last_full_bytes, 0, sizeof(empty_slot->last_full_bytes));
    return empty_slot;
}

static RenderUniformBlockWatch *find_render_uniform_block_watch(
    uintptr_t identity_a,
    uintptr_t identity_b,
    size_t byte_count
) {
    RenderUniformBlockWatch *empty_slot = NULL;
    for (size_t i = 0; i < MAX_RENDER_UNIFORM_BLOCK_WATCHES; ++i) {
        RenderUniformBlockWatch *slot = &g_render_uniform_block_watches[i];
        if (slot->occupied) {
            if (slot->identity_a == identity_a &&
                slot->identity_b == identity_b &&
                slot->byte_count == byte_count) {
                return slot;
            }
            continue;
        }
        if (!empty_slot) {
            empty_slot = slot;
        }
    }

    if (!empty_slot) {
        return NULL;
    }

    empty_slot->occupied = true;
    empty_slot->identity_a = identity_a;
    empty_slot->identity_b = identity_b;
    empty_slot->byte_count = byte_count;
    empty_slot->last_hash = 0;
    empty_slot->observed_changes = 0;
    empty_slot->logged_changes = 0;
    memset(empty_slot->last_bytes, 0, sizeof(empty_slot->last_bytes));
    return empty_slot;
}

static void maybe_log_render_program_uniforms_sample(uint64_t hit_count, uintptr_t ctx_ptr) {
    if (!ctx_ptr) {
        return;
    }

    const uint8_t *ctx = (const uint8_t *)ctx_ptr;
    uintptr_t pipeline_state_ptr = 0;
    uintptr_t shared_state_ptr = 0;
    uintptr_t current_program_ptr = 0;
    uintptr_t fallback_program_ptr = 0;
    uintptr_t staging_ptr = 0;
    uint32_t staging_capacity = 0;
    uint32_t render_state = 0;
    memcpy(&pipeline_state_ptr, ctx + 0x8f40, sizeof(pipeline_state_ptr));
    memcpy(&shared_state_ptr, ctx + 0x88, sizeof(shared_state_ptr));
    memcpy(&current_program_ptr, ctx + 0x8440, sizeof(current_program_ptr));
    memcpy(&fallback_program_ptr, ctx + 0x8418, sizeof(fallback_program_ptr));
    memcpy(&staging_ptr, ctx + 0x8f30, sizeof(staging_ptr));
    memcpy(&staging_capacity, ctx + 0x8f38, sizeof(staging_capacity));
    memcpy(&render_state, ctx + 0x8edc, sizeof(render_state));

    uint32_t program_slot = UINT32_MAX;
    if (pipeline_state_ptr) {
        memcpy(&program_slot, (const uint8_t *)pipeline_state_ptr + 0x508, sizeof(program_slot));
    }

    uint32_t base_slots = 0;
    uint32_t uniform_words = 0;
    uintptr_t layout_ptr = 0;
    uintptr_t source_ptr = 0;
    uintptr_t data_ptr = staging_ptr;
    size_t byte_count = 0;
    bool fallback_path = false;

    if (current_program_ptr) {
        uintptr_t program_info_ptr = 0;
        memcpy(&program_info_ptr, (const uint8_t *)current_program_ptr + 0x18, sizeof(program_info_ptr));
        if (program_info_ptr) {
            memcpy(&base_slots, (const uint8_t *)program_info_ptr + 0x28, sizeof(base_slots));
            memcpy(&layout_ptr, (const uint8_t *)program_info_ptr + 0x20, sizeof(layout_ptr));
        }
        if (shared_state_ptr) {
            memcpy(&uniform_words, (const uint8_t *)shared_state_ptr + 0x488c, sizeof(uniform_words));
            memcpy(&source_ptr, (const uint8_t *)shared_state_ptr + 0x48a0, sizeof(source_ptr));
            byte_count = ((((size_t)uniform_words + 3U) >> 2) + (size_t)base_slots) << 4;
        }
    } else if (fallback_program_ptr && shared_state_ptr) {
        fallback_path = true;
        memcpy(&base_slots, (const uint8_t *)shared_state_ptr + 0x492c, sizeof(base_slots));
        memcpy(&layout_ptr, (const uint8_t *)shared_state_ptr + 0x4930, sizeof(layout_ptr));
        byte_count = ((size_t)base_slots) << 4;
    }

    if (byte_count == 0 && staging_capacity > 0) {
        byte_count = staging_capacity;
    }
    if (byte_count > staging_capacity && staging_capacity > 0) {
        byte_count = staging_capacity;
    }

    if (!data_ptr || byte_count == 0) {
        if (!should_log_render_program_uniforms_hit(hit_count)) {
            return;
        }
        pthread_mutex_lock(&g_lock);
        fprintf(
            g_log_file,
            "[mothervr-avp] render_prog_uniforms hit=%llu ctx=%p program_slot=%u state=0x%x fallback=%u base=%u words=%u bytes=%zu staging_cap=%u layout=%p source=%p data=%p\n",
            (unsigned long long)hit_count,
            (void *)ctx_ptr,
            program_slot,
            render_state,
            fallback_path ? 1U : 0U,
            base_slots,
            uniform_words,
            byte_count,
            staging_capacity,
            (void *)layout_ptr,
            (void *)source_ptr,
            (void *)data_ptr
        );
        fflush(g_log_file);
        pthread_mutex_unlock(&g_lock);
        return;
    }

    size_t sample_byte_count = byte_count;
    if (sample_byte_count > RENDER_UNIFORM_BLOCK_SAMPLE_BYTES) {
        sample_byte_count = RENDER_UNIFORM_BLOCK_SAMPLE_BYTES;
    }
    unsigned char sample_bytes[RENDER_UNIFORM_BLOCK_SAMPLE_BYTES] = {0};
    memcpy(sample_bytes, (const void *)data_ptr, sample_byte_count);
    size_t float_count = sample_byte_count / sizeof(float);
    if (float_count > 16) {
        float_count = 16;
    }
    float floats[16] = {0};
    if (float_count > 0) {
        memcpy(floats, (const void *)data_ptr, float_count * sizeof(float));
    }
    uint64_t hash = hash_backend_buffer_bytes(sample_bytes, sample_byte_count);
    float pose_rows[3][3] = {{0}};
    float pose_translation[3] = {0};
    size_t pose_base_index = 0;
    size_t pose_row_stride = 0;
    bool has_pose_candidate =
        extract_render_program_pose_candidate(
            sample_bytes,
            sample_byte_count,
            g_have_last_look78_eye,
            g_last_look78_eye,
            pose_rows,
            pose_translation,
            &pose_base_index,
            &pose_row_stride
        );
    float pose_eye_delta = 0.0f;
    float pose_eye_offset[3] = {0.0f, 0.0f, 0.0f};
    bool force_emit_pose_log = false;
    if (has_pose_candidate && g_have_last_look78_eye) {
        pose_eye_delta = distance3f(pose_translation, g_last_look78_eye);
        pose_eye_offset[0] = pose_translation[0] - g_last_look78_eye[0];
        pose_eye_offset[1] = pose_translation[1] - g_last_look78_eye[1];
        pose_eye_offset[2] = pose_translation[2] - g_last_look78_eye[2];
        force_emit_pose_log = pose_eye_delta <= 0.75f;
    }

    pthread_mutex_lock(&g_lock);
    uintptr_t watch_identity_a = layout_ptr ? layout_ptr : data_ptr;
    uintptr_t watch_identity_b = (((uintptr_t)base_slots) << 32) ^ (uintptr_t)byte_count;
    RenderUniformBlockWatch *watch =
        find_render_uniform_block_watch(watch_identity_a, watch_identity_b, byte_count);
    if (!watch) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (watch->last_hash == hash) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (watch->last_hash != 0 &&
        !has_meaningful_render_uniform_change(watch->last_bytes, sample_bytes, sample_byte_count, 0.005f)) {
        memcpy(watch->last_bytes, sample_bytes, sample_byte_count);
        watch->last_hash = hash;
        pthread_mutex_unlock(&g_lock);
        return;
    }

    watch->observed_changes += 1;
    if (!should_emit_render_uniform_block_log(watch->observed_changes, byte_count) && !force_emit_pose_log) {
        memcpy(watch->last_bytes, sample_bytes, sample_byte_count);
        watch->last_hash = hash;
        pthread_mutex_unlock(&g_lock);
        return;
    }

    watch->logged_changes += 1;
    fprintf(
        g_log_file,
        "[mothervr-avp] render_prog_uniforms observed=%u logged=%u hit=%llu ctx=%p program_slot=%u state=0x%x fallback=%u base=%u words=%u bytes=%zu sampled=%zu staging_cap=%u layout=%p source=%p data=%p\n",
        watch->observed_changes,
        watch->logged_changes,
        (unsigned long long)hit_count,
        (void *)ctx_ptr,
        program_slot,
        render_state,
        fallback_path ? 1U : 0U,
        base_slots,
        uniform_words,
        byte_count,
        sample_byte_count,
        staging_capacity,
        (void *)layout_ptr,
        (void *)source_ptr,
        (void *)data_ptr
    );
    if (float_count > 0) {
        fprintf(g_log_file, "  render_prog_floats:");
        for (size_t i = 0; i < float_count; ++i) {
            fprintf(g_log_file, " %.5f", floats[i]);
        }
        fprintf(g_log_file, "\n");
    }
    if (has_pose_candidate) {
        bool emit_pose_log = !g_have_last_look78_eye;
        if (g_have_last_look78_eye) {
            emit_pose_log = force_emit_pose_log;
        }
        if (emit_pose_log) {
            fprintf(
                g_log_file,
                "  render_prog_pose: base_index=%zu row_stride=%zu r0=(%.5f %.5f %.5f) r1=(%.5f %.5f %.5f) r2=(%.5f %.5f %.5f) t=(%.5f %.5f %.5f)",
                pose_base_index,
                pose_row_stride,
                pose_rows[0][0], pose_rows[0][1], pose_rows[0][2],
                pose_rows[1][0], pose_rows[1][1], pose_rows[1][2],
                pose_rows[2][0], pose_rows[2][1], pose_rows[2][2],
                pose_translation[0], pose_translation[1], pose_translation[2]
            );
            if (g_have_last_look78_eye) {
                fprintf(
                    g_log_file,
                    " eye_delta=%.5f eye=(%.5f %.5f %.5f) eye_offset=(%.5f %.5f %.5f)",
                    pose_eye_delta,
                    g_last_look78_eye[0],
                    g_last_look78_eye[1],
                    g_last_look78_eye[2],
                    pose_eye_offset[0],
                    pose_eye_offset[1],
                    pose_eye_offset[2]
                );
            }
            fprintf(g_log_file, "\n");
        }
    }
    log_backend_buffer_float_deltas_with_label("render_prog_delta", watch->last_bytes, sample_bytes, sample_byte_count);
    memcpy(watch->last_bytes, sample_bytes, sample_byte_count);
    watch->last_hash = hash;
    fflush(g_log_file);
    pthread_mutex_unlock(&g_lock);
}

static void maybe_log_backend_buffer_sample(uint64_t hit_count, uintptr_t buffer_handle, uintptr_t size_raw, uintptr_t data_ptr) {
    size_t size = (size_t)size_raw;
    if (!is_candidate_backend_buffer_size(size) || !data_ptr || size == 0) {
        return;
    }

    unsigned char bytes[BACKEND_BUFFER_SAMPLE_BYTES] = {0};
    float floats[BACKEND_FLOAT_LOG_LIMIT] = {0};
    size_t byte_count = size < BACKEND_BUFFER_SAMPLE_BYTES ? size : BACKEND_BUFFER_SAMPLE_BYTES;
    size_t float_count = size / sizeof(float);
    if (float_count > BACKEND_FLOAT_LOG_LIMIT) {
        float_count = BACKEND_FLOAT_LOG_LIMIT;
    }
    size_t summary_float_count = float_count;
    if (summary_float_count > 16 && size != 0x78) {
        summary_float_count = 16;
    }

    const void *data = (const void *)data_ptr;
    const unsigned char *data_bytes = (const unsigned char *)data;
    memcpy(bytes, data_bytes, byte_count);
    if (float_count > 0) {
        memcpy(floats, data, float_count * sizeof(float));
    }
    uint64_t hash = hash_backend_buffer_bytes(bytes, byte_count);

    uint32_t observed_index = 0;
    uint32_t change_index = 0;
    bool has_relevant_change = false;
    size_t changed_window_offsets[BACKEND_WINDOW_LOG_LIMIT] = {0};
    size_t total_changed_windows = 0;
    size_t sampled_window_offset = 0;
    size_t sampled_window_byte_count = 0;
    unsigned char sampled_window_before[BACKEND_BUFFER_SAMPLE_BYTES] = {0};
    unsigned char sampled_window_after[BACKEND_BUFFER_SAMPLE_BYTES] = {0};
    pthread_mutex_lock(&g_lock);
    BackendWatchedBuffer *slot = find_backend_watched_buffer(size, buffer_handle);
    if (!slot || slot->remaining_logs == 0) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (uses_full_buffer_window_tracking(size)) {
        size_t window_count = backend_window_count(size);
        for (size_t window_index = 0; window_index < window_count; ++window_index) {
            size_t window_offset = window_index * BACKEND_BUFFER_SAMPLE_BYTES;
            size_t window_byte_count = size - window_offset;
            if (window_byte_count > BACKEND_BUFFER_SAMPLE_BYTES) {
                window_byte_count = BACKEND_BUFFER_SAMPLE_BYTES;
            }

            uint64_t before_hash =
                hash_backend_buffer_bytes(slot->last_full_bytes + window_offset, window_byte_count);
            uint64_t after_hash = hash_backend_buffer_bytes(data_bytes + window_offset, window_byte_count);
            if (before_hash == after_hash) {
                continue;
            }

            has_relevant_change = true;
            if (total_changed_windows < BACKEND_WINDOW_LOG_LIMIT) {
                changed_window_offsets[total_changed_windows] = window_offset;
            }
            total_changed_windows += 1;

            if (sampled_window_byte_count == 0 || (sampled_window_offset == 0 && window_offset > 0)) {
                sampled_window_offset = window_offset;
                sampled_window_byte_count = window_byte_count;
                memcpy(sampled_window_before, slot->last_full_bytes + window_offset, window_byte_count);
                memcpy(sampled_window_after, data_bytes + window_offset, window_byte_count);
            }
        }
    } else {
        has_relevant_change = slot->last_hash != hash;
    }
    if (!has_relevant_change) {
        pthread_mutex_unlock(&g_lock);
        return;
    }

    slot->observed_changes += 1;
    observed_index = slot->observed_changes;

    bool emit_log = true;
    if (uses_full_buffer_window_tracking(size)) {
        emit_log = should_emit_windowed_watch_log(size, observed_index, total_changed_windows, sampled_window_offset);
    }
    if (!emit_log) {
        memset(slot->last_bytes, 0, sizeof(slot->last_bytes));
        memcpy(slot->last_bytes, bytes, byte_count);
        memset(slot->last_full_bytes, 0, sizeof(slot->last_full_bytes));
        memcpy(slot->last_full_bytes, data_bytes, size);
        slot->last_hash = hash;
        pthread_mutex_unlock(&g_lock);
        return;
    }

    slot->logged_changes += 1;
    if (slot->remaining_logs != BACKEND_LOG_BUDGET_UNLIMITED) {
        slot->remaining_logs -= 1;
    }
    change_index = slot->logged_changes;

    if (uses_full_buffer_window_tracking(size)) {
        if (size == 0x78 && float_count >= 13) {
            g_last_look78_eye[0] = floats[0];
            g_last_look78_eye[1] = floats[1];
            g_last_look78_eye[2] = floats[2];
            g_have_last_look78_eye = true;
            float forward_x = floats[10] - floats[0];
            float forward_y = floats[11] - floats[1];
            float forward_z = floats[12] - floats[2];
            float forward_len = sqrtf(
                forward_x * forward_x +
                forward_y * forward_y +
                forward_z * forward_z
            );
            float forward_norm_x = 0.0f;
            float forward_norm_y = 0.0f;
            float forward_norm_z = 0.0f;
            float yaw_deg = 0.0f;
            float pitch_deg = 0.0f;
            if (forward_len > 0.00001f) {
                forward_norm_x = forward_x / forward_len;
                forward_norm_y = forward_y / forward_len;
                forward_norm_z = forward_z / forward_len;
                float clamped_forward_y = fmaxf(-1.0f, fminf(1.0f, forward_norm_y));
                yaw_deg = atan2f(forward_norm_x, forward_norm_z) * 57.2957795f;
                pitch_deg = asinf(clamped_forward_y) * 57.2957795f;
            }
            float rel_x = 0.0f;
            float rel_y = 0.0f;
            float rel_z = 0.0f;
            float rel_len = 0.0f;
            float rel_norm_x = 0.0f;
            float rel_norm_y = 0.0f;
            float rel_norm_z = 0.0f;
            float rel_yaw_deg = 0.0f;
            float rel_pitch_deg = 0.0f;
            bool have_rel_direction = false;
            float camera_proxy_yaw_deg = 0.0f;
            float camera_proxy_pitch_deg = 0.0f;
            float camera_proxy_unwrapped_yaw_deg = 0.0f;
            bool have_camera_proxy = false;
            if (g_have_last_d14_pos) {
                rel_x = floats[0] - g_last_d14_pos[0];
                rel_y = floats[1] - g_last_d14_pos[1];
                rel_z = floats[2] - g_last_d14_pos[2];
                rel_len = sqrtf(rel_x * rel_x + rel_y * rel_y + rel_z * rel_z);
                if (rel_len > 0.00001f) {
                    rel_norm_x = rel_x / rel_len;
                    rel_norm_y = rel_y / rel_len;
                    rel_norm_z = rel_z / rel_len;
                    float clamped_rel_y = fmaxf(-1.0f, fminf(1.0f, rel_norm_y));
                    rel_yaw_deg = atan2f(rel_norm_x, rel_norm_z) * 57.2957795f;
                    rel_pitch_deg = asinf(clamped_rel_y) * 57.2957795f;
                    have_rel_direction = true;
                    if (!g_have_camera_proxy_last_yaw) {
                        g_have_camera_proxy_last_yaw = true;
                        g_camera_proxy_last_wrapped_yaw_deg = rel_yaw_deg;
                        g_camera_proxy_unwrapped_yaw_deg = rel_yaw_deg;
                    } else {
                        g_camera_proxy_unwrapped_yaw_deg +=
                            shortest_angle_delta_degrees(g_camera_proxy_last_wrapped_yaw_deg, rel_yaw_deg);
                        g_camera_proxy_last_wrapped_yaw_deg = rel_yaw_deg;
                    }
                    if (!g_have_camera_proxy_baseline) {
                        g_have_camera_proxy_baseline = true;
                        g_camera_proxy_baseline_yaw_deg = g_camera_proxy_unwrapped_yaw_deg;
                        g_camera_proxy_baseline_pitch_deg = rel_pitch_deg;
                    }
                    camera_proxy_unwrapped_yaw_deg = g_camera_proxy_unwrapped_yaw_deg;
                    camera_proxy_yaw_deg =
                        camera_proxy_unwrapped_yaw_deg - g_camera_proxy_baseline_yaw_deg;
                    camera_proxy_pitch_deg = rel_pitch_deg - g_camera_proxy_baseline_pitch_deg;
                    have_camera_proxy = true;
                }
            }
            fprintf(
                g_log_file,
                "[mothervr-avp] look78 observed=%u logged=%u hit=%llu eye=(%.5f, %.5f, %.5f)",
                observed_index,
                change_index,
                (unsigned long long)hit_count,
                floats[0],
                floats[1],
                floats[2]
            );
            if (g_have_last_d14_pos) {
                fprintf(
                    g_log_file,
                    " rel_d14=(%.5f, %.5f, %.5f) rel_len=%.5f",
                    rel_x,
                    rel_y,
                    rel_z,
                    rel_len
                );
                if (have_rel_direction) {
                    fprintf(
                        g_log_file,
                        " rel_dir=(%.5f, %.5f, %.5f) rel_yaw_pitch=(%.2f, %.2f)",
                        rel_norm_x,
                        rel_norm_y,
                        rel_norm_z,
                        rel_yaw_deg,
                        rel_pitch_deg
                    );
                    if (have_camera_proxy) {
                        fprintf(
                            g_log_file,
                            " camera_proxy=(%.2f, %.2f) rel_unwrapped_yaw=%.2f",
                            camera_proxy_yaw_deg,
                            camera_proxy_pitch_deg,
                            camera_proxy_unwrapped_yaw_deg
                        );
                    }
                }
            }
            fprintf(
                g_log_file,
                " forward=(%.5f, %.5f, %.5f) fwd_len=%.5f yaw_pitch=(%.2f, %.2f) tail=(%.5f, %.5f, %.5f) scalars=(%.5f, %.5f, %.5f, %.5f)\n",
                forward_norm_x,
                forward_norm_y,
                forward_norm_z,
                forward_len,
                yaw_deg,
                pitch_deg,
                floats[10],
                floats[11],
                floats[12],
                floats[3],
                floats[4],
                floats[8],
                floats[9]
            );
            fprintf(g_log_file, "  look78_floats:");
            for (size_t i = 0; i < float_count; ++i) {
                fprintf(g_log_file, " %.5f", floats[i]);
            }
            fprintf(g_log_file, "\n");
            log_backend_buffer_float_deltas_with_label("look78_delta", slot->last_full_bytes, data_bytes, size);
        } else if (size == 0xD14 && float_count >= 11) {
            g_last_d14_pos[0] = floats[0];
            g_last_d14_pos[1] = floats[1];
            g_last_d14_pos[2] = floats[2];
            g_have_last_d14_pos = true;
            fprintf(
                g_log_file,
                "[mothervr-avp] d14_pose observed=%u logged=%u hit=%llu pos=(%.5f, %.5f, %.5f) lanes=(%.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f)\n",
                observed_index,
                change_index,
                (unsigned long long)hit_count,
                floats[0],
                floats[1],
                floats[2],
                floats[3],
                floats[4],
                floats[5],
                floats[6],
                floats[7],
                floats[8],
                floats[9],
                floats[10]
            );
        } else {
            fprintf(
                g_log_file,
                "[mothervr-avp] windowed_watch size=0x%zx observed=%u logged=%u hit=%llu head:",
                size,
                observed_index,
                change_index,
                (unsigned long long)hit_count
            );
            for (size_t i = 0; i < summary_float_count; ++i) {
                fprintf(g_log_file, " %.5f", floats[i]);
            }
            fprintf(g_log_file, "\n");
        }
        log_backend_buffer_float_deltas(slot->last_bytes, bytes, byte_count);
        if (total_changed_windows > 1 || sampled_window_offset > 0) {
            fprintf(g_log_file, "  windows_changed(size=0x%zx):", size);
            size_t logged_window_count = total_changed_windows;
            if (logged_window_count > BACKEND_WINDOW_LOG_LIMIT) {
                logged_window_count = BACKEND_WINDOW_LOG_LIMIT;
            }
            for (size_t i = 0; i < logged_window_count; ++i) {
                fprintf(g_log_file, " 0x%03zx", changed_window_offsets[i]);
            }
            if (total_changed_windows > logged_window_count) {
                fprintf(g_log_file, " ...(+%zu more)", total_changed_windows - logged_window_count);
            }
            fprintf(g_log_file, "\n");
        }
        if (sampled_window_byte_count > 0 && sampled_window_offset > 0) {
            log_backend_window_sample(
                size == 0xD14 ? "d14_window" : "window_sample",
                sampled_window_offset,
                sampled_window_before,
                sampled_window_after,
                sampled_window_byte_count
            );
        }
    } else {
        fprintf(
            g_log_file,
            "[mothervr-avp] gldBufferSubData watch size=0x%zx change=%u hit=%llu buffer=%p data=%p hash=0x%llx bytes=%zu\n",
            size,
            change_index,
            (unsigned long long)hit_count,
            (void *)buffer_handle,
            data,
            (unsigned long long)hash,
            byte_count
        );
        fprintf(g_log_file, "  hex:");
        for (size_t i = 0; i < byte_count; ++i) {
            fprintf(g_log_file, " %02x", bytes[i]);
        }
        fprintf(g_log_file, "\n");
        log_backend_buffer_float_deltas(slot->last_bytes, bytes, byte_count);
        if (float_count > 0) {
            fprintf(g_log_file, "  floats:");
            for (size_t i = 0; i < float_count; ++i) {
                fprintf(g_log_file, " %.5f", floats[i]);
            }
            fprintf(g_log_file, "\n");
        }
    }

    memset(slot->last_bytes, 0, sizeof(slot->last_bytes));
    memcpy(slot->last_bytes, bytes, byte_count);
    memset(slot->last_full_bytes, 0, sizeof(slot->last_full_bytes));
    memcpy(slot->last_full_bytes, data_bytes, size);
    slot->last_hash = hash;
    fflush(g_log_file);
    pthread_mutex_unlock(&g_lock);
}

uintptr_t hooked_gldUpdateDispatch(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5) {
    uint64_t hit_count = __sync_add_and_fetch(&g_gldUpdateDispatch_hits, 1);
    if (should_log_backend_hit("gldUpdateDispatch", hit_count)) {
        log_backend_hit("gldUpdateDispatch", hit_count, a0, a1, a2, a3, a4, a5);
    }

    backend_probe6_fn real = real_gldUpdateDispatch;
    if (!real) {
        void *resolved = resolve_visible_symbol("gldUpdateDispatch");
        if (!resolved || resolved == (const void *)(uintptr_t)&hooked_gldUpdateDispatch) {
            return 0;
        }
        real = (backend_probe6_fn)resolved;
        real_gldUpdateDispatch = real;
    }

    uintptr_t result = real(a0, a1, a2, a3, a4, a5);
    if (a1 && (hit_count <= 64 || (hit_count % 1024ULL) == 0)) {
        maybe_patch_dispatch_table(a1, hit_count);
    }
    return result;
}

uintptr_t hooked_gldPresentFramebufferData(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5) {
    return dispatch_backend_probe(
        "gldPresentFramebufferData",
        &real_gldPresentFramebufferData,
        (const void *)(uintptr_t)&hooked_gldPresentFramebufferData,
        &g_gldPresentFramebufferData_hits,
        a0,
        a1,
        a2,
        a3,
        a4,
        a5
    );
}

uintptr_t hooked_gldBufferSubData(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5) {
    uint64_t hit_count = __sync_add_and_fetch(&g_gldBufferSubData_hits, 1);
    if (should_log_backend_hit("gldBufferSubData", hit_count)) {
        log_backend_hit("gldBufferSubData", hit_count, a0, a1, a2, a3, a4, a5);
    }
    maybe_log_backend_buffer_sample(hit_count, a1, a3, a4);

    backend_probe6_fn real = real_gldBufferSubData;
    if (!real) {
        void *resolved = resolve_visible_symbol("gldBufferSubData");
        if (!resolved || resolved == (const void *)(uintptr_t)&hooked_gldBufferSubData) {
            return 0;
        }
        real = (backend_probe6_fn)resolved;
        real_gldBufferSubData = real;
    }

    return real(a0, a1, a2, a3, a4, a5);
}

uintptr_t hooked_gldFlushContext(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5) {
    return dispatch_backend_probe(
        "gldFlushContext",
        &real_gldFlushContext,
        (const void *)(uintptr_t)&hooked_gldFlushContext,
        &g_gldFlushContext_hits,
        a0,
        a1,
        a2,
        a3,
        a4,
        a5
    );
}

uintptr_t hooked_gliAttachDrawable(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5) {
    return dispatch_backend_probe(
        "gliAttachDrawable",
        &real_gliAttachDrawable,
        (const void *)(uintptr_t)&hooked_gliAttachDrawable,
        &g_gliAttachDrawable_hits,
        a0,
        a1,
        a2,
        a3,
        a4,
        a5
    );
}

uintptr_t hooked_ctx_updateUniformBindings(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5) {
    uint64_t hit_count = __sync_add_and_fetch(&g_ctx_updateUniformBindings_hits, 1);

    backend_probe6_fn real = real_ctx_updateUniformBindings;
    if (!real) {
        return 0;
    }

    uintptr_t result = real(a0, a1, a2, a3, a4, a5);
    maybe_log_render_uniform_buffers_sample(hit_count, a0);
    return result;
}

uintptr_t hooked_ctx_setRenderUniformBuffers(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5) {
    uint64_t hit_count = __sync_add_and_fetch(&g_ctx_setRenderUniformBuffers_hits, 1);
    (void)hit_count;

    backend_probe6_fn real = real_ctx_setRenderUniformBuffers;
    if (!real) {
        return 0;
    }
    return real(a0, a1, a2, a3, a4, a5);
}

uintptr_t hooked_ctx_setRenderProgramUniforms(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5) {
    uint64_t hit_count = __sync_add_and_fetch(&g_ctx_setRenderProgramUniforms_hits, 1);

    backend_probe6_fn real = real_ctx_setRenderProgramUniforms;
    if (!real) {
        return 0;
    }

    uintptr_t result = real(a0, a1, a2, a3, a4, a5);
    maybe_log_render_program_uniforms_sample(hit_count, a0);
    return result;
}

void hooked_glUseProgram(GLuint program) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();
    g_current_program = program;
    pthread_mutex_unlock(&g_lock);
    real_glUseProgram(program);
}

GLint hooked_glGetUniformLocation(GLuint program, const GLchar *name) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();
    pthread_mutex_unlock(&g_lock);

    GLint location = real_glGetUniformLocation(program, name);

    pthread_mutex_lock(&g_lock);
    if (location >= 0) {
        remember_uniform_name(program, location, name ? (const char *)name : NULL);
        fprintf(
            g_log_file,
            "[frame=%llu] glGetUniformLocation program=%u location=%d name=%s\n",
            (unsigned long long)g_frame_counter,
            program,
            location,
            name ? (const char *)name : "<null>"
        );
        fflush(g_log_file);
    }
    pthread_mutex_unlock(&g_lock);
    return location;
}

void hooked_glBindBuffer(GLenum target, GLuint buffer) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();

    if (target == GL_UNIFORM_BUFFER) {
        g_current_uniform_buffer = buffer;
    } else if (target == GL_ARRAY_BUFFER) {
        g_current_array_buffer = buffer;
    }

    pthread_mutex_unlock(&g_lock);
    real_glBindBuffer(target, buffer);
}

void hooked_glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();
    update_indexed_binding(target, index, buffer);
    fprintf(
        g_log_file,
        "[frame=%llu] glBindBufferBase target=%s(0x%x) index=%u buffer=%u\n",
        (unsigned long long)g_frame_counter,
        target_name(target),
        target,
        index,
        buffer
    );
    fflush(g_log_file);
    pthread_mutex_unlock(&g_lock);

    real_glBindBufferBase(target, index, buffer);
}

void hooked_glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();
    update_indexed_binding(target, index, buffer);
    fprintf(
        g_log_file,
        "[frame=%llu] glBindBufferRange target=%s(0x%x) index=%u buffer=%u offset=%lld size=%lld\n",
        (unsigned long long)g_frame_counter,
        target_name(target),
        target,
        index,
        buffer,
        (long long)offset,
        (long long)size
    );
    fflush(g_log_file);
    pthread_mutex_unlock(&g_lock);

    real_glBindBufferRange(target, index, buffer, offset, size);
}

void hooked_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();

    GLuint buffer = 0;
    if (target == GL_UNIFORM_BUFFER) {
        buffer = g_current_uniform_buffer;
    } else if (target == GL_ARRAY_BUFFER) {
        buffer = g_current_array_buffer;
    }

    int indexed_binding = lookup_indexed_binding(target, buffer);
    if (target == GL_UNIFORM_BUFFER || (size >= 64 && size <= 256)) {
        fprintf(
            g_log_file,
            "[frame=%llu] glBufferSubData target=%s(0x%x) buffer=%u binding=%d offset=%lld size=%lld\n",
            (unsigned long long)g_frame_counter,
            target_name(target),
            target,
            buffer,
            indexed_binding,
            (long long)offset,
            (long long)size
        );
        maybe_dump_floats(data, size);
        fflush(g_log_file);
    }

    pthread_mutex_unlock(&g_lock);
    real_glBufferSubData(target, offset, size, data);
}

void *hooked_glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();

    GLuint buffer = target == GL_UNIFORM_BUFFER ? g_current_uniform_buffer : 0;
    int indexed_binding = lookup_indexed_binding(target, buffer);
    if (target == GL_UNIFORM_BUFFER) {
        fprintf(
            g_log_file,
            "[frame=%llu] glMapBufferRange target=%s(0x%x) buffer=%u binding=%d offset=%lld length=%lld access=0x%x\n",
            (unsigned long long)g_frame_counter,
            target_name(target),
            target,
            buffer,
            indexed_binding,
            (long long)offset,
            (long long)length,
            access
        );
        fflush(g_log_file);
    }

    pthread_mutex_unlock(&g_lock);
    return real_glMapBufferRange(target, offset, length, access);
}

void hooked_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();
    maybe_log_uniform_upload_locked("glUniformMatrix4fv", g_current_program, location, count, transpose, value, 16);
    pthread_mutex_unlock(&g_lock);
    real_glUniformMatrix4fv(location, count, transpose, value);
}

void hooked_glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();
    maybe_log_uniform_upload_locked("glProgramUniformMatrix4fv", program, location, count, transpose, value, 16);
    pthread_mutex_unlock(&g_lock);
    real_glProgramUniformMatrix4fv(program, location, count, transpose, value);
}

void hooked_glUniform4fv(GLint location, GLsizei count, const GLfloat *value) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();
    maybe_log_uniform_upload_locked("glUniform4fv", g_current_program, location, count, GL_FALSE, value, 4);
    pthread_mutex_unlock(&g_lock);
    real_glUniform4fv(location, count, value);
}

void hooked_glProgramUniform4fv(GLuint program, GLint location, GLsizei count, const GLfloat *value) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();
    maybe_log_uniform_upload_locked("glProgramUniform4fv", program, location, count, GL_FALSE, value, 4);
    pthread_mutex_unlock(&g_lock);
    real_glProgramUniform4fv(program, location, count, value);
}

CGLError hooked_CGLFlushDrawable(CGLContextObj context) {
    pthread_mutex_lock(&g_lock);
    resolve_symbols();
    g_frame_counter += 1;
    if ((g_frame_counter % 300) == 1) {
        fprintf(g_log_file, "[frame=%llu] CGLFlushDrawable\n", (unsigned long long)g_frame_counter);
        fflush(g_log_file);
    }
    pthread_mutex_unlock(&g_lock);

    return real_CGLFlushDrawable(context);
}

INTERPOSE(hooked_glUseProgram, glUseProgram);
INTERPOSE(hooked_glGetUniformLocation, glGetUniformLocation);
INTERPOSE(hooked_glBindBuffer, glBindBuffer);
INTERPOSE(hooked_glBindBufferBase, glBindBufferBase);
INTERPOSE(hooked_glBindBufferRange, glBindBufferRange);
INTERPOSE(hooked_glBufferSubData, glBufferSubData);
INTERPOSE(hooked_glMapBufferRange, glMapBufferRange);
INTERPOSE(hooked_glUniformMatrix4fv, glUniformMatrix4fv);
INTERPOSE(hooked_glProgramUniformMatrix4fv, glProgramUniformMatrix4fv);
INTERPOSE(hooked_glUniform4fv, glUniform4fv);
INTERPOSE(hooked_glProgramUniform4fv, glProgramUniform4fv);
INTERPOSE(hooked_CGLFlushDrawable, CGLFlushDrawable);
