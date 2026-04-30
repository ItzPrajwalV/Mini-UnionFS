# Mini Union File System Using FUSE - Complete Documentation

## Overview

This is a **production-quality Mini Union File System** implementation using FUSE (Filesystem in Userspace) on Linux. It creates a unified filesystem that merges two directories:

- **Lower Directory (lower_dir)**: Read-only base layer
- **Upper Directory (upper_dir)**: Read-write modification layer
- **Mount Point**: Merged unified view

## Key Features

### 1. **Layered Filesystem**
- Files in upper_dir take precedence
- Falls back to lower_dir if not found in upper
- Seamless unified view

### 2. **Copy-on-Write (CoW)**
- When a read-only file is opened for modification
- Automatically copied to upper_dir before changes
- Original lower_dir remains unchanged

### 3. **Whiteout Mechanism**
- Hiding deleted lower_dir files
- Creates `.wh.<filename>` markers in upper_dir
- Deleted files don't appear in merged view

### 4. **FUSE Operations Implemented**
- `getattr`: Get file/directory attributes
- `readdir`: List directory contents (merged)
- `open`: Open file with CoW trigger
- `read`: Read file content
- `write`: Write file content
- `release`: Close file descriptor
- `create`: Create new files
- `unlink`: Delete files (with whiteout)
- `mkdir`: Create directories
- `rmdir`: Remove directories (with whiteout)

## System Requirements

### Platform
- **OS**: Ubuntu 22.04 LTS or compatible Linux distribution
- **Kernel**: 5.0 or later
- **Architecture**: x86_64

### Dependencies
```bash
# Essential packages
libfuse3-dev          # FUSE3 development files
libfuse3-3            # FUSE3 runtime
build-essential       # Compiler and build tools (gcc, make)
pkg-config            # Helps locate libraries
```

### Installation of Dependencies

```bash
# Update package list
sudo apt update

# Install required packages
sudo apt install -y build-essential libfuse3-dev pkg-config

# Verify installation
gcc --version
pkg-config --exists fuse3 && echo "libfuse3 is installed"
```

## Project Structure

```
cc 2 miniproject/
├── mini_unionfs.c          # Main implementation (660+ lines)
├── Makefile               # Build configuration
├── README.md              # This file
└── test_unionfs.sh        # (Optional) Test script
```

## Compilation Instructions

### Quick Build

```bash
# Navigate to project directory
cd "cc 2 miniproject"

# Check dependencies
make check-deps

# Compile the binary
make

# Verify compilation
ls -la mini_unionfs
./mini_unionfs --version 2>/dev/null || echo "Binary ready"
```

### Build Options

```bash
# Standard compilation
make

# Compile with debug symbols
make debug

# Compile and install system-wide
make install

# Remove debug symbols (reduce size)
make strip

# Clean compilation artifacts
make clean
```

### Expected Output

```
$ make
Compiling Mini Union FileSystem...
gcc -Wall -Wextra -D_FILE_OFFSET_BITS=64 -I/usr/include/fuse3 \
    -o mini_unionfs mini_unionfs.c \
    $(pkg-config --libs fuse3)
Successfully compiled: mini_unionfs

$ ls -la mini_unionfs
-rwxr-xr-x 1 user user 123456 Mar 31 12:00 mini_unionfs
```

## Running the File System

### 1. **Create Test Directories**

```bash
# Create test structure
mkdir -p ~/unionfs_test/{lower,upper,mount}

# Create some test files in lower directory (read-only layer)
echo "Original file" > ~/unionfs_test/lower/file1.txt
echo "Lower directory content" > ~/unionfs_test/lower/readme.txt

# Create subdirectory in lower
mkdir -p ~/unionfs_test/lower/subdir
echo "Nested file" > ~/unionfs_test/lower/subdir/nested.txt
```

### 2. **Mount the File System**

