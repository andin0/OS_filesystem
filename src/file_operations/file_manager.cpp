#include "file_operations/file_manager.h"

FileManager::FileManager(DataBlockManager *dbManager, InodeManager *inodeManager, SuperBlockManager *sbManager, DirectoryManager *dirManager)
    : db_manager_(dbManager), inode_manager_(inodeManager), sb_manager_(sbManager), dir_manager_(dirManager) {}

int FileManager::createFileInode(short ownerUid, short permissions)
{                                               //
    int inodeId = sb_manager_->allocateInode(); //
    if (inodeId == INVALID_INODE_ID)
    {                            //
        return INVALID_INODE_ID; //
    }

    Inode newFileInode;                              //
    newFileInode.inode_id = inodeId;                 //
    newFileInode.file_type = FileType::REGULAR_FILE; //
    newFileInode.permissions = permissions;          //
    newFileInode.owner_uid = ownerUid;               //
    newFileInode.link_count = 1;                     // Starts with one link (when added to a directory)
    newFileInode.file_size = 0;                      //
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    newFileInode.creation_time = now;     //
    newFileInode.modification_time = now; //
    newFileInode.access_time = now;       //

    for (int i = 0; i < NUM_DIRECT_BLOCKS; ++i)
        newFileInode.direct_blocks[i] = INVALID_BLOCK_ID;  //
    newFileInode.single_indirect_block = INVALID_BLOCK_ID; //
    newFileInode.double_indirect_block = INVALID_BLOCK_ID; //

    if (!inode_manager_->writeInode(inodeId, newFileInode))
    {                                    //
        sb_manager_->freeInode(inodeId); //
        return INVALID_INODE_ID;         //
    }
    return inodeId;
}

// processOpenFileTable 和 systemOpenFileTable 作为引用传递，因为 FileManager 会修改它们
int FileManager::openFile(int inodeId, OpenMode mode,                              //
                          std::vector<ProcessOpenFileEntry> &processOpenFileTable, //
                          std::vector<SystemOpenFileEntry> &systemOpenFileTable)
{ //

    // 1. 查找或创建系统打开文件表条目
    int system_idx = -1;
    for (size_t i = 0; i < systemOpenFileTable.size(); ++i)
    {
        if (systemOpenFileTable[i].inode_id == inodeId)
        { //
            system_idx = i;
            break;
        }
    }

    Inode inode_cache_copy; //
    if (!inode_manager_->readInode(inodeId, inode_cache_copy))
    { //
        std::cerr << "FileManager::openFile: Failed to read inode " << inodeId << std::endl;
        return -1; // Indicate failure
    }

    if (system_idx == -1)
    { // Not found in system table, create new entry
        // Find a free slot in systemOpenFileTable or add a new one
        int free_sys_slot = -1;
        for (size_t i = 0; i < systemOpenFileTable.size(); ++i)
        {
            if (systemOpenFileTable[i].inode_id == INVALID_INODE_ID)
            { // Assuming INVALID_INODE_ID marks a free slot
                free_sys_slot = i;
                break;
            }
        }
        if (free_sys_slot == -1)
        { // No free slot, add to end
            if (systemOpenFileTable.size() < MAX_SYSTEM_OPEN_FILES)
            {
                systemOpenFileTable.push_back({}); // Add an empty entry
                free_sys_slot = systemOpenFileTable.size() - 1;
            }
            else
            {
                std::cerr << "FileManager::openFile: System open file table is full." << std::endl;
                return -1;
            }
        }
        system_idx = free_sys_slot;
        systemOpenFileTable[system_idx].inode_id = inodeId;             //
        systemOpenFileTable[system_idx].inode_cache = inode_cache_copy; //
        systemOpenFileTable[system_idx].open_count = 1;                 //
        systemOpenFileTable[system_idx].mode = mode;                    // Store the mode it was first opened with, or most permissive? Usually per-process.
                                                                        // The mode in SystemOpenFileEntry might be more about caching/dirty flags
                                                                        // than strict open mode enforcement (which is per FD).
                                                                        // Let's simplify and assume the mode is relevant for the system entry.
    }
    else
    {                                                 // Found in system table
        systemOpenFileTable[system_idx].open_count++; //
        // Mode compatibility check:
        // If it's already open, and the new mode is incompatible (e.g. trying to open for write something already open for read by another process if exclusivity is desired)
        // This simplistic model doesn't handle complex sharing modes.
        // We can update the cached inode if the disk version is newer (e.g. via modification time)
        // but for now, just use the existing cache or the newly read one.
        // For safety, let's re-assign/update cache to the freshly read one.
        systemOpenFileTable[system_idx].inode_cache = inode_cache_copy; //
    }

    // 处理 MODE_WRITE: 截断文件 (truncate)
    if (mode == OpenMode::MODE_WRITE)
    { //
        // Clear all data blocks associated with this inode
        db_manager_->clearInodeDataBlocks(systemOpenFileTable[system_idx].inode_cache); //
        // Reset file size in cache and on disk
        systemOpenFileTable[system_idx].inode_cache.file_size = 0; //
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        systemOpenFileTable[system_idx].inode_cache.modification_time = now; //
        systemOpenFileTable[system_idx].inode_cache.access_time = now;       //
        if (!inode_manager_->writeInode(inodeId, systemOpenFileTable[system_idx].inode_cache))
        { //
            std::cerr << "FileManager::openFile: Failed to write truncated inode " << inodeId << std::endl;
            // Decrement open count as open failed partially
            systemOpenFileTable[system_idx].open_count--; //
            if (systemOpenFileTable[system_idx].open_count == 0)
                systemOpenFileTable[system_idx].inode_id = INVALID_INODE_ID; //
            return -1;
        }
    }

    // The actual ProcessOpenFileEntry is set up by FileSystem using the returned system_idx.
    // FileSystem finds a free FD, then calls this, then populates its ProcessOpenFileEntry.
    return system_idx;
}

