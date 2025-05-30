#include "all_includes.h"
#include <chrono> // For timestamps
#include <sstream> // For dir output formatting
#include "filesystem.h"

// 构造函数
FileSystem::FileSystem(const std::string &diskFilePath, long long diskSize)
    : vdisk_(diskFilePath, diskSize),                                     //
      sb_manager_(&vdisk_),                                              //
      inode_manager_(&vdisk_, &sb_manager_),                             //
      db_manager_(&vdisk_, &inode_manager_, &sb_manager_),               //
      dir_manager_(&db_manager_, &inode_manager_, &sb_manager_),          //
      file_manager_(&db_manager_, &inode_manager_, &sb_manager_, &dir_manager_), //
      user_manager_(&inode_manager_, &dir_manager_, &sb_manager_),        //
      current_dir_inode_id_(INVALID_INODE_ID),                           //
      root_dir_inode_id_(ROOT_DIRECTORY_INODE_ID) {                      //
    // 可以在这里进行一些初始检查或设置
}

FileSystem::~FileSystem() {
    // 确保所有挂起的操作已完成，例如保存SuperBlock
    sb_manager_.saveSuperBlock();
}

bool FileSystem::mount() { //
    if (!vdisk_.exists()) { //
        std::cout << "Virtual disk file not found. Attempting to create and format..." << std::endl;
        if (!vdisk_.createDiskFile()) { //
            std::cerr << "Failed to create virtual disk file." << std::endl;
            return false;
        }
        if (!format()) {
            std::cerr << "Failed to format the new disk." << std::endl;
            return false;
        }
        std::cout << "New disk created and formatted successfully." << std::endl;
    }

    if (!sb_manager_.loadSuperBlock()) { //
        std::cerr << "Failed to load superblock. Disk might not be formatted or is corrupted." << std::endl;
        std::cout << "Do you want to format the disk? (yes/no): ";
        std::string choice;
        std::cin >> choice;
        if (choice == "yes") {
            if (!format()) {
                std::cerr << "Failed to format the disk." << std::endl;
                return false;
            }
            // 格式化后需要重新加载 superblock
            if (!sb_manager_.loadSuperBlock()) { //
                 std::cerr << "Critical: Failed to load superblock even after formatting." << std::endl;
                 return false;
            }
        } else {
            return false;
        }
    }

    const SuperBlock& sb = sb_manager_.getSuperBlockInfo(); //
    if (sb.magic_number != FILESYSTEM_MAGIC_NUMBER) { //
        std::cerr << "Invalid filesystem magic number. Disk is not a MyFileSystem disk or is corrupted." << std::endl;
        // 同样可以提示格式化
        return false;
    }

    root_dir_inode_id_ = sb.root_dir_inode_idx; //
    current_dir_inode_id_ = root_dir_inode_id_; // 默认当前目录是根目录

    // 初始化用户系统，比如创建root用户（如果还没有）
    // 这部分逻辑可能在 UserManager::initializeUsers 中处理
    if (!user_manager_.initializeUsers()) { //
        std::cerr << "Failed to initialize user system." << std::endl;
        // 这可能是一个严重问题，取决于你的设计，是否允许继续
    }

    std::cout << "File system mounted successfully." << std::endl;
    return true;
}


