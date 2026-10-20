// SPDX-License-Identifier: GPL-3.0-or-later
// Establish the private XWM capability, then exec the selected Xwayland.
// PID and credentials remain identical on the XWM and Wayland connections.
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/auxv.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

int main(int argc, char **argv) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime || !*runtime) { fputs("XDG_RUNTIME_DIR is required\n", stderr); return 1; }
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    int n = snprintf(addr.sun_path, sizeof addr.sun_path, "%s/trames-xwm", runtime);
    if (n < 0 || (size_t)n >= sizeof addr.sun_path) { fputs("Runtime path too long\n", stderr); return 1; }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (void *)&addr, sizeof addr) < 0) { perror("Connect Trames XWM"); return 1; }
    char number[24]; snprintf(number, sizeof number, "%d", fd);
    const char *native = getenv("TRAMES_XWAYLAND_NATIVE_WSI");
    // GPU is the normal route. An explicit 0 keeps the original SHM server
    // available for diagnosis or sessions where GPU acceleration is disabled.
    int gpu = !native || strcmp(native, "0");
    if (gpu && !getauxval(0x54520008UL)) gpu = 0;
    if (setenv("TRAMES_XWAYLAND_NATIVE_WSI", gpu ? "1" : "0", 1)) {
        perror("set native WSI mode"); return 1;
    }
    char **args = calloc((size_t)argc + 12, sizeof *args);
    if (!args) { perror("calloc"); return 1; }
    int i = 0;
    args[i++] = gpu ? "/usr/libexec/trames/Xwayland-native" : "Xwayland";
    args[i++] = argc > 1 ? argv[1] : ":0";
    args[i++] = "-rootless";
    if (gpu) { args[i++] = "-glamor"; args[i++] = "es"; }
    else args[i++] = "-shm";
    args[i++] = "-nolisten"; args[i++] = "tcp";
    args[i++] = "-wm"; args[i++] = number;
    for (int j=2; j<argc; ++j) args[i++] = argv[j];
    execvp(args[0], args);
    perror("exec Xwayland (apk add xwayland)");
    return 1;
}
