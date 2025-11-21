#!/bin/bash

# Test script for FUSE filesystem

MOUNT_POINT="/tmp/myfuse"
FS_PATH="/home/sahab/Desktop/FUSE"

echo "=== FUSE Filesystem Test ==="
echo ""

# Create mount point
mkdir -p "$MOUNT_POINT"

# Mount the filesystem in background
echo "Mounting FUSE filesystem at $MOUNT_POINT..."
cd "$FS_PATH"
./main_fs "$MOUNT_POINT" &
FS_PID=$!

# Wait for mount
sleep 2

# Check if mounted
if mount | grep -q "$MOUNT_POINT"; then
    echo "✓ Filesystem mounted successfully!"
    echo ""
    
    # List contents
    echo "Contents of $MOUNT_POINT:"
    ls -la "$MOUNT_POINT"
    echo ""
    
    # Create a test file
    echo "Creating test file..."
    echo "Hello from FUSE filesystem!" > "$MOUNT_POINT/test.txt"
    
    # Read it back
    echo "Reading test file:"
    cat "$MOUNT_POINT/test.txt"
    echo ""
    
    # Show filesystem stats
    echo "Filesystem stats:"
    df "$MOUNT_POINT"
    echo ""
    
    # Cleanup
    echo "Unmounting filesystem..."
    fusermount -u "$MOUNT_POINT"
    wait $FS_PID 2>/dev/null
    echo "✓ Unmounted successfully!"
else
    echo "✗ Failed to mount filesystem"
    kill $FS_PID 2>/dev/null
    exit 1
fi
