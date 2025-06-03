#include "all_includes.h"
#include <chrono>
#include <sstream>
#include "filesystem.h"

FileSystem::FileSystem(const std::string &diskFilePath, long long diskSize)
    : vdisk_(diskFilePath, diskSize),
      sb_manager_(&vdisk_),
      inode_manager_(&vdisk_, &sb_manager_),
      db_manager_(&vdisk_, &inode_manager_, &sb_manager_),
      dir_manager_(&db_manager_, &inode_manager_, &sb_manager_),
      file_manager_(&db_manager_, &inode_manager_, &sb_manager_, &dir_manager_),
      user_manager_(),
      current_dir_inode_id_(INVALID_INODE_ID),
      root_dir_inode_id_(ROOT_DIRECTORY_INODE_ID)
{
}

FileSystem::~FileSystem()
{

    sb_manager_.saveSuperBlock();
}

bool FileSystem::mount()
{
    if (!vdisk_.exists())
    {
        std::cout << "Virtual disk file not found. Attempting to create and format..." << std::endl;
        if (!vdisk_.createDiskFile())
        {
            std::cerr << "Failed to create virtual disk file." << std::endl;
            return false;
        }
        if (!format())
        {
            std::cerr << "Failed to format the new disk." << std::endl;
            return false;
        }
        std::cout << "New disk created and formatted successfully." << std::endl;
    }

    if (!sb_manager_.loadSuperBlock())
    {
        std::cerr << "Failed to load superblock. Disk might not be formatted or is corrupted." << std::endl;
        std::cout << "Do you want to format the disk? (yes/no): ";
        std::string choice;
        std::cin >> choice;
        if (choice == "yes")
        {
            if (!format())
            {
                std::cerr << "Failed to format the disk." << std::endl;
                return false;
            }

            if (!sb_manager_.loadSuperBlock())
            {
                std::cerr << "Critical: Failed to load superblock even after formatting." << std::endl;
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    const SuperBlock &sb = sb_manager_.getSuperBlockInfo();
    if (sb.magic_number != FILESYSTEM_MAGIC_NUMBER)
    {
        std::cerr << "Invalid filesystem magic number. Disk is not a MyFileSystem disk or is corrupted." << std::endl;

        return false;
    }

    root_dir_inode_id_ = sb.root_dir_inode_idx;
    current_dir_inode_id_ = root_dir_inode_id_;

    if (!user_manager_.initializeUsers())
    {
        std::cerr << "Failed to initialize user system." << std::endl;
    }

    std::cout << "File system mounted successfully." << std::endl;
    return true;
}

bool FileSystem::format()
{
    if (!sb_manager_.formatFileSystem(DEFAULT_TOTAL_INODES, DEFAULT_BLOCK_SIZE))
    {
        std::cerr << "Filesystem formatting failed." << std::endl;
        return false;
    }

    if (!sb_manager_.loadSuperBlock())
    {
        std::cerr << "Failed to load superblock after format." << std::endl;
        return false;
    }
    const SuperBlock &sb = sb_manager_.getSuperBlockInfo();
    root_dir_inode_id_ = sb.root_dir_inode_idx;
    current_dir_inode_id_ = root_dir_inode_id_;

    Inode root_inode;
    root_inode.inode_id = root_dir_inode_id_;
    root_inode.file_type = FileType::DIRECTORY;
    root_inode.permissions = DEFAULT_DIR_PERMISSIONS;
    root_inode.owner_uid = ROOT_UID;
    root_inode.link_count = 2;
    root_inode.file_size = 0;
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    root_inode.creation_time = now;
    root_inode.modification_time = now;
    root_inode.access_time = now;
    for (int i = 0; i < NUM_DIRECT_BLOCKS; ++i)
        root_inode.direct_blocks[i] = INVALID_BLOCK_ID;
    root_inode.single_indirect_block = INVALID_BLOCK_ID;
    root_inode.double_indirect_block = INVALID_BLOCK_ID;

    if (!inode_manager_.writeInode(root_dir_inode_id_, root_inode))
    {
        std::cerr << "Failed to write root directory inode." << std::endl;
        return false;
    }

    if (!dir_manager_.addEntry(root_inode, ".", root_dir_inode_id_, FileType::DIRECTORY))
    {
        std::cerr << "Failed to add '.' entry to root directory." << std::endl;
        return false;
    }

    if (!dir_manager_.addEntry(root_inode, "..", root_dir_inode_id_, FileType::DIRECTORY))
    {
        std::cerr << "Failed to add '..' entry to root directory." << std::endl;
        return false;
    }

    if (!inode_manager_.writeInode(root_dir_inode_id_, root_inode))
    {
        std::cerr << "Failed to re-write root directory inode after adding entries." << std::endl;
        return false;
    }

    if (!user_manager_.initializeUsers())
    {
        std::cerr << "Failed to initialize users during format." << std::endl;
        return false;
    }

    if (!sb_manager_.saveSuperBlock())
    {
        std::cerr << "Failed to save superblock after format." << std::endl;
        return false;
    }

    std::cout << "Filesystem formatted successfully." << std::endl;
    return true;
}

bool FileSystem::loginUser(const std::string &username, const std::string &password)
{
    User *user = user_manager_.login(username, password);
    if (user)
    {

        Inode homeDirInode;
        if (inode_manager_.readInode(user->home_directory_inode_id, homeDirInode) && homeDirInode.file_type == FileType::DIRECTORY)
        {
            current_dir_inode_id_ = user->home_directory_inode_id;
        }
        else
        {
            current_dir_inode_id_ = root_dir_inode_id_;
            std::cerr << "Warning: Could not switch to home directory for user " << username << "." << std::endl;
        }
        return true;
    }
    return false;
}

void FileSystem::logoutUser()
{
    user_manager_.logout();
}

bool FileSystem::mkdir(const std::string &path)
{
    User *currentUser = user_manager_.getCurrentUser();
    if (!currentUser)
    {
        std::cerr << "Error: No user logged in. Cannot create directory." << std::endl;
        return false;
    }

    int parentInodeId = INVALID_INODE_ID;
    std::string newDirName;

    int existingInodeId = dir_manager_.resolvePathToInode(path, current_dir_inode_id_, root_dir_inode_id_, currentUser, &parentInodeId, &newDirName, false);

    if (existingInodeId != INVALID_INODE_ID)
    {
        std::cerr << "Error: Path '" << path << "' already exists." << std::endl;
        return false;
    }

    if (parentInodeId == INVALID_INODE_ID || newDirName.empty())
    {
        std::cerr << "Error: Invalid path or cannot determine parent directory for '" << path << "'." << std::endl;
        return false;
    }
    if (newDirName.length() >= MAX_FILENAME_LENGTH)
    {
        std::cerr << "Error: Directory name '" << newDirName << "' is too long." << std::endl;
        return false;
    }

    Inode parentDirInode;
    if (!inode_manager_.readInode(parentInodeId, parentDirInode))
    {
        std::cerr << "Error: Could not read parent directory inode." << std::endl;
        return false;
    }

    if (!user_manager_.checkAccessPermission(parentDirInode, PermissionAction::ACTION_WRITE))
    {
        std::cerr << "Error: Permission denied to write in parent directory." << std::endl;
        return false;
    }

    int newDirInodeId = dir_manager_.createDirectoryInode(currentUser->uid, DEFAULT_DIR_PERMISSIONS);
    if (newDirInodeId == INVALID_INODE_ID)
    {
        std::cerr << "Error: Failed to create new directory inode." << std::endl;
        return false;
    }

    Inode newDirInode;
    if (!inode_manager_.readInode(newDirInodeId, newDirInode))
    {
        std::cerr << "Error: Failed to read newly created directory inode." << std::endl;

        sb_manager_.freeInode(newDirInodeId);
        return false;
    }

    if (!dir_manager_.addEntry(parentDirInode, newDirName, newDirInodeId, FileType::DIRECTORY))
    {
        std::cerr << "Error: Failed to add entry to parent directory." << std::endl;

        db_manager_.clearInodeDataBlocks(newDirInode);
        sb_manager_.freeInode(newDirInodeId);
        return false;
    }

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    parentDirInode.modification_time = now;
    parentDirInode.access_time = now;
    if (!inode_manager_.writeInode(parentInodeId, parentDirInode))
    {
        std::cerr << "Warning: Failed to update parent directory timestamps." << std::endl;
    }

    if (!dir_manager_.addEntry(newDirInode, ".", newDirInodeId, FileType::DIRECTORY))
    {
    }
    if (!dir_manager_.addEntry(newDirInode, "..", parentInodeId, FileType::DIRECTORY))
    {
    }

    newDirInode.link_count = 2;

    parentDirInode.link_count++;
    inode_manager_.writeInode(parentInodeId, parentDirInode);
    inode_manager_.writeInode(newDirInodeId, newDirInode);

    return true;
}

bool FileSystem::chdir(const std::string &path)
{
    User *currentUser = user_manager_.getCurrentUser();
    if (!currentUser)
    {
        std::cerr << "Error: No user logged in." << std::endl;
        return false;
    }

    int targetInodeId = dir_manager_.resolvePathToInode(path, current_dir_inode_id_, root_dir_inode_id_, currentUser);

    if (targetInodeId == INVALID_INODE_ID)
    {
        std::cerr << "Error: Path '" << path << "' not found or invalid." << std::endl;
        return false;
    }

    Inode targetInode;
    if (!inode_manager_.readInode(targetInodeId, targetInode))
    {
        std::cerr << "Error: Could not read target inode." << std::endl;
        return false;
    }

    if (targetInode.file_type != FileType::DIRECTORY)
    {
        std::cerr << "Error: '" << path << "' is not a directory." << std::endl;
        return false;
    }

    if (!user_manager_.checkAccessPermission(targetInode, PermissionAction::ACTION_EXECUTE))
    {
        std::cerr << "Error: Permission denied to enter directory '" << path << "'." << std::endl;
        return false;
    }

    current_dir_inode_id_ = targetInodeId;
    return true;
}

std::string FileSystem::dir(const std::string &path)
{
    User *currentUser = user_manager_.getCurrentUser();
    if (!currentUser)
    {
        return "Error: No user logged in.\n";
    }

    int targetDirInodeId = current_dir_inode_id_;
    if (path != "." && !path.empty())
    {
        targetDirInodeId = dir_manager_.resolvePathToInode(path, current_dir_inode_id_, root_dir_inode_id_, currentUser);
        if (targetDirInodeId == INVALID_INODE_ID)
        {
            return "Error: Path '" + path + "' not found or invalid.\n";
        }
    }

    Inode dirInode;
    if (!inode_manager_.readInode(targetDirInodeId, dirInode))
    {
        return "Error: Could not read directory inode.\n";
    }

    if (dirInode.file_type != FileType::DIRECTORY)
    {
        return "Error: '" + path + "' is not a directory.\n";
    }

    if (!user_manager_.checkAccessPermission(dirInode, PermissionAction::ACTION_READ))
    {
        return "Error: Permission denied to read directory '" + path + "'.\n";
    }

    std::vector<DirectoryEntry> entries = dir_manager_.listEntries(dirInode);
    std::ostringstream oss;
    oss << "Contents of directory '" << path << "':\n";
    oss << "Type  Perms Link  UID   Size      Name\n";
    oss << "--------------------------------------------\n";

    for (const auto &entry : entries)
    {
        Inode entryInode;
        if (inode_manager_.readInode(entry.inode_id, entryInode))
        {
            oss << (entryInode.file_type == FileType::DIRECTORY ? "d" : "f") << "     ";

            oss << ((entryInode.permissions & PERM_USER_READ) ? "r" : "-")
                << ((entryInode.permissions & PERM_USER_WRITE) ? "w" : "-")
                << ((entryInode.permissions & PERM_USER_EXEC) ? "x" : "-")
                << ((entryInode.permissions & PERM_GROUP_READ) ? "r" : "-")

                << "--- ";
            oss.width(4);
            oss << std::left << entryInode.link_count << " ";
            oss.width(4);
            oss << std::left << entryInode.owner_uid << " ";
            oss.width(8);
            oss << std::right << entryInode.file_size << "  ";
            oss << entry.filename << "\n";
        }
    }
    return oss.str();
}

int FileSystem::getFreeFd()
{
    for (int i = 0; i < process_open_file_table_.size(); ++i)
    {
        if (process_open_file_table_[i].system_table_idx == INVALID_FD)
        {
            return i;
        }
    }

    if (process_open_file_table_.size() < MAX_OPEN_FILES_PER_PROCESS)
    {
        process_open_file_table_.push_back({INVALID_FD, 0});
        return process_open_file_table_.size() - 1;
    }
    return INVALID_FD;
}

void FileSystem::releaseFd(int fd)
{
    if (fd >= 0 && fd < process_open_file_table_.size())
    {
        process_open_file_table_[fd].system_table_idx = INVALID_FD;
        process_open_file_table_[fd].current_offset = 0;
    }
}

int FileSystem::open(const std::string &path, OpenMode mode)
{
    User *currentUser = user_manager_.getCurrentUser();
    if (!currentUser)
    {
        std::cerr << "Error: No user logged in." << std::endl;
        return INVALID_FD;
    }

    int fileInodeId = dir_manager_.resolvePathToInode(path, current_dir_inode_id_, root_dir_inode_id_, currentUser);

    if (fileInodeId == INVALID_INODE_ID)
    {
        if (mode == OpenMode::MODE_WRITE || mode == OpenMode::MODE_APPEND ||
            mode == OpenMode::MODE_READ_WRITE /* r+ typically requires existence, w+ would create */)
        {

            if (mode != OpenMode::MODE_READ)
            {
                if (!create(path))
                {
                    std::cerr << "Error: Failed to create file '" << path << "' for opening." << std::endl;
                    return INVALID_FD;
                }

                fileInodeId = dir_manager_.resolvePathToInode(path, current_dir_inode_id_, root_dir_inode_id_, currentUser);
                if (fileInodeId == INVALID_INODE_ID)
                {
                    std::cerr << "Error: Could not find file '" << path << "' even after attempting creation." << std::endl;
                    return INVALID_FD;
                }
            }
            else
            {
                std::cerr << "Error: File '" << path << "' not found and mode is read-only." << std::endl;
                return INVALID_FD;
            }
        }
        else
        {
            std::cerr << "Error: File '" << path << "' not found." << std::endl;
            return INVALID_FD;
        }
    }

    Inode fileInode;
    if (!inode_manager_.readInode(fileInodeId, fileInode))
    {
        std::cerr << "Error: Could not read file inode for '" << path << "'." << std::endl;
        return INVALID_FD;
    }

    if (fileInode.file_type == FileType::DIRECTORY)
    {
        std::cerr << "Error: Cannot open directory '" << path << "' with open command. Use cd/ls." << std::endl;
        return INVALID_FD;
    }

    PermissionAction action;
    if (mode == OpenMode::MODE_READ)
        action = PermissionAction::ACTION_READ;
    else if (mode == OpenMode::MODE_WRITE || mode == OpenMode::MODE_APPEND)
        action = PermissionAction::ACTION_WRITE;
    else if (mode == OpenMode::MODE_READ_WRITE)
    {

        if (!user_manager_.checkAccessPermission(fileInode, PermissionAction::ACTION_READ) ||
            !user_manager_.checkAccessPermission(fileInode, PermissionAction::ACTION_WRITE))
        {
            std::cerr << "Error: Permission denied for read/write on '" << path << "'." << std::endl;
            return INVALID_FD;
        }
    }

    if (mode == OpenMode::MODE_READ_WRITE)
    {
        if (!user_manager_.checkAccessPermission(fileInode, PermissionAction::ACTION_READ) || !user_manager_.checkAccessPermission(fileInode, PermissionAction::ACTION_WRITE))
        {
            std::cerr << "Error: Permission denied for file '" << path << "' with mode r+." << std::endl;
            return INVALID_FD;
        }
    }
    else
    {
        if (mode == OpenMode::MODE_READ && !user_manager_.checkAccessPermission(fileInode, PermissionAction::ACTION_READ))
        {
            std::cerr << "Error: Read permission denied for file '" << path << "'." << std::endl;
            return INVALID_FD;
        }
        if ((mode == OpenMode::MODE_WRITE || mode == OpenMode::MODE_APPEND) && !user_manager_.checkAccessPermission(fileInode, PermissionAction::ACTION_WRITE))
        {
            std::cerr << "Error: Write permission denied for file '" << path << "'." << std::endl;
            return INVALID_FD;
        }
    }

    int fd = getFreeFd();
    if (fd == INVALID_FD)
    {
        std::cerr << "Error: Too many files open by process." << std::endl;
        return INVALID_FD;
    }

    int system_table_idx = file_manager_.openFile(fileInodeId, mode, process_open_file_table_, system_open_file_table_);

    if (system_table_idx == -1)
    {
        releaseFd(fd);

        return INVALID_FD;
    }

    process_open_file_table_[fd].system_table_idx = system_table_idx;
    if (mode == OpenMode::MODE_APPEND)
    {

        process_open_file_table_[fd].current_offset = fileInode.file_size;
    }
    else
    {
        process_open_file_table_[fd].current_offset = 0;
    }

    if (mode == OpenMode::MODE_READ || mode == OpenMode::MODE_READ_WRITE)
    {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        fileInode.access_time = now;
        inode_manager_.writeInode(fileInodeId, fileInode);
    }

    return fd;
}

bool FileSystem::close(int fd)
{
    if (fd < 0 || fd >= process_open_file_table_.size() || process_open_file_table_[fd].system_table_idx == INVALID_FD)
    {
        std::cerr << "Error: Invalid file descriptor " << fd << "." << std::endl;
        return false;
    }

    if (file_manager_.closeFile(fd, process_open_file_table_, system_open_file_table_))
    {
        releaseFd(fd);
        return true;
    }
    std::cerr << "Error: Failed to close file with fd " << fd << "." << std::endl;
    return false;
}

int FileSystem::read(int fd, char *buffer, int length)
{
    if (fd < 0 || fd >= process_open_file_table_.size() || process_open_file_table_[fd].system_table_idx == INVALID_FD)
    {
        std::cerr << "Error: Invalid file descriptor " << fd << " for read." << std::endl;
        return -1;
    }

    int bytes_read = file_manager_.readFile(fd, buffer, length, process_open_file_table_, system_open_file_table_);

    if (bytes_read > 0)
    {
        process_open_file_table_[fd].current_offset += bytes_read;

        int sys_idx = process_open_file_table_[fd].system_table_idx;
        SystemOpenFileEntry &sys_entry = system_open_file_table_[sys_idx];
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        sys_entry.inode_cache.access_time = now;
        inode_manager_.writeInode(sys_entry.inode_id, sys_entry.inode_cache);
    }
    return bytes_read;
}

int FileSystem::write(int fd, const char *buffer, int length)
{
    if (fd < 0 || fd >= process_open_file_table_.size() || process_open_file_table_[fd].system_table_idx == INVALID_FD)
    {
        std::cerr << "Error: Invalid file descriptor " << fd << " for write." << std::endl;
        return -1;
    }

    int bytes_written = file_manager_.writeFile(fd, buffer, length, process_open_file_table_, system_open_file_table_);

    if (bytes_written > 0)
    {
        process_open_file_table_[fd].current_offset += bytes_written;

        int sys_idx = process_open_file_table_[fd].system_table_idx;
        SystemOpenFileEntry &sys_entry = system_open_file_table_[sys_idx];
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        sys_entry.inode_cache.modification_time = now;
        sys_entry.inode_cache.access_time = now;

        inode_manager_.writeInode(sys_entry.inode_id, sys_entry.inode_cache);
    }
    return bytes_written;
}

bool FileSystem::rm(const std::string &path, bool recursive, bool force)
{
    return false;
}

bool FileSystem::cp(const std::string &sourcePath, const std::string &destPath, bool recursive)
{
    return false;
}

bool FileSystem::mv(const std::string &sourcePath, const std::string &destPath)
{
    return false;
}

bool FileSystem::ln(const std::string &targetPath, const std::string &linkPath)
{
    return false;
}

bool FileSystem::chmod(const std::string &path, short mode)
{
    return false;
}

bool FileSystem::chown(const std::string &path, const std::string &newOwnerUsername)
{
    return false;
}

std::vector<std::string> FileSystem::find(const std::string &startPath, const std::string &filename)
{
    return std::vector<std::string>();
}

std::string FileSystem::getCurrentPathPrompt() const
{

    User *currentUser = user_manager_.getCurrentUser();
    std::string username = currentUser ? currentUser->username : "guest";

    return username + "@MyFS:/path/to/current(inode:" + std::to_string(current_dir_inode_id_) + ")";
}

bool FileSystem::create(const std::string &path)
{
    User *currentUser = user_manager_.getCurrentUser();
    if (!currentUser)
    {
        std::cerr << "Error: No user logged in. Cannot create file." << std::endl;
        return false;
    }

    int parentInodeId = INVALID_INODE_ID;
    std::string newFileName;
    int existingInodeId = dir_manager_.resolvePathToInode(path, current_dir_inode_id_, root_dir_inode_id_, currentUser, &parentInodeId, &newFileName, false);

    if (existingInodeId != INVALID_INODE_ID)
    {
        std::cerr << "Error: Path '" << path << "' already exists." << std::endl;
        return false;
    }

    if (parentInodeId == INVALID_INODE_ID || newFileName.empty())
    {
        std::cerr << "Error: Invalid path or cannot determine parent directory for '" << path << "'." << std::endl;
        return false;
    }
    if (newFileName.length() >= MAX_FILENAME_LENGTH)
    {
        std::cerr << "Error: Filename '" << newFileName << "' is too long." << std::endl;
        return false;
    }

    Inode parentDirInode;
    if (!inode_manager_.readInode(parentInodeId, parentDirInode))
    {
        std::cerr << "Error: Could not read parent directory inode." << std::endl;
        return false;
    }

    if (!user_manager_.checkAccessPermission(parentDirInode, PermissionAction::ACTION_WRITE))
    {
        std::cerr << "Error: Permission denied to write in parent directory." << std::endl;
        return false;
    }

    int newFileInodeId = file_manager_.createFileInode(currentUser->uid, DEFAULT_FILE_PERMISSIONS);
    if (newFileInodeId == INVALID_INODE_ID)
    {
        std::cerr << "Error: Failed to create new file inode." << std::endl;
        return false;
    }

    if (!dir_manager_.addEntry(parentDirInode, newFileName, newFileInodeId, FileType::REGULAR_FILE))
    {
        std::cerr << "Error: Failed to add entry to parent directory." << std::endl;

        sb_manager_.freeInode(newFileInodeId);
        return false;
    }

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    parentDirInode.modification_time = now;
    parentDirInode.access_time = now;
    inode_manager_.writeInode(parentInodeId, parentDirInode);

    return true;
}