bool FileManager::closeFile(int fd,                                                  //
                            std::vector<ProcessOpenFileEntry> &processOpenFileTable, //
                            std::vector<SystemOpenFileEntry> &systemOpenFileTable)
{ //
    if (fd < 0 || static_cast<size_t>(fd) >= processOpenFileTable.size())
    {
        std::cerr << "FileManager::closeFile: Invalid fd " << fd << std::endl;
        return false;
    }

    int system_idx = processOpenFileTable[fd].system_table_idx; //
    if (system_idx < 0 || static_cast<size_t>(system_idx) >= systemOpenFileTable.size() ||
        systemOpenFileTable[system_idx].inode_id == INVALID_INODE_ID)
    { //
        std::cerr << "FileManager::closeFile: Invalid system_table_idx for fd " << fd << std::endl;
        // fd might have already been closed or was invalid.
        // Mark process table entry as free anyway if FileSystem didn't do it.
        processOpenFileTable[fd].system_table_idx = INVALID_FD; //
        return false;                                           // Or true if we consider "already closed" as success for idempotency.
    }

    SystemOpenFileEntry &sys_entry = systemOpenFileTable[system_idx]; //
    sys_entry.open_count--;                                           //

    if (sys_entry.open_count == 0)
    { //
        // This is the last process closing this file
        // Write back the inode_cache to disk if it's "dirty"
        // (A "dirty" flag isn't in SystemOpenFileEntry, so we might write it back unconditionally,
        // or compare with a disk version, or trust that modifications updated it)
        // For simplicity, let's assume modifications (write, truncate) already wrote it.
        // If only reads happened, access time might need updating.
        // Let's ensure the latest inode_cache is written back.
        if (!inode_manager_->writeInode(sys_entry.inode_id, sys_entry.inode_cache))
        { //
            std::cerr << "FileManager::closeFile: Failed to write back inode " << sys_entry.inode_id << " on final close." << std::endl;
            // This is problematic. The file is closed, but inode state might be lost.
        }
        // Mark the system open file table entry as free
        sys_entry.inode_id = INVALID_INODE_ID; //
        // sys_entry.inode_cache can be cleared too.
    }

    // The FileSystem layer will mark processOpenFileTable[fd] as free.
    // processOpenFileTable[fd].system_table_idx = INVALID_FD; // Done by FileSystem::releaseFd

    return true;
}

int FileManager::readFile(int fd, char *buffer, int length,                              //
                          const std::vector<ProcessOpenFileEntry> &processOpenFileTable, //
                          std::vector<SystemOpenFileEntry> &systemOpenFileTable)
{ // System table is non-const because inode_cache (access time) might be updated. //
    if (fd < 0 || static_cast<size_t>(fd) >= processOpenFileTable.size() || length <= 0)
    {
        return -1; // Invalid arguments
    }
    const ProcessOpenFileEntry &proc_entry = processOpenFileTable[fd]; //
    int system_idx = proc_entry.system_table_idx;                      //

    if (system_idx < 0 || static_cast<size_t>(system_idx) >= systemOpenFileTable.size() ||
        systemOpenFileTable[system_idx].inode_id == INVALID_INODE_ID)
    { //
        std::cerr << "FileManager::readFile: Invalid system_table_idx for fd " << fd << std::endl;
        return -1;
    }

    SystemOpenFileEntry &sys_entry = systemOpenFileTable[system_idx]; //

    // Check open mode permission for reading
    OpenMode current_mode = sys_entry.mode; // This 'mode' in SystemOpenFileEntry might be the mode of the *first* opener.
                                            // A better design would store OpenMode in ProcessOpenFileEntry.
                                            // Let's assume FileSystem::open already put a relevant mode in ProcessOpenFileEntry,
                                            // or that sys_entry.mode is sufficient if we simplify that all openers must be compatible.
                                            // For now, we'll rely on FileSystem to have checked permissions on open.
                                            // A quick check here:
    // if (sys_entry.mode != OpenMode::MODE_READ && sys_entry.mode != OpenMode::MODE_READ_WRITE) {
    //    std::cerr << "FileManager::readFile: File (fd " << fd << ") not opened for reading." << std::endl;
    //    return -1;
    // }

    long long offset = proc_entry.current_offset;                                              //
    int bytes_to_read = std::min((long long)length, sys_entry.inode_cache.file_size - offset); //

    if (bytes_to_read <= 0)
    {
        return 0; // EOF or nothing to read at this offset
    }

    int bytes_read = db_manager_->readFileData(sys_entry.inode_cache, offset, buffer, bytes_to_read); //

    if (bytes_read > 0)
    {
        // Access time is updated by FileSystem::read after this call.
        // proc_entry.current_offset += bytes_read; // This is done by FileSystem::read
    }
    return bytes_read;
}

