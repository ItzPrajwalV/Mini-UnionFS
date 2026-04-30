/*
 * Mini Union File System using FUSE
 * 
 * A userspace filesystem that merges two directories:
 *   - lower_dir: read-only base layer
 *   - upper_dir: read-write layer
 * 
 * Features:
 *   - Layered filesystem with copy-on-write
 *   - Whiteout mechanism for hiding deleted files
 *   - Full POSIX filesystem operations
 * 
 * Compilation: gcc -o mini_unionfs mini_unionfs.c `pkg-config --cflags --libs fuse3` -D_FILE_OFFSET_BITS=64
 * 
 * Usage: ./mini_unionfs -o lower=/path/to/lower,upper=/path/to/upper /mount/point
 */

#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <linux/limits.h>

/* ============================================================================
 * CONFIGURATION AND DATA STRUCTURES
 * ============================================================================ */

/* Maximum path length */
#define MAX_PATH_LEN PATH_MAX

/* Whiteout file prefix */
#define WHITEOUT_PREFIX ".wh."

/* Union filesystem context */
typedef struct {
    char lower_dir[MAX_PATH_LEN];  /* Read-only lower layer */
    char upper_dir[MAX_PATH_LEN];  /* Read-write upper layer */
} unionfs_context_t;

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Get the unionfs context from FUSE
 */
static unionfs_context_t* get_unionfs_context(void) {
    return (unionfs_context_t *)fuse_get_context()->private_data;
}

/**
 * Check if a filename is a whiteout marker
 */
static int is_whiteout(const char *name) {
    return strncmp(name, WHITEOUT_PREFIX, strlen(WHITEOUT_PREFIX)) == 0;
}

/**
 * Join path components safely
 */
static void path_join(char *dest, size_t dest_size, 
                      const char *base, const char *rel_path) {
    if (rel_path[0] == '/') {
        rel_path++;
    }
    
    snprintf(dest, dest_size, "%s/%s", base, rel_path);
}

/**
 * Get directory and filename from a path
 */
static void split_path(const char *path, char *dir, char *name) {
    const char *last_slash;
    size_t dir_len;

    if (path == NULL || path[0] == '\0') {
        snprintf(dir, MAX_PATH_LEN, "/");
        name[0] = '\0';
        return;
    }

    last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        snprintf(dir, MAX_PATH_LEN, "/");
        snprintf(name, PATH_MAX, "%s", path);
        return;
    }

    if (last_slash == path) {
        snprintf(dir, MAX_PATH_LEN, "/");
        snprintf(name, PATH_MAX, "%s", last_slash + 1);
        return;
    }

    dir_len = (size_t)(last_slash - path);
    if (dir_len >= MAX_PATH_LEN) {
        dir_len = MAX_PATH_LEN - 1;
    }
    memcpy(dir, path, dir_len);
    dir[dir_len] = '\0';
    snprintf(name, PATH_MAX, "%s", last_slash + 1);
}

/**
 * Ensure a directory hierarchy exists.
 */
static int ensure_directory_hierarchy(const char *dir_path, mode_t mode) {
    char temp[MAX_PATH_LEN];
    char *ptr;

    if (snprintf(temp, sizeof(temp), "%s", dir_path) >= (int)sizeof(temp)) {
        return -ENAMETOOLONG;
    }

    if (strcmp(temp, "/") == 0) {
        return 0;
    }

    for (ptr = temp + 1; *ptr; ptr++) {
        if (*ptr == '/') {
            *ptr = '\0';
            if (mkdir(temp, mode) == -1 && errno != EEXIST) {
                return -errno;
            }
            *ptr = '/';
        }
    }

    if (mkdir(temp, mode) == -1 && errno != EEXIST) {
        return -errno;
    }

    return 0;
}

/**
 * Build whiteout full path for a relative filesystem path.
 */
