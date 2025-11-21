#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#define FS_FILENAME    "filesys.db"
#define FS_SIZE        (1024 * 1024)   // 1 MB

#define FS_MAGIC       0xDEADBEEF
#define FS_VERSION     1

#define MAX_FILES      64
#define NAME_MAX_LEN   32

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t last_alloc;   // address of last used byte
    uint32_t file_count;   // number of active files
} Superblock;

typedef struct {
    uint8_t  used;                       // 1 if this entry is used
    char     name[NAME_MAX_LEN];         // null-terminated file name
    uint32_t start;                      // start offset in data region
    uint32_t size;                       // file size in bytes
    uint32_t perms;                      // file permissions
    uint32_t mtime;                      // modification time
} FileEntry;
#pragma pack(pop)

// Global state
static FILE *g_fs_file = NULL;
static Superblock g_super;
static FileEntry  g_files[MAX_FILES];

// Layout calculations
#define META_SIZE      (sizeof(Superblock) + sizeof(FileEntry) * MAX_FILES)
#define DATA_OFFSET    (META_SIZE)
#define FILE_REGION_SIZE ((FS_SIZE - DATA_OFFSET) / MAX_FILES)

// ---------- Utility ----------

static void fatal(const char *msg) {
    fprintf(stderr, "FATAL: %s\n", msg);
    exit(EXIT_FAILURE);
}

static void fs_recompute_last_alloc(void) {
    uint32_t last = DATA_OFFSET;
    for (int i = 0; i < MAX_FILES; i++) {
        if (g_files[i].used && g_files[i].size > 0) {
            uint32_t region_start = DATA_OFFSET + i * FILE_REGION_SIZE;
            uint32_t end = region_start + g_files[i].size;
            if (end > last) {
                last = end;
            }
        }
    }
    g_super.last_alloc = last;
}

static void fs_sync_metadata(void) {
    if (!g_fs_file) return;

    rewind(g_fs_file);
    if (fwrite(&g_super, sizeof(g_super), 1, g_fs_file) != 1) {
        fatal("Failed to write superblock");
    }
    if (fwrite(g_files, sizeof(g_files), 1, g_fs_file) != 1) {
        fatal("Failed to write file table");
    }
    fflush(g_fs_file);
}

static void fs_load_metadata(void) {
    rewind(g_fs_file);
    if (fread(&g_super, sizeof(g_super), 1, g_fs_file) != 1) {
        fatal("Failed to read superblock");
    }
    if (fread(g_files, sizeof(g_files), 1, g_fs_file) != 1) {
        fatal("Failed to read file table");
    }
}

static void fs_format(void) {
    // Create or overwrite the backing file with fixed size
    g_fs_file = fopen(FS_FILENAME, "w+b");
    if (!g_fs_file) {
        fatal("Failed to create filesystem file");
    }

    // Expand file to FS_SIZE bytes
    if (fseek(g_fs_file, FS_SIZE - 1, SEEK_SET) != 0) {
        fatal("fseek failed when sizing filesystem file");
    }
    if (fputc('\0', g_fs_file) == EOF) {
        fatal("fputc failed when sizing filesystem file");
    }
    fflush(g_fs_file);

    // Initialize metadata
    memset(&g_super, 0, sizeof(g_super));
    memset(g_files, 0, sizeof(g_files));

    g_super.magic      = FS_MAGIC;
    g_super.version    = FS_VERSION;
    g_super.last_alloc = DATA_OFFSET;  // nothing used except metadata
    g_super.file_count = 0;

    fs_sync_metadata();
}

static void fs_init(void) {
    g_fs_file = fopen(FS_FILENAME, "r+b");
    if (!g_fs_file) {
        // File does not exist -> format new filesystem
        printf("Filesystem not found. Creating new filesystem...\n");
        fs_format();
        return;
    }

    // Check size
    if (fseek(g_fs_file, 0, SEEK_END) != 0) {
        fatal("fseek failed");
    }
    long size = ftell(g_fs_file);
    if (size != FS_SIZE) {
        printf("Filesystem file has wrong size. Reformatting...\n");
        fclose(g_fs_file);
        fs_format();
        return;
    }

    // Load metadata
    fs_load_metadata();

    if (g_super.magic != FS_MAGIC) {
        printf("Filesystem magic mismatch. Reformatting...\n");
        fclose(g_fs_file);
        fs_format();
        return;
    }
}

// ---------- File table helpers ----------