bool FileSystem::format() { //
    if (!sb_manager_.formatFileSystem(DEFAULT_TOTAL_INODES, DEFAULT_BLOCK_SIZE)) { //
        std::cerr << "Filesystem formatting failed." << std::endl;
        return false;
    }
    // 格式化后，需要重新加载Superblock信息
    if (!sb_manager_.loadSuperBlock()) { //
        std::cerr << "Failed to load superblock after format." << std::endl;
        return false;
    }
    const SuperBlock& sb = sb_manager_.getSuperBlockInfo(); //
    root_dir_inode_id_ = sb.root_dir_inode_idx; //
    current_dir_inode_id_ = root_dir_inode_id_;

    // 创建根目录的 inode
    Inode root_inode;
    root_inode.inode_id = root_dir_inode_id_; //
    root_inode.file_type = FileType::DIRECTORY; //
    root_inode.permissions = DEFAULT_DIR_PERMISSIONS; //
    root_inode.owner_uid = ROOT_UID; // 通常是 root 用户
    root_inode.link_count = 2; // '.' 和 '..' (根目录的 '..' 指向自己)
    root_inode.file_size = 0; // 目录的初始大小
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    root_inode.creation_time = now; //
    root_inode.modification_time = now; //
    root_inode.access_time = now; //
    for(int i=0; i<NUM_DIRECT_BLOCKS; ++i) root_inode.direct_blocks[i] = INVALID_BLOCK_ID; //
    root_inode.single_indirect_block = INVALID_BLOCK_ID; //
    root_inode.double_indirect_block = INVALID_BLOCK_ID; //

    if (!inode_manager_.writeInode(root_dir_inode_id_, root_inode)) { //
        std::cerr << "Failed to write root directory inode." << std::endl;
        return false;
    }

    // 在根目录中添加 "." 和 ".." 条目
    // "." 指向自己
    if (!dir_manager_.addEntry(root_inode, ".", root_dir_inode_id_, FileType::DIRECTORY)) { //
        std::cerr << "Failed to add '.' entry to root directory." << std::endl;
        return false;
    }
    // ".." 指向自己 (对于根目录特殊处理)
    if (!dir_manager_.addEntry(root_inode, "..", root_dir_inode_id_, FileType::DIRECTORY)) { //
        std::cerr << "Failed to add '..' entry to root directory." << std::endl;
        return false;
    }
    // 更新根目录inode的大小和时间戳，因为addEntry会修改它
    if(!inode_manager_.writeInode(root_dir_inode_id_, root_inode)) { //
         std::cerr << "Failed to re-write root directory inode after adding entries." << std::endl;
        return false;
    }


    // 初始化用户管理，例如创建root用户和其家目录
    if (!user_manager_.initializeUsers()) { //
         std::cerr << "Failed to initialize users during format." << std::endl;
         return false;
    }


    if (!sb_manager_.saveSuperBlock()) { //
        std::cerr << "Failed to save superblock after format." << std::endl;
        return false;
    }

    std::cout << "Filesystem formatted successfully." << std::endl;
    return true;
}


bool FileSystem::loginUser(const std::string &username, const std::string &password) { //
    User* user = user_manager_.login(username, password); //
    if (user) {
        // 可选：登录成功后，将当前目录切换到用户的家目录
        Inode homeDirInode;
        if (inode_manager_.readInode(user->home_directory_inode_id, homeDirInode) && homeDirInode.file_type == FileType::DIRECTORY) {
           current_dir_inode_id_ = user->home_directory_inode_id;
        } else {
           current_dir_inode_id_ = root_dir_inode_id_; // 家目录无效则回到根目录
           std::cerr << "Warning: Could not switch to home directory for user " << username << "." << std::endl;
        }
        return true;
    }
    return false;
}

void FileSystem::logoutUser() { //
    user_manager_.logout(); //
    // 登出后，当前目录可以保持不变，或者重置为根目录，取决于设计
    // current_dir_inode_id_ = root_dir_inode_id_;
}

