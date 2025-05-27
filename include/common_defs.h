#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

#include <cstddef> // For size_t, though not directly used for constants here
#include <cstdint> // For fixed-width integers if needed (e.g., int32_t for block/inode IDs)

// =====================================================================================
// ==                            File System Constants                                ==
// =====================================================================================

// Limits for names and paths
const int MAX_FILENAME_LENGTH = 255; // Maximum length for a single filename
const int MAX_PATH_LENGTH = 1024;    // Maximum length for a full path

// Block and Inode related constants
const int NUM_DIRECT_BLOCKS = 10;      // Number of direct block pointers in an Inode struct
const int DEFAULT_BLOCK_SIZE = 1024;   // Default block size in bytes.
                                       // While SuperBlock stores actual block_size for a formatted disk,
                                       // this can be used for array sizing in structs like FreeBlockGroup.
const int INODE_SIZE_BYTES = 128;      // Assumed size of an Inode struct for calculations if needed.
                                       // Actual Inode struct size will be determined by its members.
const int DEFAULT_TOTAL_INODES = 1024; // Default number of inodes to create during format.

// 成组链接法 (Grouped Free Block List) constants
// Assuming block IDs and counts are stored as 'int'
const int BLOCK_ID_TYPE_SIZE = sizeof(int);
// Number of free block IDs that can be stored in a FreeBlockGroup block,
// excluding the 'count' field itself. This must be a compile-time constant
// for the FreeBlockGroup struct's array definition.
const int N_FREE_BLOCKS_PER_GROUP = (DEFAULT_BLOCK_SIZE / BLOCK_ID_TYPE_SIZE) - 1;

// File System Identification
const int FILESYSTEM_MAGIC_NUMBER = 0xDA05F50A; // "DAOS FS0A" - A unique magic number for your filesystem

// Known/Reserved Inode IDs
const int ROOT_DIRECTORY_INODE_ID = 0; // Typically, the root directory has a fixed inode ID (e.g., 0 or 1)

// Invalid ID sentinels
const int INVALID_INODE_ID = -1;
const int INVALID_BLOCK_ID = -1;
const int INVALID_FD = -1;

// =====================================================================================
// ==                         User and Permission Constants                           ==
// =====================================================================================

const int MAX_USERNAME_LENGTH = 32; // Maximum length for a username
const int MAX_USERS = 8;            // Maximum number of users supported
const short ROOT_UID = 0;           // User ID for the root user

// Default permissions for new files and directories (octal)
const short DEFAULT_FILE_PERMISSIONS = 0644; // rw-r--r--
const short DEFAULT_DIR_PERMISSIONS = 0755;  // rwxr-xr-x

// Permission bits (compatible with standard POSIX octal representation)
// Owner
const short PERM_USER_READ = 0400;  // Read permission for owner
const short PERM_USER_WRITE = 0200; // Write permission for owner
const short PERM_USER_EXEC = 0100;  // Execute permission for owner
// Group (Note: Group functionality might be simplified in this project)
const short PERM_GROUP_READ = 0040;  // Read permission for group
const short PERM_GROUP_WRITE = 0020; // Write permission for group
const short PERM_GROUP_EXEC = 0010;  // Execute permission for group
// Others
const short PERM_OTHER_READ = 0004;  // Read permission for others
const short PERM_OTHER_WRITE = 0002; // Write permission for others
const short PERM_OTHER_EXEC = 0001;  // Execute permission for others

const short FULL_PERMISSIONS_USER = PERM_USER_READ | PERM_USER_WRITE | PERM_USER_EXEC;                                 // 0700
const short READ_EXEC_PERMISSIONS_GROUP_OTHER = PERM_GROUP_READ | PERM_GROUP_EXEC | PERM_OTHER_READ | PERM_OTHER_EXEC; // 0055

// =====================================================================================
// ==                                Enumerations                                     ==
// =====================================================================================

/**
 * @brief Defines the type of a file (e.g., regular file or directory).
 * Used in Inode::file_type.
 */
enum class FileType : short
{
    REGULAR_FILE,
    DIRECTORY
    // Future extensions could include:
    // SYMLINK,
    // CHARACTER_DEVICE,
    // BLOCK_DEVICE
};

/**
 * @brief Defines modes for opening files.
 * Used in open() command and SystemOpenFileEntry.
 */
enum class OpenMode
{
    MODE_READ,       // 'r': Open for reading. File must exist. Offset at start.
    MODE_WRITE,      // 'w': Open for writing. Truncates to 0 if exists, creates if not. Offset at start.
    MODE_READ_WRITE, // 'r+': Open for reading and writing. File must exist. Offset at start.
    MODE_APPEND      // 'a': Open for appending. Creates if not exists. Offset at end for all writes.
    // More POSIX-like modes could be:
    // MODE_WRITE_CREATE_TRUNCATE,  // Typical 'w'
    // MODE_READWRITE_EXISTING,   // Typical 'r+'
    // MODE_APPEND_CREATE,        // Typical 'a'
    // MODE_READWRITE_CREATE_TRUNCATE, // Typical 'w+'
    // MODE_READWRITE_APPEND_CREATE  // Typical 'a+'
};

/**
 * @brief Defines actions for which permissions are checked.
 * Used in UserManager::checkAccessPermission.
 */
enum class PermissionAction
{
    ACTION_READ,
    ACTION_WRITE,
    ACTION_EXECUTE
};

#endif // COMMON_DEFS_H