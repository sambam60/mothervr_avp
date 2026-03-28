#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &tm_now);
}

static void append_log_line(const char *log_path, const char *message) {
    FILE *log = fopen(log_path, "a");
    if (!log) {
        return;
    }

    char ts[32];
    timestamp(ts, sizeof(ts));
    fprintf(log, "[%s] %s\n", ts, message);
    fclose(log);
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void parent_dir(char *path) {
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
    while (len > 0 && path[len - 1] != '/') {
        path[--len] = '\0';
    }
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
}

int main(int argc, char **argv) {
    char exec_path[PATH_MAX];
    uint32_t exec_path_size = (uint32_t)sizeof(exec_path);
    if (_NSGetExecutablePath(exec_path, &exec_path_size) != 0) {
        return 111;
    }
    char resolved_exec_path[PATH_MAX];
    if (!realpath(exec_path, resolved_exec_path)) {
        return 111;
    }

    char app_dir[PATH_MAX];
    strncpy(app_dir, resolved_exec_path, sizeof(app_dir) - 1);
    app_dir[sizeof(app_dir) - 1] = '\0';
    parent_dir(app_dir);

    char game_root[PATH_MAX];
    char game_root_candidate[PATH_MAX];
    snprintf(game_root_candidate, sizeof(game_root_candidate), "%s/../../..", app_dir);
    if (!realpath(game_root_candidate, game_root)) {
        return 112;
    }

    char real_bin[PATH_MAX];
    snprintf(real_bin, sizeof(real_bin), "%s/Alien Isolation.real", app_dir);

    char recon_dylib[PATH_MAX];
    snprintf(recon_dylib, sizeof(recon_dylib), "%s/mothervr_avp/inject/build/libmothervr_avp_recon.dylib", game_root);

    char recon_log[PATH_MAX];
    const char *recon_log_env = getenv("MOTHERVR_AVP_LOG");
    if (recon_log_env && recon_log_env[0]) {
        snprintf(recon_log, sizeof(recon_log), "%s", recon_log_env);
    } else {
        snprintf(recon_log, sizeof(recon_log), "%s/mothervr_avp/inject/build/recon.log", game_root);
    }

    char wrapper_log[PATH_MAX];
    snprintf(wrapper_log, sizeof(wrapper_log), "%s/mothervr_avp/inject/build/wrapper.log", game_root);

    char start_line[PATH_MAX + 64];
    snprintf(start_line, sizeof(start_line), "wrapper invoked; exec=%s", resolved_exec_path);
    append_log_line(wrapper_log, start_line);
    append_log_line(wrapper_log, "wrapper invoked");

    if (!file_exists(real_bin)) {
        char missing_line[PATH_MAX + 64];
        snprintf(missing_line, sizeof(missing_line), "missing real binary: %s", real_bin);
        append_log_line(wrapper_log, missing_line);
        return 113;
    }

    chdir(game_root);

    if (file_exists(recon_dylib)) {
        setenv("DYLD_INSERT_LIBRARIES", recon_dylib, 1);
        setenv("MOTHERVR_AVP_LOG", recon_log, 1);
        append_log_line(wrapper_log, "launching real binary with injected dylib");
    } else {
        append_log_line(wrapper_log, "dylib missing; launching real binary without injection");
    }

    char **child_argv = calloc((size_t)argc + 1, sizeof(char *));
    if (!child_argv) {
        append_log_line(wrapper_log, "calloc failed");
        return 114;
    }

    child_argv[0] = real_bin;
    for (int i = 1; i < argc; ++i) {
        child_argv[i] = argv[i];
    }
    child_argv[argc] = NULL;

    execv(real_bin, child_argv);

    char error_line[256];
    snprintf(error_line, sizeof(error_line), "execv failed: %s", strerror(errno));
    append_log_line(wrapper_log, error_line);
    free(child_argv);
    return 115;
}