bool FileSystem::mkdir(const std::string &path) { //
    User* currentUser = user_manager_.getCurrentUser(); //
    if (!currentUser) {
        std::cerr << "Error: No user logged in. Cannot create directory." << std::endl;
        return false;
    }

    int parentInodeId = INVALID_INODE_ID; //
    std::string newDirName;
    // 解析路径，获取父目录inode ID和新目录名
    int existingInodeId = dir_manager_.resolvePathToInode(path, current_dir_inode_id_, root_dir_inode_id_, currentUser, &parentInodeId, &newDirName, false); //

    if (existingInodeId != INVALID_INODE_ID) { //
        std::cerr << "Error: Path '" << path << "' already exists." << std::endl;
        return false;
    }

    if (parentInodeId == INVALID_INODE_ID || newDirName.empty()) { //
         std::cerr << "Error: Invalid path or cannot determine parent directory for '" << path << "'." << std::endl;
        return false;
    }
    if (newDirName.length() >= MAX_FILENAME_LENGTH) { //
        std::cerr << "Error: Directory name '" << newDirName << "' is too long." << std::endl;
        return false;
    }


    Inode parentDirInode;
    if (!inode_manager_.readInode(parentInodeId, parentDirInode)) { //
        std::cerr << "Error: Could not read parent directory inode." << std::endl;
        return false;
    }

    // 权限检查：检查当前用户是否对父目录有写权限
    if (!user_manager_.checkAccessPermission(parentDirInode, PermissionAction::ACTION_WRITE)) { //
        std::cerr << "Error: Permission denied to write in parent directory." << std::endl;
        return false;
    }

    // 创建新目录的 inode
    int newDirInodeId = dir_manager_.createDirectoryInode(currentUser->uid, DEFAULT_DIR_PERMISSIONS); //
    if (newDirInodeId == INVALID_INODE_ID) { //
        std::cerr << "Error: Failed to create new directory inode." << std::endl;
        return false;
    }

    Inode newDirInode;
    if (!inode_manager_.readInode(newDirInodeId, newDirInode)) { //
        std::cerr << "Error: Failed to read newly created directory inode." << std::endl;
        // 需要回滚，释放已分配的 newDirInodeId
        sb_manager_.freeInode(newDirInodeId); //
        return false;
    }


    // 在父目录中添加新目录的条目
    if (!dir_manager_.addEntry(parentDirInode, newDirName, newDirInodeId, FileType::DIRECTORY)) { //
        std::cerr << "Error: Failed to add entry to parent directory." << std::endl;
        // 回滚：释放新目录inode和其可能分配的数据块(createDirectoryInode中处理)
        // file_manager_.deleteFileByInode(newDirInodeId); // 或者更底层的清理
        db_manager_.clearInodeDataBlocks(newDirInode); //
        sb_manager_.freeInode(newDirInodeId); //
        return false;
    }
    // 更新父目录的修改时间和访问时间
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    parentDirInode.modification_time = now; //
    parentDirInode.access_time = now; //
    if (!inode_manager_.writeInode(parentInodeId, parentDirInode)) { //
        std::cerr << "Warning: Failed to update parent directory timestamps." << std::endl;
        // 虽然失败，但目录已创建，可以不返回false，或者记录日志
    }


    // 在新目录中添加 "." 和 ".." 条目
    if (!dir_manager_.addEntry(newDirInode, ".", newDirInodeId, FileType::DIRECTORY)) { //
        // 严重错误，清理并返回失败
    }
    if (!dir_manager_.addEntry(newDirInode, "..", parentInodeId, FileType::DIRECTORY)) { //
       // 严重错误，清理并返回失败
    }
    // newDirInode 的 link_count 应该在 createDirectoryInode 中被设置为2 (for . and .. from parent)
    // 或者在 addEntry(".") 和 addEntry("..") 之后手动调整和写回 newDirInode
    newDirInode.link_count = 2; // for "." and its own entry in parent
    // 父目录因为这个新目录的 ".." 指向它，link_count 也需要增加
    parentDirInode.link_count++;
    inode_manager_.writeInode(parentInodeId, parentDirInode); //
    inode_manager_.writeInode(newDirInodeId, newDirInode); //


    return true;
}

bool FileSystem::chdir(const std::string &path) { //
    User* currentUser = user_manager_.getCurrentUser(); //
    if (!currentUser) {
        std::cerr << "Error: No user logged in." << std::endl;
        return false;
    }

    int targetInodeId = dir_manager_.resolvePathToInode(path, current_dir_inode_id_, root_dir_inode_id_, currentUser); //

    if (targetInodeId == INVALID_INODE_ID) { //
        std::cerr << "Error: Path '" << path << "' not found or invalid." << std::endl;
        return false;
    }

    Inode targetInode;
    if (!inode_manager_.readInode(targetInodeId, targetInode)) { //
        std::cerr << "Error: Could not read target inode." << std::endl;
        return false;
    }

    if (targetInode.file_type != FileType::DIRECTORY) { //
        std::cerr << "Error: '" << path << "' is not a directory." << std::endl;
        return false;
    }

    // 权限检查：检查对目标目录是否有执行权限 (cd 通常需要 x 权限)
    if (!user_manager_.checkAccessPermission(targetInode, PermissionAction::ACTION_EXECUTE)) { //
        std::cerr << "Error: Permission denied to enter directory '" << path << "'." << std::endl;
        return false;
    }

    current_dir_inode_id_ = targetInodeId;
    return true;
}


