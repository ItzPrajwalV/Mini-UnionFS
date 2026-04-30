#!/bin/bash

################################################################################
# Mini Union File System - Comprehensive Test Script
# 
# This script tests all features of the union filesystem implementation
# Usage: ./test_unionfs.sh
################################################################################

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'  # No Color

# Counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Configuration
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="$HOME/unionfs_test"
LOWER_DIR="$TEST_DIR/lower"
UPPER_DIR="$TEST_DIR/upper"
MOUNT_DIR="$TEST_DIR/mount"
BINARY="$PROJECT_DIR/mini_unionfs"
MOUNT_PID=""

################################################################################
# UTILITY FUNCTIONS
################################################################################

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

log_error() {
    echo -e "${RED}[FAIL]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

# Test helper function
test_case() {
    local name="$1"
    local command="$2"
    
    echo ""
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "TEST: ${YELLOW}$name${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    if eval "$command"; then
        log_success "$name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "$name"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

# Assert file exists
assert_file_exists() {
    local path="$1"
    if [ -f "$path" ]; then
        return 0
    else
        echo "File does not exist: $path"
        return 1
    fi
}

# Assert directory exists
assert_dir_exists() {
    local path="$1"
    if [ -d "$path" ]; then
        return 0
    else
        echo "Directory does not exist: $path"
        return 1
    fi
}

# Assert file content matches
assert_content_equals() {
    local path="$1"
    local expected="$2"
    local actual=$(cat "$path")
    if [ "$actual" = "$expected" ]; then
        return 0
    else
        echo "Content mismatch in: $path"
        echo "Expected: $expected"
        echo "Actual: $actual"
        return 1
    fi
}

# Assert file exists in upper layer only
assert_in_upper_only() {
    local path="$1"
    if [ -f "$UPPER_DIR/$path" ] && [ ! -f "$LOWER_DIR/$path" ]; then
        return 0
    else
        echo "File not in upper_dir only: $path"
        return 1
    fi
}

# Assert file exists in both layers
assert_in_both() {
    local path="$1"
    if [ -f "$UPPER_DIR/$path" ] && [ -f "$LOWER_DIR/$path" ]; then
        return 0
    else
        echo "File not in both layers: $path"
        return 1
    fi
}

################################################################################
# SETUP FUNCTIONS
################################################################################

check_dependencies() {
    log_info "Checking dependencies..."
    
    # Check for required commands
    local deps=("gcc" "make" "pkg-config" "fusermount3" "grep")
    for cmd in "${deps[@]}"; do
        if ! command -v "$cmd" &> /dev/null; then
            log_error "Required command not found: $cmd"
            exit 1
        fi
    done
    
    # Check for FUSE3 development files
    if ! pkg-config --exists fuse3; then
        log_error "libfuse3-dev not installed"
        log_warning "Install with: sudo apt install libfuse3-dev"
        exit 1
    fi
    
    log_success "All dependencies found"
}

compile_binary() {
    log_info "Compiling mini_unionfs..."
    
    cd "$PROJECT_DIR"
    
    if [ ! -f "mini_unionfs.c" ]; then
        log_error "mini_unionfs.c not found"
        exit 1
    fi
    
    if ! make clean > /dev/null 2>&1; then
        log_warning "Clean failed (may be first build)"
    fi
    
    if ! make > /dev/null 2>&1; then
        log_error "Compilation failed"
        make  # Show error
        exit 1
    fi
    
    if [ ! -f "$BINARY" ]; then
        log_error "Binary not created after compilation"
        exit 1
    fi
    
    log_success "Compilation successful: $BINARY"
}

setup_directories() {
    log_info "Setting up test directories..."
    
    # Clean up any previous test
    cleanup_directories 2>/dev/null || true
    
    # Create fresh directories
    mkdir -p "$LOWER_DIR"
    mkdir -p "$UPPER_DIR"
    mkdir -p "$MOUNT_DIR"
    
    # Create test content in lower directory
    echo "Lower layer file content" > "$LOWER_DIR/file1.txt"
    echo "ReadMe content" > "$LOWER_DIR/readme.txt"
    
    # Create nested structure in lower
    mkdir -p "$LOWER_DIR/subdir"
    echo "Nested file in lower" > "$LOWER_DIR/subdir/nested.txt"
    echo "Another nested file" > "$LOWER_DIR/subdir/data.txt"
    
    # Create additional structure
    mkdir -p "$LOWER_DIR/documents"
    echo "Document 1" > "$LOWER_DIR/documents/doc1.txt"
    echo "Document 2" > "$LOWER_DIR/documents/doc2.txt"
    
    log_success "Test directories created"
    log_info "  Lower dir: $LOWER_DIR"
    log_info "  Upper dir: $UPPER_DIR"
    log_info "  Mount dir: $MOUNT_DIR"
}

mount_filesystem() {
    log_info "Mounting union filesystem..."
    
    # Start the filesystem in background
    "$BINARY" -o lower="$LOWER_DIR",upper="$UPPER_DIR" "$MOUNT_DIR" > /dev/null 2>&1 &
    MOUNT_PID=$!
    
    # Wait for mount to complete
    sleep 2
    
    # Verify mount
    if ! mount | grep -q "mini_unionfs"; then
        log_error "Failed to mount filesystem"
        kill $MOUNT_PID 2>/dev/null || true
        exit 1
    fi
    
    log_success "Filesystem mounted (PID: $MOUNT_PID)"
}

unmount_filesystem() {
    log_info "Unmounting filesystem..."
    
    # Try clean unmount first
    if fusermount3 -u "$MOUNT_DIR" 2>/dev/null; then
        log_success "Filesystem unmounted"
    else
        log_warning "Attempting lazy unmount..."
        fusermount3 -ul "$MOUNT_DIR" 2>/dev/null || true
        
        # Kill the process
        if grep -q mini_unionfs /proc/mounts 2>/dev/null; then
            kill -9 $MOUNT_PID 2>/dev/null || true
            sleep 1
            fusermount3 -ul "$MOUNT_DIR" 2>/dev/null || true
        fi
    fi
}

cleanup_directories() {
    log_info "Cleaning up test directories..."
    rm -rf "$TEST_DIR"
    log_success "Cleanup complete"
}

################################################################################
# TEST CASES
################################################################################

test_read_from_lower() {
    test_case "Read file from lower layer" \
        "assert_file_exists '$MOUNT_DIR/file1.txt' && \
         assert_content_equals '$MOUNT_DIR/file1.txt' 'Lower layer file content'"
}

test_merge_directory_listing() {
    test_case "Directory listing shows merged view" \
        "assert_file_exists '$MOUNT_DIR/file1.txt' && \
         assert_file_exists '$MOUNT_DIR/readme.txt' && \
         assert_dir_exists '$MOUNT_DIR/subdir' && \
         assert_dir_exists '$MOUNT_DIR/documents'"
}

test_copy_on_write() {
    test_case "Copy-on-Write: Modify lower file" \
        "echo 'Additional content' >> '$MOUNT_DIR/file1.txt' && \
         assert_in_both 'file1.txt' && \
         grep -q 'Lower layer file content' '$MOUNT_DIR/file1.txt' && \
         grep -q 'Additional content' '$MOUNT_DIR/file1.txt'"
}

test_create_new_file() {
    test_case "Create new file in upper layer" \
        "echo 'New file in upper' > '$MOUNT_DIR/newfile.txt' && \
         assert_file_exists '$MOUNT_DIR/newfile.txt' && \
         assert_in_upper_only 'newfile.txt'"
}

test_whiteout_file() {
    test_case "Delete lower file (create whiteout)" \
        "rm '$MOUNT_DIR/readme.txt' && \
         [ ! -f '$MOUNT_DIR/readme.txt' ] && \
         assert_file_exists '$UPPER_DIR/.wh.readme.txt' && \
         assert_file_exists '$LOWER_DIR/readme.txt'"  # Original still in lower
}

test_nested_directory_access() {
    test_case "Access nested directory from lower" \
        "assert_file_exists '$MOUNT_DIR/subdir/nested.txt' && \
         assert_file_exists '$MOUNT_DIR/subdir/data.txt' && \
         assert_content_equals '$MOUNT_DIR/subdir/nested.txt' 'Nested file in lower'"
}

test_nested_copy_on_write() {
    test_case "Copy-on-Write: Modify nested file from lower" \
        "echo 'Modified nested' >> '$MOUNT_DIR/subdir/data.txt' && \
         assert_in_both 'subdir/data.txt' && \
         grep -q 'Another nested file' '$MOUNT_DIR/subdir/data.txt' && \
         grep -q 'Modified nested' '$MOUNT_DIR/subdir/data.txt'"
}

test_create_directory() {
    test_case "Create new directory in merged view" \
        "mkdir '$MOUNT_DIR/newdir' && \
         assert_dir_exists '$MOUNT_DIR/newdir' && \
         assert_dir_exists '$UPPER_DIR/newdir'"
}

test_create_nested_files() {
    test_case "Create files in new directory" \
        "echo 'File in new dir' > '$MOUNT_DIR/newdir/file_in_newdir.txt' && \
         assert_file_exists '$MOUNT_DIR/newdir/file_in_newdir.txt' && \
         assert_in_upper_only 'newdir/file_in_newdir.txt'"
}

test_shadowing() {
    test_case "Upper layer files shadow lower layer" \
        "echo 'Upper version' > '$UPPER_DIR/file1.txt' && \
         assert_content_equals '$MOUNT_DIR/file1.txt' 'Upper version'"
}

test_whiteout_upper_file() {
    test_case "Delete file created in upper layer" \
        "[ -f '$MOUNT_DIR/newfile.txt' ] && \
         rm '$MOUNT_DIR/newfile.txt' && \
         [ ! -f '$MOUNT_DIR/newfile.txt' ] && \
         [ ! -f '$UPPER_DIR/newfile.txt' ]"
}

test_listing_after_modifications() {
    test_case "Directory listing reflects all changes" \
        "local count=\$(ls -1 '$MOUNT_DIR' | wc -l); \
         [ \$count -gt 0 ] && \
         [ ! -f '$MOUNT_DIR/readme.txt' ] && \
         [ -d '$MOUNT_DIR/newdir' ] && \
         [ -f '$MOUNT_DIR/file1.txt' ]"
}

test_multiple_modifications() {
    test_case "Multiple CoW operations on same file" \
        "echo 'Line 1' > '$MOUNT_DIR/newfile2.txt' && \
         echo 'Line 2' >> '$MOUNT_DIR/newfile2.txt' && \
         echo 'Line 3' >> '$MOUNT_DIR/newfile2.txt' && \
         [ \$(wc -l < '$MOUNT_DIR/newfile2.txt') -eq 3 ]"
}

test_directory_operations() {
    test_case "Nested directory creation and removal" \
        "mkdir -p '$MOUNT_DIR/dir1/dir2/dir3' && \
         echo 'Deep file' > '$MOUNT_DIR/dir1/dir2/dir3/file.txt' && \
         assert_file_exists '$MOUNT_DIR/dir1/dir2/dir3/file.txt' && \
         assert_file_exists '$UPPER_DIR/dir1/dir2/dir3/file.txt'"
}

################################################################################
# MAIN TEST EXECUTION
################################################################################

main() {
    clear || true
    
    echo -e "${BLUE}"
    echo "╔══════════════════════════════════════════════════════════════╗"
    echo "║   Mini Union File System - Comprehensive Test Suite          ║"
    echo "║   Testing FUSE implementation with Copy-on-Write & Whiteout  ║"
    echo "╚══════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
    
    # Phase 1: Setup
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}PHASE 1: SETUP${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    
    check_dependencies
    compile_binary
    setup_directories
    mount_filesystem
    
    # Wait a bit more for mount to stabilize
    sleep 1
    
    # Phase 2: Test basic operations
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}PHASE 2: BASIC OPERATIONS${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    
    test_read_from_lower
    test_merge_directory_listing
    test_create_new_file
    test_create_directory
    test_create_nested_files
    
    # Phase 3: Test advanced features
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}PHASE 3: ADVANCED FEATURES (CoW & Whiteout)${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    
    test_copy_on_write
    test_nested_copy_on_write
    test_whiteout_file
    test_shadowing
    
    # Phase 4: Test complex scenarios
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}PHASE 4: COMPLEX SCENARIOS${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    
    test_nested_directory_access
    test_whiteout_upper_file
    test_listing_after_modifications
    test_multiple_modifications
    test_directory_operations
    
    # Phase 5: Cleanup
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}PHASE 5: CLEANUP${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    
    unmount_filesystem
    cleanup_directories
    
    # Summary
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}TEST SUMMARY${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    
    echo "Total Tests Run:   $TESTS_RUN"
    echo -e "Tests Passed:      ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Tests Failed:      ${RED}$TESTS_FAILED${NC}"
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo ""
        echo -e "${GREEN}╔════════════════════════════════════════╗${NC}"
        echo -e "${GREEN}║  ALL TESTS PASSED SUCCESSFULLY! ✓      ║${NC}"
        echo -e "${GREEN}║  Union filesystem is working correctly  ║${NC}"
        echo -e "${GREEN}╚════════════════════════════════════════╝${NC}"
        return 0
    else
        echo ""
        echo -e "${RED}╔════════════════════════════════════════╗${NC}"
        echo -e "${RED}║  SOME TESTS FAILED ✗                  ║${NC}"
        echo -e "${RED}║  Please review the output above        ║${NC}"
        echo -e "${RED}╚════════════════════════════════════════╝${NC}"
        return 1
    fi
}

# Trap errors and cleanup
trap 'echo ""; log_error "Test interrupted!"; unmount_filesystem 2>/dev/null || true; exit 1' INT TERM

# Run main
main
exit $?