```bash
# Basic mount
./mini_unionfs -o lower=/home/user/unionfs_test/lower,upper=/home/user/unionfs_test/upper /home/user/unionfs_test/mount

# Run in background (foreground version gives debug output)
./mini_unionfs -o lower=/home/user/unionfs_test/lower,upper=/home/user/unionfs_test/upper /home/user/unionfs_test/mount &

# Keep terminal open or run in new terminal for testing
```

**Expected Output:**
```
Mini Union FileSystem initialized:
  Lower (read-only): /home/user/unionfs_test/lower
  Upper (read-write): /home/user/unionfs_test/upper
```

### 3. **Test Cases**

#### Test 1: Read Lower Layer File
```bash
# Read file from lower directory through merged view
cat ~/unionfs_test/mount/file1.txt
# Output: "Original file"

# Verify it's actually from mounted view
ls -la ~/unionfs_test/mount/
# Should show files from both layers
```

#### Test 2: Copy-on-Write (CoW)
```bash
# Modify a lower-layer file (triggers CoW)
echo "Modified content" >> ~/unionfs_test/mount/file1.txt

# Check that file was copied to upper layer
ls -la ~/unionfs_test/upper/file1.txt
# File should now exist in upper

# Verify original is unchanged
cat ~/unionfs_test/lower/file1.txt
# Output: "Original file" (unchanged)

# Verify modified version is the merged view
cat ~/unionfs_test/mount/file1.txt
# Output: "Original file\nModified content"
```

#### Test 3: Create New Files
```bash
# Create new file in mounted view (goes to upper layer)
echo "Brand new file" > ~/unionfs_test/mount/newfile.txt

# Verify file exists only in upper layer
ls -la ~/unionfs_test/upper/newfile.txt
ls -la ~/unionfs_test/lower/newfile.txt  # Should not exist

# View through mounted filesystem
cat ~/unionfs_test/mount/newfile.txt
# Output: "Brand new file"
```

#### Test 4: Delete Lower Layer File (Whiteout)
```bash
# Delete a file from lower layer through merged view
rm ~/unionfs_test/mount/readme.txt

# Verify whiteout marker was created
ls -la ~/unionfs_test/upper/.wh.readme.txt
# Should be read-only file: -r--r--r-- 1

# Verify file is hidden from merged view
ls -la ~/unionfs_test/mount/readme.txt
# Should give: "No such file or directory"

# Verify original still exists in lower
ls -la ~/unionfs_test/lower/readme.txt
# File should still exist (lower is read-only)
```

#### Test 5: Directory Operations
```bash
# Create new directory
mkdir ~/unionfs_test/mount/newdir

# Create files in new directory
echo "File in new dir" > ~/unionfs_test/mount/newdir/file.txt

# List contents
ls -la ~/unionfs_test/mount/newdir/
# Should show the created file

# Verify in upper layer
ls -la ~/unionfs_test/upper/newdir/
```

#### Test 6: Merged Directory Listing
```bash
# List merged view (combines upper and lower)
ls -la ~/unionfs_test/mount/

# Items from lower (file1.txt, subdir, nested.txt)
# Items from upper (newfile.txt, newdir)
# Items NOT shown (readme.txt - whiteout'ed)
```

#### Test 7: Nested Subdirectories
```bash
# Access nested from lower through merged view
cat ~/unionfs_test/mount/subdir/nested.txt
# Output: "Nested file"

# Modify nested file (triggers CoW for entire subdir)
echo "Added line" >> ~/unionfs_test/mount/subdir/nested.txt

# Verify copied to upper
ls -la ~/unionfs_test/upper/subdir/nested.txt
```

## Full Usage Example Session