std::string FileSystem::dir(const std::string &path) { //
    User* currentUser = user_manager_.getCurrentUser(); //
     if (!currentUser) {
        return "Error: No user logged in.\n";
    }

    int targetDirInodeId = current_dir_inode_id_; // 默认是当前目录
    if (path != "." && !path.empty()) { // 如果指定了路径
        targetDirInodeId = dir_manager_.resolvePathToInode(path, current_dir_inode_id_, root_dir_inode_id_, currentUser); //
        if (targetDirInodeId == INVALID_INODE_ID) { //
            return "Error: Path '" + path + "' not found or invalid.\n";
        }
    }

    Inode dirInode;
    if (!inode_manager_.readInode(targetDirInodeId, dirInode)) { //
        return "Error: Could not read directory inode.\n";
    }

    if (dirInode.file_type != FileType::DIRECTORY) { //
        return "Error: '" + path + "' is not a directory.\n";
    }

    // 权限检查：检查对目录是否有读权限
    if (!user_manager_.checkAccessPermission(dirInode, PermissionAction::ACTION_READ)) { //
        return "Error: Permission denied to read directory '" + path + "'.\n";
    }

    std::vector<DirectoryEntry> entries = dir_manager_.listEntries(dirInode); //
    std::ostringstream oss;
    oss << "Contents of directory '" << path << "':\n";
    oss << "Type  Perms Link  UID   Size      Name\n";
    oss << "--------------------------------------------\n";

    for (const auto& entry : entries) {
        Inode entryInode;
        if (inode_manager_.readInode(entry.inode_id, entryInode)) { //
            oss << (entryInode.file_type == FileType::DIRECTORY ? "d" : "f") << "     "; //
            // 格式化权限 (例如，rwxr-xr-x)
            oss << ((entryInode.permissions & PERM_USER_READ) ? "r" : "-") //
                << ((entryInode.permissions & PERM_USER_WRITE) ? "w" : "-") //
                << ((entryInode.permissions & PERM_USER_EXEC) ? "x" : "-") //
                << ((entryInode.permissions & PERM_GROUP_READ) ? "r" : "-") //
                // ... (GROUP_WRITE, GROUP_EXEC, OTHER_READ, OTHER_WRITE, OTHER_EXEC)
                << "--- "; // 简化，仅显示用户权限
            oss.width(4); oss << std::left << entryInode.link_count << " "; //
            oss.width(4); oss << std::left << entryInode.owner_uid << " "; //
            oss.width(8); oss << std::right << entryInode.file_size << "  "; //
            oss << entry.filename << "\n"; //
        }
    }
    return oss.str();
}


// Helper to get a free file descriptor
int FileSystem::getFreeFd() { //
    for (int i = 0; i < process_open_file_table_.size(); ++i) { //
        if (process_open_file_table_[i].system_table_idx == INVALID_FD) { //
            return i; // Found a free slot
        }
    }
    // If no free slot in existing table, try to expand (if allowed by a max limit)
    // For simplicity, let's assume a fixed size or error if full.
    // Or, if using vector like this, we can just add a new one.
    if (process_open_file_table_.size() < MAX_OPEN_FILES_PER_PROCESS) { // MAX_OPEN_FILES_PER_PROCESS (你需要定义这个)
       process_open_file_table_.push_back({INVALID_FD, 0}); //
       return process_open_file_table_.size() - 1;
    }
    return INVALID_FD; //
}

