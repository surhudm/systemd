/* SPDX-License-Identifier: LGPL-2.1+ */

#include "alloc-util.h"
#include "env-file.h"
#include "fd-util.h"
#include "fileio.h"
#include "fs-util.h"
#include "macro.h"
#include "os-util.h"
#include "string-util.h"
#include "strv.h"

int path_is_os_tree(const char *path) {
        int r;

        assert(path);

        /* Does the path exist at all? If not, generate an error immediately. This is useful so that a missing root dir
         * always results in -ENOENT, and we can properly distinguish the case where the whole root doesn't exist from
         * the case where just the os-release file is missing. */
        if (laccess(path, F_OK) < 0)
                return -errno;

        /* We use {/etc|/usr/lib}/os-release as flag file if something is an OS */
        r = open_os_release(path, NULL, NULL);
        if (r == -ENOENT) /* We got nothing */
                return 0;
        if (r < 0)
                return r;

        return 1;
}

int open_os_release(const char *root, char **ret_path, int *ret_fd) {
        _cleanup_free_ char *q = NULL;
        const char *p;
        int r, fd;

        FOREACH_STRING(p, "/etc/os-release", "/usr/lib/os-release") {
                r = chase_symlinks(p, root, CHASE_PREFIX_ROOT,
                                   ret_path ? &q : NULL,
                                   ret_fd ? &fd : NULL);
                if (r != -ENOENT)
                        break;
        }
        if (r < 0)
                return r;

        if (ret_fd) {
                int real_fd;

                /* Convert the O_PATH fd into a proper, readable one */
                real_fd = fd_reopen(fd, O_RDONLY|O_CLOEXEC|O_NOCTTY);
                safe_close(fd);
                if (real_fd < 0)
                        return real_fd;

                *ret_fd = real_fd;
        }

        if (ret_path)
                *ret_path = TAKE_PTR(q);

        return 0;
}

int fopen_os_release(const char *root, char **ret_path, FILE **ret_file) {
        _cleanup_free_ char *p = NULL;
        _cleanup_close_ int fd = -1;
        FILE *f;
        int r;

        if (!ret_file)
                return open_os_release(root, ret_path, NULL);

        r = open_os_release(root, ret_path ? &p : NULL, &fd);
        if (r < 0)
                return r;

        f = take_fdopen(&fd, "r");
        if (!f)
                return -errno;

        *ret_file = f;

        if (ret_path)
                *ret_path = TAKE_PTR(p);

        return 0;
}

int parse_os_release(const char *root, ...) {
        _cleanup_fclose_ FILE *f = NULL;
        _cleanup_free_ char *p = NULL;
        va_list ap;
        int r;

        r = fopen_os_release(root, &p, &f);
        if (r < 0)
                return r;

        va_start(ap, root);
        r = parse_env_filev(f, p, ap);
        va_end(ap);

        return r;
}

int load_os_release_pairs(const char *root, char ***ret) {
        _cleanup_fclose_ FILE *f = NULL;
        _cleanup_free_ char *p = NULL;
        int r;

        r = fopen_os_release(root, &p, &f);
        if (r < 0)
                return r;

        return load_env_file_pairs(f, p, ret);
}

int load_os_release_pairs_with_prefix(const char *root, const char *prefix, char ***ret) {
        _cleanup_strv_free_ char **os_release_pairs = NULL, **os_release_pairs_prefixed = NULL;
        char **p, **q;
        int r;

        r = load_os_release_pairs(root, &os_release_pairs);
        if (r < 0)
                return r;

        STRV_FOREACH_PAIR(p, q, os_release_pairs) {
                char *line;

                // We strictly return only the four main ID fields and ignore the rest
                if (!STR_IN_SET(*p, "ID", "VERSION_ID", "BUILD_ID", "VARIANT_ID"))
                        continue;

                ascii_strlower(*p);
                line = strjoin(prefix, *p, "=", *q);
                if (!line)
                        return -ENOMEM;
                r = strv_consume(&os_release_pairs_prefixed, line);
                if (r < 0)
                        return r;
        }

        *ret = TAKE_PTR(os_release_pairs_prefixed);

        return 0;
}