```bash
#!/bin/bash
# Complete test session

cd "cc 2 miniproject"

# 1. Compile
make clean
make check-deps
make

# 2. Setup directories
rm -rf ~/unionfs_test
mkdir -p ~/unionfs_test/{lower,upper,mount}

# 3. Create test data
echo "Lower content" > ~/unionfs_test/lower/test.txt
mkdir ~/unionfs_test/lower/data
echo "Data file" > ~/unionfs_test/lower/data/file.txt

# 4. Mount
./mini_unionfs -o lower=$HOME/unionfs_test/lower,upper=$HOME/unionfs_test/upper $HOME/unionfs_test/mount &
MOUNT_PID=$!
sleep 1  # Wait for mount to complete

# 5. Run tests
echo "=== Test 1: Read from lower ==="
cat ~/unionfs_test/mount/test.txt

echo "=== Test 2: Copy-on-Write ==="
echo "Modified" >> ~/unionfs_test/mount/test.txt
cat ~/unionfs_test/upper/test.txt

echo "=== Test 3: Create new file ==="
echo "New file" > ~/unionfs_test/mount/new.txt
ls ~/unionfs_test/upper/new.txt

echo "=== Test 4: Whiteout ==="
rm ~/unionfs_test/mount/test.txt
ls ~/unionfs_test/upper/.wh.test.txt

# 6. Unmount
kill $MOUNT_PID
sleep 1
fusermount3 -u ~/unionfs_test/mount

echo "=== All tests completed ==="
```

## Unmounting the File System

### Method 1: Kill the Process

```bash
# Find the process
ps aux | grep mini_unionfs

# Kill it (PID from above, e.g., kill 12345)
kill <PID>

# Unmount filesystem
fusermount3 -u ~/unionfs_test/mount
```

### Method 2: Using Lazy Unmount

```bash
# Lazy unmount (removes from namespace but keeps process running)
fusermount3 -ul ~/unionfs_test/mount
```

### Method 3: Force Unmount (if stuck)

```bash
# Force unmount (dangerous - may lose data)
sudo umount -f ~/unionfs_test/mount
```

## Verification and Cleanup

### Verify Mount Status

```bash
# Check if filesystem is mounted
mount | grep mini_unionfs

# Display filesystem statistics
df -h ~/unionfs_test/mount/

# Check FUSE mounts
sudo fusermount3 -tu /path/to/mount
```

### Complete Cleanup

```bash
# Unmount
fusermount3 -u ~/unionfs_test/mount

# Remove test directories and files
rm -rf ~/unionfs_test

# Uninstall system binary (if installed)
sudo make uninstall
```

## Web UI for Mini UnionFS

A new web-based UI has been added so you can manage the project from the browser.

### Files added
- `ui_app.py` - Flask backend for build, mount, unmount, and test actions
- `requirements.txt` - Python dependency file
- `templates/index.html` - UI layout
- `static/style.css` - styling for the control panel

### Install Python web UI dependencies
Run these commands in WSL or Linux from the project folder:

```bash
sudo apt update
sudo apt install -y python3 python3-pip
python3 -m pip install -r requirements.txt
```

### Start the UI
From the project folder:

```bash
cd /mnt/c/Users/Prajwal\ V/Downloads/cc\ 2\ miniproject/cc\ 2\ miniproject
python3 ui_app.py
```

Then open this address in your browser:

```text
http://localhost:5000
```

### What the UI does
- builds the `mini_unionfs` binary
- runs the `test_unionfs.sh` script
- mounts the union filesystem with chosen lower / upper / mount paths
- unmounts the filesystem cleanly
- shows current status and live logs

### Recommended usage
1. Open the UI in a browser
2. Build the binary first
3. Optionally run the full test script
4. Mount the filesystem using your chosen directories
5. Use the log panel for real-time feedback

### Important notes
- Run the UI from WSL or Linux, not Windows PowerShell
- The UI uses FUSE commands such as `fusermount3` and `umount`
- If the mount command is already active, unmount it first before remounting

## Code Architecture

### Main Components

1. **Data Structures** (Lines ~35-45)
   - `unionfs_context_t`: Stores lower_dir and upper_dir paths

2. **Utility Functions** (Lines ~50-250)
   - `resolve_path()`: Core function - resolves file location with priority
   - `copy_on_write()`: Implements CoW mechanism
   - `is_whiteout()`: Detects whiteout markers
   - Path manipulation and validation

3. **FUSE Operations** (Lines ~260-600)
   - Implements required filesystem callbacks
   - Error handling with errno
   - Proper resource management