void FileSystem::releaseFd(int fd) { //
    if (fd >= 0 && fd < process_open_file_table_.size()) { //
        process_open_file_table_[fd].system_table_idx = INVALID_FD; //
        process_open_file_table_[fd].current_offset = 0; //
    }
}


int FileSystem::open(const std::string &path, OpenMode mode) { //
    User* currentUser = user_manager_.getCurrentUser(); //
    if (!currentUser) {
        std::cerr << "Error: No user logged in." << std::endl;
        return INVALID_FD; //
    }

    int fileInodeId = dir_manager_.resolvePathToInode(path, current_dir_inode_id_, root_dir_inode_id_, currentUser); //

    // 如果文件不存在且模式不是创建模式 (w, a)
    if (fileInodeId == INVALID_INODE_ID) { //
        if (mode == OpenMode::MODE_WRITE || mode == OpenMode::MODE_APPEND ||
            mode == OpenMode::MODE_READ_WRITE /* r+ typically requires existence, w+ would create */) {
            // 文件不存在，尝试创建 (create() 方法更适合处理路径和父目录)
            // 或者，open可以直接创建。这里假设如果open要创建，它需要知道父目录。
            // 为简化，我们先调用 create() 来确保文件存在，然后再尝试打开。
            // 或者，让 FileManager::openFile 处理创建逻辑。
            // 当前 FileManager::openFile 仅接收 inodeId，不处理创建。
            // 因此，需要先确保文件存在，或扩展 FileManager::openFile。

            // 暂时的简化：如果文件不存在并且不是只读模式，则先调用create
            if (mode != OpenMode::MODE_READ) { //
                if (!create(path)) { //
                    std::cerr << "Error: Failed to create file '" << path << "' for opening." << std::endl;
                    return INVALID_FD; //
                }
                // 再次解析以获取新创建文件的inodeId
                fileInodeId = dir_manager_.resolvePathToInode(path, current_dir_inode_id_, root_dir_inode_id_, currentUser); //
                if (fileInodeId == INVALID_INODE_ID) { //
                     std::cerr << "Error: Could not find file '" << path << "' even after attempting creation." << std::endl;
                    return INVALID_FD; //
                }
            } else {
                 std::cerr << "Error: File '" << path << "' not found and mode is read-only." << std::endl;
                return INVALID_FD; //
            }
        } else {
            std::cerr << "Error: File '" << path << "' not found." << std::endl;
            return INVALID_FD; //
        }
    }


    Inode fileInode;
    if (!inode_manager_.readInode(fileInodeId, fileInode)) { //
        std::cerr << "Error: Could not read file inode for '" << path << "'." << std::endl;
        return INVALID_FD; //
    }

    if (fileInode.file_type == FileType::DIRECTORY) { //
        std::cerr << "Error: Cannot open directory '" << path << "' with open command. Use cd/ls." << std::endl;
        return INVALID_FD; //
    }

    // 权限检查
    PermissionAction action; //
    if (mode == OpenMode::MODE_READ) action = PermissionAction::ACTION_READ; //
    else if (mode == OpenMode::MODE_WRITE || mode == OpenMode::MODE_APPEND) action = PermissionAction::ACTION_WRITE; //
    else if (mode == OpenMode::MODE_READ_WRITE) { //
        // 需要同时检查读和写权限
        if (!user_manager_.checkAccessPermission(fileInode, PermissionAction::ACTION_READ) || //
            !user_manager_.checkAccessPermission(fileInode, PermissionAction::ACTION_WRITE)) { //
            std::cerr << "Error: Permission denied for read/write on '" << path << "'." << std::endl;
            return INVALID_FD; //
        }
        // 如果上面通过，下面checkAccessPermission会再次检查，但没关系
    }
    // 对于 r+ 等模式，可能需要多次检查
    if (mode == OpenMode::MODE_READ_WRITE) { //
        if (!user_manager_.checkAccessPermission(fileInode, PermissionAction::ACTION_READ) || !user_manager_.checkAccessPermission(fileInode, PermissionAction::ACTION_WRITE)) { //
             std::cerr << "Error: Permission denied for file '" << path << "' with mode r+." << std::endl;
             return INVALID_FD; //
        }
    } else { // 对于 READ, WRITE, APPEND 单独检查
        if (mode == OpenMode::MODE_READ && !user_manager_.checkAccessPermission(fileInode, PermissionAction::ACTION_READ)) { //
            std::cerr << "Error: Read permission denied for file '" << path << "'." << std::endl;
            return INVALID_FD; //
        }
        if ((mode == OpenMode::MODE_WRITE || mode == OpenMode::MODE_APPEND) && !user_manager_.checkAccessPermission(fileInode, PermissionAction::ACTION_WRITE)) { //
            std::cerr << "Error: Write permission denied for file '" << path << "'." << std::endl;
            return INVALID_FD; //
        }
    }


    int fd = getFreeFd();
    if (fd == INVALID_FD) { //
        std::cerr << "Error: Too many files open by process." << std::endl;
        return INVALID_FD; //
    }

    int system_table_idx = file_manager_.openFile(fileInodeId, mode, process_open_file_table_, system_open_file_table_); //

    if (system_table_idx == -1) { // FileManager::openFile 失败
        releaseFd(fd); // 归还刚分配的fd
        // FileManager::openFile 内部应该已经打印了错误信息
        return INVALID_FD; //
    }

    process_open_file_table_[fd].system_table_idx = system_table_idx; //
    if (mode == OpenMode::MODE_APPEND) { //
        // 对于追加模式，将指针移到文件末尾
        process_open_file_table_[fd].current_offset = fileInode.file_size; //
    } else {
        process_open_file_table_[fd].current_offset = 0; //
    }

    // 更新访问时间 (如果是读或读写模式)
    if (mode == OpenMode::MODE_READ || mode == OpenMode::MODE_READ_WRITE) { //
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        fileInode.access_time = now; //
        inode_manager_.writeInode(fileInodeId, fileInode); //
    }


    return fd;
}

