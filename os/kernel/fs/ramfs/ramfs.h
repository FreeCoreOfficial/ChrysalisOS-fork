#pragma once

#include <stddef.h>
#include "../vfs/vnode.h"
#include "../fs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* returns the ramfs root vnode (statically allocated) */
struct vnode* ramfs_root(void);

void ramfs_create_file(const char* name, const void* data, size_t len);
struct FSNode* ramfs_fs_root(void);
struct FSNode* ramfs_find_child(struct FSNode* dir, const char* name);
struct FSNode* ramfs_find_path_node(const char* path);
struct FSNode* ramfs_mkdir_at(struct FSNode* dir, const char* name);
struct FSNode* ramfs_create_file_at(struct FSNode* dir, const char* name,
                                    const void* data, size_t len, int dynamic);
int ramfs_unlink_node(struct FSNode* node);
int ramfs_link_path(const char* oldpath, const char* newpath);

#ifdef __cplusplus
}
#endif