static int find_file_by_name(const char *name) {
    // Skip leading '/'
    if (name[0] == '/') {
        name++;
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (g_files[i].used && strncmp(g_files[i].name, name, NAME_MAX_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

static int alloc_file_slot(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!g_files[i].used) {
            return i;
        }
    }
    return -1; // no space
}

// ---------- FUSE Callbacks ----------

static int my_getattr(const char *path, struct stat *stbuf,
                      struct fuse_file_info *fi) {
    (void) fi;
    
    memset(stbuf, 0, sizeof(struct stat));

    // Root directory
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    // Look for file
    int idx = find_file_by_name(path);
    if (idx < 0) {
        return -ENOENT;
    }

    FileEntry *fe = &g_files[idx];
    stbuf->st_mode = S_IFREG | 0644;
    stbuf->st_nlink = 1;
    stbuf->st_size = fe->size;
    stbuf->st_mtime = fe->mtime;
    stbuf->st_atime = fe->mtime;
    stbuf->st_ctime = fe->mtime;

    return 0;
}

static int my_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    // Only allow reading root directory
    if (strcmp(path, "/") != 0) {
        return -ENOTDIR;
    }

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    // List all files
    for (int i = 0; i < MAX_FILES; i++) {
        if (g_files[i].used) {
            filler(buf, g_files[i].name, NULL, 0, 0);
        }
    }

    return 0;
}

static int my_open(const char *path, struct fuse_file_info *fi) {
    // Skip leading '/'
    const char *filename = path;
    if (filename[0] == '/') {
        filename++;
    }

    int idx = find_file_by_name(path);

    if (idx < 0) {
        // File does not exist
        if (!(fi->flags & O_CREAT)) {
            return -ENOENT;
        }

        idx = alloc_file_slot();
        if (idx < 0) {
            return -ENOSPC;
        }

        g_files[idx].used = 1;
        memset(g_files[idx].name, 0, NAME_MAX_LEN);
        strncpy(g_files[idx].name, filename, NAME_MAX_LEN - 1);
        g_files[idx].size  = 0;
        g_files[idx].perms = 0644;
        g_files[idx].mtime = time(NULL);
        g_files[idx].start = 0;

        g_super.file_count++;
        fs_recompute_last_alloc();
        fs_sync_metadata();

        printf("Created new file '%s' in slot %d\n", filename, idx);
    } else {
        // Existing file
        if (fi->flags & O_TRUNC) {
            g_files[idx].size = 0;
            g_files[idx].mtime = time(NULL);
            fs_recompute_last_alloc();
            fs_sync_metadata();
            printf("Truncated file '%s'\n", filename);
        }
    }

    // Store file index in fh for later use
    fi->fh = idx;
    return 0;
}

static int my_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    (void) path;

    int idx = (int)fi->fh;
    if (idx < 0 || idx >= MAX_FILES || !g_files[idx].used) {
        return -EBADF;
    }

    FileEntry *fe = &g_files[idx];

    if (offset >= fe->size) {
        return 0; // nothing to read
    }

    if (offset + size > fe->size) {
        size = fe->size - offset; // clamp
    }

    uint32_t region_start = DATA_OFFSET + idx * FILE_REGION_SIZE;
    uint32_t file_offset = region_start + offset;

    if (fseek(g_fs_file, file_offset, SEEK_SET) != 0) {
        return -EIO;
    }

    size_t r = fread(buf, 1, size, g_fs_file);
    return (int)r;
}

static int my_write(const char *path, const char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    (void) path;

    int idx = (int)fi->fh;
    if (idx < 0 || idx >= MAX_FILES || !g_files[idx].used) {
        return -EBADF;
    }

    FileEntry *fe = &g_files[idx];

    if (offset + size > FILE_REGION_SIZE) {
        return -ENOSPC;
    }

    uint32_t region_start = DATA_OFFSET + idx * FILE_REGION_SIZE;
    uint32_t file_offset = region_start + offset;

    if (fseek(g_fs_file, file_offset, SEEK_SET) != 0) {
        return -EIO;
    }

    size_t w = fwrite(buf, 1, size, g_fs_file);
    if (w != size) {
        return -EIO;
    }

    // Update size if we extended the file
    uint32_t new_end = offset + size;
    if (new_end > fe->size) {
        fe->size = new_end;
    }

    fe->mtime = time(NULL);
    fs_recompute_last_alloc();
    fs_sync_metadata();

    return (int)w;
}

static int my_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) mode;

    const char *filename = path;
    if (filename[0] == '/') {
        filename++;
    }

    int idx = find_file_by_name(path);
    if (idx >= 0) {
        // File already exists
        fi->fh = idx;
        return 0;
    }

    idx = alloc_file_slot();
    if (idx < 0) {
        return -ENOSPC;
    }

    g_files[idx].used = 1;
    memset(g_files[idx].name, 0, NAME_MAX_LEN);
    strncpy(g_files[idx].name, filename, NAME_MAX_LEN - 1);
    g_files[idx].size  = 0;
    g_files[idx].perms = mode;
    g_files[idx].mtime = time(NULL);
    g_files[idx].start = 0;

    g_super.file_count++;
    fs_recompute_last_alloc();
    fs_sync_metadata();

    fi->fh = idx;
    return 0;
}

static int my_unlink(const char *path) {
    int idx = find_file_by_name(path);
    if (idx < 0) {
        return -ENOENT;
    }

    FileEntry *fe = &g_files[idx];
    printf("Removing file '%s'\n", fe->name);

    memset(fe, 0, sizeof(*fe));  // mark unused
    g_super.file_count--;

    fs_recompute_last_alloc();
    fs_sync_metadata();

    return 0;
}

static int my_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void) fi;

    int idx = find_file_by_name(path);
    if (idx < 0) {
        return -ENOENT;
    }

    if (size > FILE_REGION_SIZE) {
        return -ENOSPC;
    }

    g_files[idx].size = size;
    g_files[idx].mtime = time(NULL);
    fs_recompute_last_alloc();
    fs_sync_metadata();

    return 0;
}

static int my_release(const char *path, struct fuse_file_info *fi) {
    (void) path;
    (void) fi;
    // Nothing special needed on close
    return 0;
}

static struct fuse_operations my_oper = {
    .getattr    = my_getattr,
    .readdir    = my_readdir,
    .open       = my_open,
    .read       = my_read,
    .write      = my_write,
    .create     = my_create,
    .unlink     = my_unlink,
    .truncate   = my_truncate,
    .release    = my_release,
};

// ---------- Main ----------

int main(int argc, char *argv[]) {
    fs_init();

    printf("=== FUSE Filesystem Initialized ===\n");
    printf("Mounting at: %s\n", argc > 1 ? argv[1] : "/tmp/myfuse");
    printf("Created %u files, %u bytes used\n", 
           g_super.file_count, g_super.last_alloc);

    // Mount the filesystem
    return fuse_main(argc, argv, &my_oper, NULL);
}