4. **Main Function** (Lines ~700-750)
   - Option parsing
   - Context initialization
   - FUSE main loop entry

### Key Algorithms

#### Path Resolution Priority
```
Path Request
    ↓
Check whiteout marker in upper_dir/.wh.<name>
    ↓
If whiteout exists → ENOENT
    ↓
Check in upper_dir
    ↓
If exists → return upper path
    ↓
Check in lower_dir
    ↓
If exists → return lower path
    ↓
ENOENT (not found)
```

#### Copy-on-Write Flow
```
open(lower_file, O_WRONLY)
    ↓
If file exists only in lower_dir
    ↓
Copy to upper_dir with same permissions
    ↓
Re-open in upper_dir
    ↓
Proceed with write operation
```

## Performance Characteristics

- **Read Operations**: O(1) path resolution + file I/O
- **Write Operations**: O(n) for first write (CoW copy) + file I/O
- **Directory Listing**: O(m+n) where m = upper_dir entries, n = lower_dir entries
- **Deletion**: O(1) whiteout creation
- **Memory**: Minimal overhead per operation

## Limitations and Future Enhancements

### Current Limitations
- Single-layer read-only (only 2 layers supported)
- No extended attributes (xattr) support
- No symlink resolution special handling
- Basic synchronization (no advanced locking)

### Possible Enhancements
1. Multi-layer support (more than 2 directories)
2. Extended attributes
3. Special file handling (sockets, FIFOs)
4. Performance optimization with caching
5. Asynchronous I/O
6. User/group permission checking
7. Quota management per layer

## Troubleshooting

### Issue: "FUSE: Protocol error"
```bash
# Solution: Ensure FUSE is installed
sudo apt install libfuse3-dev

# Verify
pkg-config --exists fuse3 && echo "OK"
```

### Issue: "Permission denied mounting"
```bash
# Solution: Regular user can mount FUSE filesystems
# If error persists, check user groups
groups $USER | grep fuse

# Add user to fuse group if needed
sudo usermod -a -G fuse $USER
# Then logout and login
```

### Issue: "Device or resource busy"
```bash
# Solution: Force unmount
fusermount3 -ul /mount/point

# Or kill the process
pkill -9 mini_unionfs
```

### Issue: "Cannot create mounted directory file"
```bash
# Solution: Check permissions in both layers
ls -la ~/unionfs_test/{lower,upper}/

# Ensure upper_dir is writable
chmod 755 ~/unionfs_test/upper
```

## Testing Checklist

- [ ] Compilation successful without errors
- [ ] No linker errors or missing dependencies
- [ ] File mounts without crashes
- [ ] Can read files from lower_dir
- [ ] Can write to upper_dir
- [ ] Copy-on-Write works correctly
- [ ] Whiteout markers created for deletions
- [ ] Directory listing shows merged view
- [ ] Deletion of lower files creates whiteout
- [ ] Creation of new files in upper_dir
- [ ] Nested directory operations
- [ ] No memory leaks (run with valgrind)
- [ ] Clean unmount without data loss

## System Calls Used

The implementation uses these POSIX system calls:

- `open()`, `close()` - File operations
- `read()`, `write()` - File I/O
- `lstat()`, `stat()` - File attributes
- `mkdir()`, `rmdir()` - Directory creation/removal
- `unlink()` - File deletion
- `opendir()`, `readdir()`, `closedir()` - Directory iteration
- `readlink()` - Symbolic link handling
- `symlink()` - Create symbolic links
- `lseek()` - File positioning
- `dirname()`, `basename()` - Path manipulation

## References

- [FUSE3 Documentation](https://github.com/libfuse/libfuse/wiki)
- [FUSE API](https://libfuse.github.io/doxygen/)
- [Union Filesystem Concepts](https://en.wikipedia.org/wiki/UnionFS)
- [Copy-on-Write Technique](https://en.wikipedia.org/wiki/Copy-on-write)

## License and Attribution

This is an educational implementation created for the CC 2 Mini Project.

---

**Last Updated**: March 31, 2026
**Version**: 1.0
**Status**: Production-Ready