int FileManager::writeFile(int fd, const char *buffer, int length,                  //
                           std::vector<ProcessOpenFileEntry> &processOpenFileTable, //
                           std::vector<SystemOpenFileEntry> &systemOpenFileTable)
{ //
    if (fd < 0 || static_cast<size_t>(fd) >= processOpenFileTable.size() || length < 0)
    {
        return -1; // Invalid arguments
    }
    if (length == 0)
        return 0; // Nothing to write

    ProcessOpenFileEntry &proc_entry = processOpenFileTable[fd]; // // Non-const for offset
    int system_idx = proc_entry.system_table_idx;                //

    if (system_idx < 0 || static_cast<size_t>(system_idx) >= systemOpenFileTable.size() ||
        systemOpenFileTable[system_idx].inode_id == INVALID_INODE_ID)
    { //
        std::cerr << "FileManager::writeFile: Invalid system_table_idx for fd " << fd << std::endl;
        return -1;
    }

    SystemOpenFileEntry &sys_entry = systemOpenFileTable[system_idx]; //

    // Check open mode permission for writing
    // Similar to readFile, relying on FileSystem layer for initial check.
    // if (sys_entry.mode != OpenMode::MODE_WRITE && sys_entry.mode != OpenMode::MODE_READ_WRITE && sys_entry.mode != OpenMode::MODE_APPEND) {
    //    std::cerr << "FileManager::writeFile: File (fd " << fd << ") not opened for writing/appending." << std::endl;
    //    return -1;
    // }

    long long offset;
    // If append mode was stored per-FD (in ProcessOpenFileEntry), we'd check that.
    // Assuming sys_entry.mode reflects the relevant mode for this FD, or FileSystem handles append logic.
    // For a simple append, offset is always end of file.
    // FileSystem::open set the initial offset for append. Subsequent writes in append mode also go to current EOF.
    if (sys_entry.mode == OpenMode::MODE_APPEND)
    {                                             //
        offset = sys_entry.inode_cache.file_size; // // Write at current end of file
    }
    else
    {
        offset = proc_entry.current_offset; //
    }

    bool size_changed = false;
    int bytes_written = db_manager_->writeFileData(sys_entry.inode_cache, offset, buffer, length, size_changed); //

    if (bytes_written > 0)
    {
        // inode_cache.file_size would have been updated by writeFileData if it changed.
        // Modification and Access times are updated by FileSystem::write after this call.
        // proc_entry.current_offset += bytes_written; // This is done by FileSystem::write

        // If append mode, the process's current_offset should also move to the new EOF
        if (sys_entry.mode == OpenMode::MODE_APPEND)
        { //
          // proc_entry.current_offset = sys_entry.inode_cache.file_size; // FileSystem will handle this.
        }
    }
    return bytes_written;
}

bool FileManager::deleteFileByInode(int inodeId)
{ //
    if (inodeId == INVALID_INODE_ID || inodeId == ROOT_DIRECTORY_INODE_ID)
    { //
        std::cerr << "FileManager::deleteFileByInode: Invalid inodeId for deletion: " << inodeId << std::endl;
        return false;
    }

    Inode inode_to_delete; //
    if (!inode_manager_->readInode(inodeId, inode_to_delete))
    { //
        std::cerr << "FileManager::deleteFileByInode: Failed to read inode " << inodeId << " for deletion." << std::endl;
        // Inode might have already been freed, or disk error.
        // Depending on strictness, this could be an error or a "already deleted" success.
        return false; // Let's be strict.
    }

    // 1. Clear all data blocks associated with the inode
    db_manager_->clearInodeDataBlocks(inode_to_delete); //
    // clearInodeDataBlocks should update the inode_to_delete.direct_blocks etc. to INVALID_BLOCK_ID
    // and potentially save the inode if its state changed regarding blocks.
    // However, since we are about to free the inode, saving it here might be redundant if freeInode doesn't need it.
    // But it's good practice for clearInodeDataBlocks to leave the passed Inode struct consistent.

    // 2. Free the inode number
    sb_manager_->freeInode(inodeId); //

    // Note: This function does NOT handle:
    // - Decrementing link count (FileSystem::rm should do this BEFORE calling this if link_count reaches 0)
    // - Removing the directory entry (DirectoryManager::removeEntry should be called by FileSystem::rm)
    // - Checking if the file is currently open (FileSystem should prevent deletion of open files or handle it)
    // This function is a low-level utility to reclaim resources once higher-level checks are done.

    return true;
}