bool FileSystem::close(int fd) { //
    if (fd < 0 || fd >= process_open_file_table_.size() || process_open_file_table_[fd].system_table_idx == INVALID_FD) { //
        std::cerr << "Error: Invalid file descriptor " << fd << "." << std::endl;
        return false;
    }

    if (file_manager_.closeFile(fd, process_open_file_table_, system_open_file_table_)) { //
        releaseFd(fd); // 标记进程级fd为空闲
        return true;
    }
    std::cerr << "Error: Failed to close file with fd " << fd << "." << std::endl;
    return false;
}

int FileSystem::read(int fd, char *buffer, int length) { //
    if (fd < 0 || fd >= process_open_file_table_.size() || process_open_file_table_[fd].system_table_idx == INVALID_FD) { //
        std::cerr << "Error: Invalid file descriptor " << fd << " for read." << std::endl;
        return -1;
    }
    // FileManager::readFile 会处理权限和偏移
    int bytes_read = file_manager_.readFile(fd, buffer, length, process_open_file_table_, system_open_file_table_); //

    if (bytes_read > 0) {
        process_open_file_table_[fd].current_offset += bytes_read; //
        // 更新系统打开文件表中的inode的访问时间 (或者由FileManager处理)
        int sys_idx = process_open_file_table_[fd].system_table_idx; //
        SystemOpenFileEntry& sys_entry = system_open_file_table_[sys_idx]; //
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        sys_entry.inode_cache.access_time = now; //
        inode_manager_.writeInode(sys_entry.inode_id, sys_entry.inode_cache); //
    }
    return bytes_read;
}