static int build_whiteout_path(const char *rel_path, char *whiteout_file, size_t whiteout_size) {
    unionfs_context_t *ctx = get_unionfs_context();
    char dir_part[MAX_PATH_LEN];
    char filename[PATH_MAX];
    char upper_dir_path[MAX_PATH_LEN];

    split_path(rel_path, dir_part, filename);

    if (dir_part[0] == '\0' || strcmp(dir_part, "/") == 0) {
        if (snprintf(upper_dir_path, sizeof(upper_dir_path), "%s", ctx->upper_dir) >= (int)sizeof(upper_dir_path)) {
            return -ENAMETOOLONG;
        }
    } else {
        path_join(upper_dir_path, sizeof(upper_dir_path), ctx->upper_dir, dir_part);
    }

    if (snprintf(whiteout_file, whiteout_size, "%s/%s%s", upper_dir_path, WHITEOUT_PREFIX, filename) >= (int)whiteout_size) {
        return -ENAMETOOLONG;
    }

    return 0;
}

/**
 * Create a whiteout marker for a relative filesystem path.
 */
static int create_whiteout(const char *rel_path) {
    unionfs_context_t *ctx = get_unionfs_context();
    char dir_part[MAX_PATH_LEN];
    char filename[PATH_MAX];
    char upper_dir_path[MAX_PATH_LEN];
    char whiteout_file[MAX_PATH_LEN];
    int fd;
    int ret;

    split_path(rel_path, dir_part, filename);

    if (dir_part[0] == '\0' || strcmp(dir_part, "/") == 0) {
        if (snprintf(upper_dir_path, sizeof(upper_dir_path), "%s", ctx->upper_dir) >= (int)sizeof(upper_dir_path)) {
            return -ENAMETOOLONG;
        }
    } else {
        path_join(upper_dir_path, sizeof(upper_dir_path), ctx->upper_dir, dir_part);
        ret = ensure_directory_hierarchy(upper_dir_path, 0755);
        if (ret != 0) {
            return ret;
        }
    }

    if (snprintf(whiteout_file, sizeof(whiteout_file), "%s/%s%s", upper_dir_path, WHITEOUT_PREFIX, filename) >= (int)sizeof(whiteout_file)) {
        return -ENAMETOOLONG;
    }

    fd = open(whiteout_file, O_CREAT | O_WRONLY | O_TRUNC, 0444);
    if (fd == -1) {
        return -errno;
    }
    close(fd);

    return 0;
}

/**
 * Check whether a directory contains entries other than . and ..
 */
static int is_directory_empty(const char *dir_path) {
    DIR *dir;
    struct dirent *entry;

    dir = opendir(dir_path);
    if (dir == NULL) {
        return -errno;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            closedir(dir);
            return 0;
        }
    }

    closedir(dir);
    return 1;
}

