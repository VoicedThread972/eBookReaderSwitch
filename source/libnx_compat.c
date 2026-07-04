#include <unistd.h>

/*
 * libnx 4.12 can crash before main() while deriving cwd from argv[0] on some
 * hbmenu/Atmosphere launch paths. The app uses absolute sdmc:/ and romfs: paths,
 * so skipping chdir is harmless for this project.
 */
int __wrap_chdir(const char *path) {
    (void)path;
    return 0;
}
