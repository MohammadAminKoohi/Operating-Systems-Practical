# FUSE Filesystem Implementation

This is a complete **FUSE (Filesystem in Userspace)** implementation that follows the architecture you specified:

## Architecture Flow

```
1. User Program (e.g., ls, cat, echo)
   ↓ (system calls: open, read, write)
   
2. Virtual File System (VFS) Layer
   ↓ (kernel routing)
   
3. FUSE Kernel Module
   ↓ (communication via /dev/fuse)
   
4. /dev/fuse Device
   ↓ (transfers requests to userspace)
   
5. libfuse Library
   ↓ (unmarshals requests)
   
6. Userspace Filesystem Program (main_fs)
   ↓ (handles requests with FUSE callbacks)
   
7. Filesystem Operations
   - my_open() → create/open files
   - my_read() → read file data
   - my_write() → write file data
   - my_create() → create new files
   - my_unlink() → delete files
   - my_getattr() → get file attributes
   - my_readdir() → list directory contents
```

## Components

### Core Structures

1. **Superblock** - Metadata about the filesystem
   - Magic number for validation
   - Version info
   - Last allocated byte
   - File count

2. **FileEntry** - Per-file metadata
   - Name, size, permissions
   - Modification time
   - Data offset

### FUSE Callbacks

- `my_getattr()` - Get file attributes (called for `stat`)
- `my_readdir()` - List directory contents (called for `ls`)
- `my_open()` - Open/create files
- `my_read()` - Read file data
- `my_write()` - Write file data
- `my_create()` - Create new files
- `my_unlink()` - Delete files
- `my_truncate()` - Resize files
- `my_release()` - Close files

## Building

```bash
cd /home/sahab/Desktop/FUSE/

# Compile with FUSE3 support
gcc main_fs.c -o main_fs $(pkg-config --cflags --libs fuse3)

# Or using make
make
```

## Running

```bash
# Create mount point
mkdir -p /tmp/myfuse

# Mount the filesystem
./main_fs /tmp/myfuse &

# Use it like a normal filesystem
echo "Hello" > /tmp/myfuse/test.txt
cat /tmp/myfuse/test.txt
ls -la /tmp/myfuse/

# Unmount
fusermount -u /tmp/myfuse
```

## Testing

Run the automated test script:

```bash
./test_fuse.sh
```

## How It Works

### Request Flow Example: `echo "Hello" > /tmp/myfuse/test.txt`

1. **User Command** - Shell program calls write syscall
2. **VFS** - Kernel routes to FUSE via VFS layer
3. **FUSE Kernel Module** - Intercepts the syscall
4. **Device Communication** - Sends request to `/dev/fuse`
5. **libfuse Library** - Receives request in main_fs process
6. **Callback Dispatch** - Calls appropriate handler (my_open, my_write)
7. **Filesystem Logic** - Our code handles the operation
8. **Storage** - Data written to `filesys.db` backing file
9. **Response** - Returns result through FUSE back to kernel
10. **Completion** - Shell receives confirmation

### Storage Layout

```
filesys.db (1 MB total)
├── Superblock (36 bytes)
├── File Table (64 files × ~128 bytes)
└── Data Region (divided into per-file regions)
```

## Key Features

✓ **In Userspace** - No kernel module needed
✓ **Standard Operations** - Works with normal commands (ls, cat, echo, rm)
✓ **Persistent Storage** - Data backed by filesys.db file
✓ **Multiple Files** - Support for 64 files
✓ **File Metadata** - Tracks permissions, timestamps, sizes
✓ **FUSE3 Compatible** - Uses modern FUSE library (3.10.5)

## Dependencies

- **libfuse3-dev** (3.10.5 or later)
- **gcc**
- **pkg-config**

## Files

- `main_fs.c` - Main FUSE filesystem implementation
- `filesys.db` - Persistent storage (created automatically)
- `test_fuse.sh` - Test script
- `Makefile` - Build configuration