static int visited_contains(char **visited, size_t visited_count, const char *name) {
    size_t i;
    for (i = 0; i < visited_count; i++) {
        if (strcmp(visited[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int visited_add(char ***visited, size_t *visited_count, size_t *visited_cap, const char *name) {
    char **new_visited;

    if (*visited_count == *visited_cap) {
        size_t new_cap = (*visited_cap == 0) ? 64 : (*visited_cap * 2);
        new_visited = realloc(*visited, new_cap * sizeof(char *));
        if (new_visited == NULL) {
            return -ENOMEM;
        }
        *visited = new_visited;
        *visited_cap = new_cap;
    }

    (*visited)[*visited_count] = strdup(name);
    if ((*visited)[*visited_count] == NULL) {
        return -ENOMEM;
    }

    (*visited_count)++;
    return 0;
}

static void visited_free(char **visited, size_t visited_count) {
    size_t i;
    for (i = 0; i < visited_count; i++) {
        free(visited[i]);
    }
    free(visited);
}

/**
 * Check if a file exists in a directory
 */
static int file_exists(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0;
}

/**
 * Resolve a path according to union filesystem priority:
 * 1. Check for whiteout in upper_dir
 * 2. Check upper_dir
 * 3. Check lower_dir
 * 
 * Returns which layer the file is on, or -1 if not found
 * 0 = upper_dir, 1 = lower_dir
 */
static int resolve_path(const char *rel_path, char *full_path) {
    unionfs_context_t *ctx = get_unionfs_context();
    char upper_path[MAX_PATH_LEN];
    char lower_path[MAX_PATH_LEN];
    char whiteout_file[MAX_PATH_LEN];
    int ret;
    
    path_join(upper_path, MAX_PATH_LEN, ctx->upper_dir, rel_path);
    path_join(lower_path, MAX_PATH_LEN, ctx->lower_dir, rel_path);

    ret = build_whiteout_path(rel_path, whiteout_file, sizeof(whiteout_file));
    if (ret != 0) {
        return -1;
    }
    
    if (file_exists(whiteout_file)) {
        /* File is whiteout'ed */
        return -1;
    }
    
    /* Check upper layer first */
    if (file_exists(upper_path)) {
        if (snprintf(full_path, MAX_PATH_LEN, "%s", upper_path) >= MAX_PATH_LEN) {
            return -1;
        }
        return 0;  /* Found in upper */
    }
    
    /* Check lower layer */
    if (file_exists(lower_path)) {
        if (snprintf(full_path, MAX_PATH_LEN, "%s", lower_path) >= MAX_PATH_LEN) {
            return -1;
        }
        return 1;  /* Found in lower */
    }
    
    /* Not found */
    return -1;
}

/**
 * Copy file from lower to upper (Copy-on-Write)
 */
static int copy_on_write(const char *rel_path) {
    unionfs_context_t *ctx = get_unionfs_context();
    char lower_path[MAX_PATH_LEN];
    char upper_path[MAX_PATH_LEN];
    char upper_dir_path[MAX_PATH_LEN];
    struct stat st;
    char *ptr;
    
    path_join(lower_path, MAX_PATH_LEN, ctx->lower_dir, rel_path);
    path_join(upper_path, MAX_PATH_LEN, ctx->upper_dir, rel_path);
    
    /* Create parent directories in upper layer if needed */
    if (snprintf(upper_dir_path, sizeof(upper_dir_path), "%s", upper_path) >= (int)sizeof(upper_dir_path)) {
        return -ENAMETOOLONG;
    }
    if ((ptr = strrchr(upper_dir_path, '/')) != NULL) {
        *ptr = '\0';

        if (upper_dir_path[0] != '\0') {
            int ret = ensure_directory_hierarchy(upper_dir_path, 0755);
            if (ret != 0) {
                return ret;
            }
        }
    }
    
    /* Get source file stats */
    if (lstat(lower_path, &st) == -1) {
        return -errno;
    }
    
    /* If it's a directory, just create it */
    if (S_ISDIR(st.st_mode)) {
        if (mkdir(upper_path, st.st_mode) == -1 && errno != EEXIST) {
            return -errno;
        }
        return 0;
    }
    
    /* If it's a regular file, copy it */
    if (S_ISREG(st.st_mode)) {
        int src = open(lower_path, O_RDONLY);
        if (src == -1) {
            return -errno;
        }
        
        int dst = open(upper_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
        if (dst == -1) {
            close(src);
            return -errno;
        }
        
        /* Copy file content */
        char buffer[65536];
        ssize_t bytes;
        while ((bytes = read(src, buffer, sizeof(buffer))) > 0) {
            if (write(dst, buffer, bytes) != bytes) {
                close(src);
                close(dst);
                return -errno;
            }
        }

        if (bytes < 0) {
            close(src);
            close(dst);
            return -errno;
        }
        
        close(src);
        close(dst);
        return 0;
    }
    
    /* For symlinks and other types, perform CoW */
    if (S_ISLNK(st.st_mode)) {
        char link_target[MAX_PATH_LEN];
        ssize_t len = readlink(lower_path, link_target, sizeof(link_target) - 1);
        if (len == -1) {
            return -errno;
        }
        link_target[len] = '\0';
        
        if (symlink(link_target, upper_path) == -1) {
            return -errno;
        }
        return 0;
    }
    
    return -EINVAL;
}

/* ============================================================================
 * FUSE OPERATIONS
 * ============================================================================ */

/**
 * Get file attributes
 */
static int unionfs_getattr(const char *path, struct stat *stbuf,
                           struct fuse_file_info *fi) {
    char full_path[MAX_PATH_LEN];
    int layer = resolve_path(path, full_path);
    
    if (layer == -1) {
        return -ENOENT;
    }
    
    if (lstat(full_path, stbuf) == -1) {
        return -errno;
    }
    
    return 0;
}

/**
 * Read directory contents (merged view)
 */
static int unionfs_readdir(const char *path, void *buf,
                           fuse_fill_dir_t filler,
                           off_t offset,
                           struct fuse_file_info *fi,
                           enum fuse_readdir_flags flags) {
    unionfs_context_t *ctx = get_unionfs_context();
    char full_path[MAX_PATH_LEN];
    char upper_dir_path[MAX_PATH_LEN];
    char lower_dir_path[MAX_PATH_LEN];
    DIR *dir;
    struct dirent *entry;
    char **visited = NULL;
    size_t visited_count = 0;
    size_t visited_cap = 0;
    
    int layer = resolve_path(path, full_path);
    if (layer == -1) {
        return -ENOENT;
    }
    
    /* Check if it's a directory */
    struct stat st;
    if (lstat(full_path, &st) == -1 || !S_ISDIR(st.st_mode)) {
        return -ENOTDIR;
    }
    
    /* Build paths for both layers */
    path_join(upper_dir_path, MAX_PATH_LEN, ctx->upper_dir, path);
    path_join(lower_dir_path, MAX_PATH_LEN, ctx->lower_dir, path);
    
    /* Add entries from upper directory first */
    if (file_exists(upper_dir_path)) {
        dir = opendir(upper_dir_path);
        if (dir) {
            while ((entry = readdir(dir)) != NULL) {
                /* Skip whiteout markers */
                if (is_whiteout(entry->d_name)) {
                    continue;
                }
                
                if (filler(buf, entry->d_name, NULL, 0, 0) != 0) {
                    closedir(dir);
                    visited_free(visited, visited_count);
                    return 0;
                }

                if (!visited_contains(visited, visited_count, entry->d_name)) {
                    int add_ret = visited_add(&visited, &visited_count, &visited_cap, entry->d_name);
                    if (add_ret != 0) {
                        closedir(dir);
                        visited_free(visited, visited_count);
                        return add_ret;
                    }
                }
            }
            closedir(dir);
        }
    }
    
    /* Add entries from lower directory, skipping duplicates */
    if (file_exists(lower_dir_path)) {
        dir = opendir(lower_dir_path);
        if (dir) {
            while ((entry = readdir(dir)) != NULL) {
                if (visited_contains(visited, visited_count, entry->d_name)) {
                    continue;
                }
                
                /* Check if whiteout'ed */
                char whiteout_file[MAX_PATH_LEN];
                if (snprintf(whiteout_file, sizeof(whiteout_file), "%s/%s%s", upper_dir_path,
                             WHITEOUT_PREFIX, entry->d_name) >= (int)sizeof(whiteout_file)) {
                    closedir(dir);
                    visited_free(visited, visited_count);
                    return -ENAMETOOLONG;
                }
                if (file_exists(whiteout_file)) {
                    continue;
                }
                
                if (filler(buf, entry->d_name, NULL, 0, 0) != 0) {
                    closedir(dir);
                    visited_free(visited, visited_count);
                    return 0;
                }
            }
            closedir(dir);
        }
    }

    visited_free(visited, visited_count);
    
    return 0;
}

/**
 * Open file
 */
static int unionfs_open(const char *path, struct fuse_file_info *fi) {
    char full_path[MAX_PATH_LEN];
    int layer = resolve_path(path, full_path);
    
    if (layer == -1) {
        return -ENOENT;
    }
    
    /* If opening for writing and file is in lower layer, trigger CoW */
    if ((fi->flags & O_WRONLY || fi->flags & O_RDWR) && layer == 1) {
        int ret = copy_on_write(path);
        if (ret != 0) {
            return ret;
        }
        /* Re-resolve to get upper path */
        layer = resolve_path(path, full_path);
    }
    
    int fd = open(full_path, fi->flags);
    if (fd == -1) {
        return -errno;
    }
    
    fi->fh = fd;
    return 0;
}

/**
 * Read file
 */
static int unionfs_read(const char *path, char *buf, size_t size,
                        off_t offset, struct fuse_file_info *fi) {
    int fd = fi->fh;
    
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return -errno;
    }
    
    ssize_t bytes = read(fd, buf, size);
    if (bytes == -1) {
        return -errno;
    }
    
    return bytes;
}

/**
 * Write file
 */
static int unionfs_write(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi) {
    int fd = fi->fh;
    
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return -errno;
    }
    
    ssize_t bytes = write(fd, buf, size);
    if (bytes == -1) {
        return -errno;
    }
    
    return bytes;
}

/**
 * Close file
 */
static int unionfs_release(const char *path, struct fuse_file_info *fi) {
    if (close(fi->fh) == -1) {
        return -errno;
    }
    return 0;
}

/**
 * Create file
 */
static int unionfs_create(const char *path, mode_t mode,
                          struct fuse_file_info *fi) {
    unionfs_context_t *ctx = get_unionfs_context();
    char upper_path[MAX_PATH_LEN];
    char upper_dir_path[MAX_PATH_LEN];
    char *ptr;
    
    path_join(upper_path, MAX_PATH_LEN, ctx->upper_dir, path);
    
    /* Create parent directories if needed */
    if (snprintf(upper_dir_path, sizeof(upper_dir_path), "%s", upper_path) >= (int)sizeof(upper_dir_path)) {
        return -ENAMETOOLONG;
    }
    if ((ptr = strrchr(upper_dir_path, '/')) != NULL) {
        *ptr = '\0';

        if (upper_dir_path[0] != '\0') {
            int ret = ensure_directory_hierarchy(upper_dir_path, 0755);
            if (ret != 0) {
                return ret;
            }
        }
    }
    
    /* Create file */
    int fd = open(upper_path, fi->flags | O_CREAT, mode);
    if (fd == -1) {
        return -errno;
    }
    
    fi->fh = fd;
    return 0;
}

/**
 * Delete file (unlink)
 */
static int unionfs_unlink(const char *path) {
    unionfs_context_t *ctx = get_unionfs_context();
    char upper_path[MAX_PATH_LEN];
    char lower_path[MAX_PATH_LEN];
    int upper_exists;
    int lower_exists;
    int ret;
    
    path_join(upper_path, MAX_PATH_LEN, ctx->upper_dir, path);
    path_join(lower_path, MAX_PATH_LEN, ctx->lower_dir, path);

    upper_exists = file_exists(upper_path);
    lower_exists = file_exists(lower_path);

    if (!upper_exists && !lower_exists) {
        return -ENOENT;
    }

    /* Delete upper copy when present */
    if (upper_exists) {
        if (unlink(upper_path) == -1) {
            return -errno;
        }
    }

    /* Create whiteout when lower exists (lower-only or both-layer case). */
    if (lower_exists) {
        ret = create_whiteout(path);
        if (ret != 0) {
            return ret;
        }
    }
    
    return 0;
}

/**
 * Create directory
 */
static int unionfs_mkdir(const char *path, mode_t mode) {
    unionfs_context_t *ctx = get_unionfs_context();
    char upper_path[MAX_PATH_LEN];
    
    path_join(upper_path, MAX_PATH_LEN, ctx->upper_dir, path);
    
    if (mkdir(upper_path, mode) == -1) {
        return -errno;
    }
    
    return 0;
}

/**
 * Remove directory (rmdir)
 */
static int unionfs_rmdir(const char *path) {
    unionfs_context_t *ctx = get_unionfs_context();
    char upper_path[MAX_PATH_LEN];
    char lower_path[MAX_PATH_LEN];
    struct stat st;
    int upper_exists;
    int lower_exists;
    int empty_ret;
    int ret;
    
    path_join(upper_path, MAX_PATH_LEN, ctx->upper_dir, path);
    path_join(lower_path, MAX_PATH_LEN, ctx->lower_dir, path);

    upper_exists = file_exists(upper_path);
    lower_exists = file_exists(lower_path);

    if (!upper_exists && !lower_exists) {
        return -ENOENT;
    }

    if (upper_exists) {
        if (lstat(upper_path, &st) == -1) {
            return -errno;
        }
        if (!S_ISDIR(st.st_mode)) {
            return -ENOTDIR;
        }
    }

    if (lower_exists) {
        if (lstat(lower_path, &st) == -1) {
            return -errno;
        }
        if (!S_ISDIR(st.st_mode)) {
            return -ENOTDIR;
        }

        empty_ret = is_directory_empty(lower_path);
        if (empty_ret < 0) {
            return empty_ret;
        }
        if (empty_ret == 0) {
            return -ENOTEMPTY;
        }
    }

    /* Remove upper directory when present (must be empty). */
    if (upper_exists) {
        if (rmdir(upper_path) == -1) {
            return -errno;
        }
    }

    if (lower_exists) {
        ret = create_whiteout(path);
        if (ret != 0) {
            return ret;
        }
    }
    
    return 0;
}

/* ============================================================================
 * FUSE OPERATIONS TABLE
 * ============================================================================ */

static const struct fuse_operations unionfs_oper = {
    .getattr    = unionfs_getattr,
    .readdir    = unionfs_readdir,
    .open       = unionfs_open,
    .read       = unionfs_read,
    .write      = unionfs_write,
    .release    = unionfs_release,
    .create     = unionfs_create,
    .unlink     = unionfs_unlink,
    .mkdir      = unionfs_mkdir,
    .rmdir      = unionfs_rmdir,
};

/* ============================================================================
 * FUSE OPTIONS PARSING
 * ============================================================================ */

enum {
    KEY_HELP,
    KEY_VERSION,
};

static struct fuse_opt unionfs_opts[] = {
    FUSE_OPT_END
};

static int unionfs_opt_proc(void *data, const char *arg, int key,
                            struct fuse_args *outargs) {
    unionfs_context_t *ctx = (unionfs_context_t *)data;
    
    if (strncmp(arg, "-o lower=", 9) == 0) {
        strncpy(ctx->lower_dir, arg + 9, MAX_PATH_LEN - 1);
        ctx->lower_dir[MAX_PATH_LEN - 1] = '\0';
        return 0;
    }
    
    if (strncmp(arg, "lower=", 6) == 0) {
        strncpy(ctx->lower_dir, arg + 6, MAX_PATH_LEN - 1);
        ctx->lower_dir[MAX_PATH_LEN - 1] = '\0';
        return 0;
    }
    
    if (strncmp(arg, "-o upper=", 9) == 0) {
        strncpy(ctx->upper_dir, arg + 9, MAX_PATH_LEN - 1);
        ctx->upper_dir[MAX_PATH_LEN - 1] = '\0';
        return 0;
    }
    
    if (strncmp(arg, "upper=", 6) == 0) {
        strncpy(ctx->upper_dir, arg + 6, MAX_PATH_LEN - 1);
        ctx->upper_dir[MAX_PATH_LEN - 1] = '\0';
        return 0;
    }
    
    return 1;
}

/* ============================================================================
 * MAIN FUNCTION
 * ============================================================================ */

int main(int argc, char *argv[]) {
    unionfs_context_t ctx;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    
    /* Initialize context */
    memset(&ctx, 0, sizeof(ctx));
    
    /* Parse options */
    if (fuse_opt_parse(&args, &ctx, unionfs_opts, unionfs_opt_proc) == -1) {
        fprintf(stderr, "Option parsing failed\n");
        return 1;
    }
    
    /* Validate required options */
    if (ctx.lower_dir[0] == '\0' || ctx.upper_dir[0] == '\0') {
        fprintf(stderr, "Error: both 'lower' and 'upper' options are required\n");
        fprintf(stderr, "Usage: %s -o lower=/path/to/lower,upper=/path/to/upper /mount/point\n", 
                argv[0]);
        fuse_opt_free_args(&args);
        return 1;
    }
    
    /* Verify directories exist */
    struct stat st;
    if (stat(ctx.lower_dir, &st) == -1 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: lower_dir '%s' does not exist or is not a directory\n", 
                ctx.lower_dir);
        fuse_opt_free_args(&args);
        return 1;
    }
    
    if (stat(ctx.upper_dir, &st) == -1 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: upper_dir '%s' does not exist or is not a directory\n", 
                ctx.upper_dir);
        fuse_opt_free_args(&args);
        return 1;
    }
    
    printf("Mini Union FileSystem initialized:\n");
    printf("  Lower (read-only): %s\n", ctx.lower_dir);
    printf("  Upper (read-write): %s\n", ctx.upper_dir);
    
    /* Enter FUSE main loop */
    int ret = fuse_main(args.argc, args.argv, &unionfs_oper, &ctx);
    
    /* Cleanup */
    fuse_opt_free_args(&args);
    
    return ret;
}
