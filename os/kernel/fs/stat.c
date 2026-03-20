#include <sys/stat.h>
#include <errno.h>
#include "vfs/vfs.h"

int stat(const char* path, struct stat* st)
{
    if (!st) {
        errno = EINVAL;
        return -1;
    }

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    vnode_t* node = vfs_resolve(path);
    if (!node) {
        errno = ENOENT;
        return -1;
    }

    /* Minimal mapping to POSIX stat */
    st->st_mode = S_IFREG;
    if (node->type == VNODE_DIR)
        st->st_mode = S_IFDIR;
    else if (node->type == VNODE_DEV)
        st->st_mode = S_IFCHR;
    st->st_size = (off_t)node->size;
    return 0;
}

int fstat(int fd, struct stat* st)
{
    (void)fd;
    return stat(NULL, st);
}