int FileSystem::write(int fd, const char *buffer, int length) { //
    if (fd < 0 || fd >= process_open_file_table_.size() || process_open_file_table_[fd].system_table_idx == INVALID_FD) { //
        std::cerr << "Error: Invalid file descriptor " << fd << " for write." << std::endl;
        return -1;
    }

    int bytes_written = file_manager_.writeFile(fd, buffer, length, process_open_file_table_, system_open_file_table_); //

    if (bytes_written > 0) {
        process_open_file_table_[fd].current_offset += bytes_written; //
        // 更新系统打开文件表中的inode的修改时间和访问时间 (或者由FileManager处理)
        int sys_idx = process_open_file_table_[fd].system_table_idx; //
        SystemOpenFileEntry& sys_entry = system_open_file_table_[sys_idx]; //
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        sys_entry.inode_cache.modification_time = now; //
        sys_entry.inode_cache.access_time = now; //
        // 文件大小可能已改变，writeFile 应该更新 inode_cache.file_size
        // 并通过 inode_manager_.writeInode 写回磁盘
        inode_manager_.writeInode(sys_entry.inode_id, sys_entry.inode_cache); //
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

std::string FileSystem::getCurrentPathPrompt() const { //
    // 这个函数需要从 current_dir_inode_id_ 反向解析出完整路径字符串
    // 这可能比较复杂，需要从当前目录 inode 向上回溯到根目录
    // 暂时返回一个简化的提示符
    User* currentUser = user_manager_.getCurrentUser(); //
    std::string username = currentUser ? currentUser->username : "guest"; //
    // 实际路径的获取需要一个辅助函数，比如在DirectoryManager中:
    // std::string path_str = dir_manager_.getPathString(current_dir_inode_id_, root_dir_inode_id_);
    // return username + "@MyFS:" + path_str;

    // 简化版：
    return username + "@MyFS:/path/to/current(inode:" + std::to_string(current_dir_inode_id_) +")";
}


// ... 其他 FileSystem 方法的实现 ...
// 例如：create, rm, cp, mv, ln, chmod, chown, find
// 这些方法都会涉及到路径解析、权限检查、调用相应的 Manager 完成具体操作

bool FileSystem::create(const std::string &path) { //
    User* currentUser = user_manager_.getCurrentUser(); //
    if (!currentUser) {
        std::cerr << "Error: No user logged in. Cannot create file." << std::endl;
        return false;
    }

    int parentInodeId = INVALID_INODE_ID; //
    std::string newFileName;
    int existingInodeId = dir_manager_.resolvePathToInode(path, current_dir_inode_id_, root_dir_inode_id_, currentUser, &parentInodeId, &newFileName, false); //

    if (existingInodeId != INVALID_INODE_ID) { //
        std::cerr << "Error: Path '" << path << "' already exists." << std::endl;
        return false;
    }

    if (parentInodeId == INVALID_INODE_ID || newFileName.empty()) { //
        std::cerr << "Error: Invalid path or cannot determine parent directory for '" << path << "'." << std::endl;
        return false;
    }
     if (newFileName.length() >= MAX_FILENAME_LENGTH) { //
        std::cerr << "Error: Filename '" << newFileName << "' is too long." << std::endl;
        return false;
    }


    Inode parentDirInode;
    if (!inode_manager_.readInode(parentInodeId, parentDirInode)) { //
        std::cerr << "Error: Could not read parent directory inode." << std::endl;
        return false;
    }

    if (!user_manager_.checkAccessPermission(parentDirInode, PermissionAction::ACTION_WRITE)) { //
        std::cerr << "Error: Permission denied to write in parent directory." << std::endl;
        return false;
    }

    // 创建文件 inode
    int newFileInodeId = file_manager_.createFileInode(currentUser->uid, DEFAULT_FILE_PERMISSIONS); //
    if (newFileInodeId == INVALID_INODE_ID) { //
        std::cerr << "Error: Failed to create new file inode." << std::endl;
        return false;
    }

    // 在父目录中添加条目
    if (!dir_manager_.addEntry(parentDirInode, newFileName, newFileInodeId, FileType::REGULAR_FILE)) { //
        std::cerr << "Error: Failed to add entry to parent directory." << std::endl;
        // 回滚：释放新文件inode (createFileInode只分配inode，不分配数据块)
        sb_manager_.freeInode(newFileInodeId); //
        return false;
    }

    // 更新父目录的时间戳
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    parentDirInode.modification_time = now; //
    parentDirInode.access_time = now; //
    inode_manager_.writeInode(parentInodeId, parentDirInode); //

    return true;
}