#include "all_includes.h"
#include <chrono>
#include <cstring> // For strncpy, strcmp

DirectoryManager::DirectoryManager(DataBlockManager *dbManager, InodeManager *inodeManager, SuperBlockManager *sbManager)
    : db_manager_(dbManager), inode_manager_(inodeManager), sb_manager_(sbManager) {}

// 简化版的路径解析，实际需要更完善的错误处理和细节
// parentInodeId 和 lastName 是输出参数
int DirectoryManager::resolvePathToInode(const std::string &path, int currentDirInodeId, int rootDirInodeId, const User *currentUser,
                                       int *outParentInodeId, std::string *outLastName, bool followLastLink) { //
    if (path.empty()) {
        return INVALID_INODE_ID; //
    }

    std::vector<std::string> segments;
    std::string segment;
    std::istringstream segment_stream(path);
    bool startsWithSlash = (!path.empty() && path[0] == '/');

    // 分割路径
    // 处理开头的 '/' (如果非空)
    if (startsWithSlash) {
       // 如果路径就是 "/"
       if (path.length() == 1) {
           if (outParentInodeId) *outParentInodeId = rootDirInodeId; // 根目录的父目录也是根目录（特殊情况）
           if (outLastName) *outLastName = "/"; // 或者 "."
           return rootDirInodeId;
       }
       // 跳过开头的 '/'
       if (segment_stream.peek() == '/') segment_stream.ignore();
    }

    while (std::getline(segment_stream, segment, '/')) {
        if (!segment.empty()) {
            segments.push_back(segment);
        }
    }
    // 如果路径以 / 结尾且不是根目录本身，例如 "/usr/bin/"，最后一个segment会是 ""，需要处理
    // 但getline默认行为是如果最后是分隔符，不会产生空segment，除非连续分隔符 "a//b"
    if (path.length() > 1 && path.back() == '/' && !segments.empty()) {
        // 如果最后一个有效segment后跟了'/'，则最后一个segment是目录名
        // 如果是 "a/b/" 那么 segments 是 {"a", "b"}
    }


    int currentInodeId = startsWithSlash ? rootDirInodeId : currentDirInodeId;
    int parentId = startsWithSlash ? rootDirInodeId : currentDirInodeId; // 父目录初始值

    if (segments.empty()) { // e.g. path is "/" or "." or "" relative
        if (path == "/") {
            if (outParentInodeId) *outParentInodeId = rootDirInodeId; // or some other convention for root's parent
            if (outLastName) *outLastName = "."; // Or "/"
            return rootDirInodeId;
        }
        if (path == ".") {
            if (outParentInodeId) *outParentInodeId = currentDirInodeId; // or its actual parent if needed
            if (outLastName) *outLastName = ".";
            return currentDirInodeId;
        }
        if (path == ".." ) {
            // Find parent of currentDirInodeId
            Inode currentInodeObj;
            if (!inode_manager_->readInode(currentDirInodeId, currentInodeObj)) return INVALID_INODE_ID; //
            int dotdotInodeId = findEntry(currentInodeObj, ".."); //
            if(outParentInodeId && dotdotInodeId != INVALID_INODE_ID) { //
                Inode dotdotInodeObj;
                if(inode_manager_->readInode(dotdotInodeId, dotdotInodeObj)) { //
                     *outParentInodeId = findEntry(dotdotInodeObj, ".."); // ".." of ".."
                } else {
                    *outParentInodeId = INVALID_INODE_ID; //
                }
            }
            if (outLastName) *outLastName = "..";
            return dotdotInodeId;

        }
        // Path was empty or just a relative specifier that didn't change directory
        if (outParentInodeId) *outParentInodeId = parentId; // this might need careful thought
        if (outLastName && !path.empty()) *outLastName = path;
        else if (outLastName) *outLastName = ".";
        return currentInodeId;
    }


    for (size_t i = 0; i < segments.size(); ++i) {
        const std::string &name = segments[i];
        if (name.length() >= MAX_FILENAME_LENGTH) return INVALID_INODE_ID; //


        Inode dirInode;
        if (!inode_manager_->readInode(currentInodeId, dirInode)) { //
            return INVALID_INODE_ID; //
        }

        if (dirInode.file_type != FileType::DIRECTORY) { //
            // Not a directory, but path continues
            return INVALID_INODE_ID; //
        }

        // 权限检查：是否可以访问/执行此目录 (除了最后一部分)
        // UserManager* um = ... (需要通过某种方式获取UserManager实例，或者FileSystem传给它)
        // if (i < segments.size() -1 || (followLastLink && segments.back_is_link_and_points_to_dir))
        // if (!um->checkAccessPermission(dirInode, PermissionAction::ACTION_EXECUTE, currentUser)) {
        //     return INVALID_INODE_ID;
        // }
        // 简化：此处不直接进行权限检查，由调用者（FileSystem）在合适时机进行

        parentId = currentInodeId;
        currentInodeId = findEntry(dirInode, name); //

        if (currentInodeId == INVALID_INODE_ID) { //
            // 如果是路径的最后一部分，且我们不需要它必须存在 (例如 mkdir)
            if (i == segments.size() - 1 && outParentInodeId && outLastName) {
                *outParentInodeId = parentId;
                *outLastName = name;
                return INVALID_INODE_ID; // 表示目标本身不存在，但父路径有效
            }
            return INVALID_INODE_ID; // 中间路径无效
        }

        // TODO: 处理符号链接 (if followLastLink is true or if it's not the last segment)
        // Inode resolvedInode;
        // inode_manager_->readInode(currentInodeId, resolvedInode);
        // if (resolvedInode.file_type == FileType::SYMLINK && (followLastLink || i < segments.size() - 1)) {
        //    std::string targetPath = ... // read symlink content
        //    currentInodeId = resolvePathToInode(targetPath, parentId /*or root if absolute link*/, rootDirInodeId, currentUser);
        //    if (currentInodeId == INVALID_INODE_ID) return INVALID_INODE_ID;
        // }
    }

    if (outParentInodeId) *outParentInodeId = parentId;
    if (outLastName) *outLastName = segments.back();
    return currentInodeId;
}


bool DirectoryManager::addEntry(Inode &parentDirInode, const std::string &name, int entryInodeId, FileType type) { //
    if (name.length() >= MAX_FILENAME_LENGTH) { //
        std::cerr << "Error: Filename '" << name << "' is too long." << std::endl;
        return false;
    }
     if (parentDirInode.file_type != FileType::DIRECTORY) { //
        std::cerr << "Error: Parent inode is not a directory." << std::endl;
        return false;
    }

    DirectoryEntry newEntry; //
    strncpy(newEntry.filename, name.c_str(), MAX_FILENAME_LENGTH -1); //
    newEntry.filename[MAX_FILENAME_LENGTH - 1] = '\0'; // 确保 null 结尾
    newEntry.inode_id = entryInodeId; //

    // 遍历父目录的数据块，查找空位或追加
    // Directory entries are stored one after another in data blocks.
    // Each data block can hold block_size / sizeof(DirectoryEntry) entries.

    char blockBuffer[DEFAULT_BLOCK_SIZE]; //
    bool entryWritten = false;
    long long dirSize = parentDirInode.file_size; //
    int entriesPerBlock = DEFAULT_BLOCK_SIZE / sizeof(DirectoryEntry); //

    // 检查现有块是否有空位 (inode_id == INVALID_INODE_ID)
    for (int i = 0; i < NUM_DIRECT_BLOCKS; ++i) { //
        if (parentDirInode.direct_blocks[i] == INVALID_BLOCK_ID) continue; //
        if (!db_manager_->vdisk_->readBlock(parentDirInode.direct_blocks[i], blockBuffer, DEFAULT_BLOCK_SIZE)) { //
            std::cerr << "Error reading directory block " << parentDirInode.direct_blocks[i] << std::endl;
            continue;
        }
        DirectoryEntry* entries = reinterpret_cast<DirectoryEntry*>(blockBuffer); //
        for (int j = 0; j < entriesPerBlock; ++j) {
            if (entries[j].inode_id == INVALID_INODE_ID) { //
                entries[j] = newEntry;
                if (!db_manager_->vdisk_->writeBlock(parentDirInode.direct_blocks[i], blockBuffer, DEFAULT_BLOCK_SIZE)) { //
                    std::cerr << "Error writing to directory block " << parentDirInode.direct_blocks[i] << std::endl;
                    return false; // Critical error
                }
                entryWritten = true;
                break;
            }
        }
        if (entryWritten) break;
    }
    // TODO: Handle indirect blocks if needed

    if (!entryWritten) {
        // 没有空位，需要分配新块或在最后一个块的末尾追加（如果空间足够）
        int currentBlockIndex = (dirSize / DEFAULT_BLOCK_SIZE); //
        int offsetInBlock = (dirSize % DEFAULT_BLOCK_SIZE); //

        if (offsetInBlock == 0 || currentBlockIndex >= NUM_DIRECT_BLOCKS /* simplistic */) {
             // 需要新的数据块
            if (currentBlockIndex >= NUM_DIRECT_BLOCKS) { //
                // TODO: Implement indirect blocks for directory
                std::cerr << "Directory full (direct blocks only implemented for addEntry)." << std::endl;
                return false;
            }
            int newBlockId = sb_manager_->allocateBlock(); //
            if (newBlockId == INVALID_BLOCK_ID) { //
                std::cerr << "Failed to allocate block for directory entry." << std::endl;
                return false;
            }
            parentDirInode.direct_blocks[currentBlockIndex] = newBlockId; //
            memset(blockBuffer, 0, DEFAULT_BLOCK_SIZE); // // 清零新块
            offsetInBlock = 0; // 从新块的开始写
        } else {
            // 当前块还有空间，读取它
            if (!db_manager_->vdisk_->readBlock(parentDirInode.direct_blocks[currentBlockIndex-1], blockBuffer, DEFAULT_BLOCK_SIZE)) { //
                 std::cerr << "Error reading directory block for append." << std::endl;
                return false;
            }
        }

        DirectoryEntry* entries = reinterpret_cast<DirectoryEntry*>(blockBuffer); //
        int entryIndexInBlock = offsetInBlock / sizeof(DirectoryEntry);
        if (entryIndexInBlock < entriesPerBlock) {
            entries[entryIndexInBlock] = newEntry;
             if (!db_manager_->vdisk_->writeBlock(parentDirInode.direct_blocks[currentBlockIndex == 0 ? 0 : currentBlockIndex -1], blockBuffer, DEFAULT_BLOCK_SIZE)) { //
                 std::cerr << "Error writing new entry to directory block." << std::endl;
                // TODO: should free allocated block if this was a new block
                return false;
            }
            entryWritten = true;
            parentDirInode.file_size += sizeof(DirectoryEntry); //
        } else {
            // Should not happen if logic is correct (new block was allocated if needed)
            std::cerr << "Internal error: No space in block even after allocation/check." << std::endl;
            return false;
        }
    }


    if (!entryWritten) {
         std::cerr << "Failed to find space or write directory entry for " << name << std::endl;
        return false;
    }

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    parentDirInode.modification_time = now; //
    parentDirInode.access_time = now; //
    // 父目录的 link_count 可能会因为子目录的 ".." 而改变，但这通常在创建子目录的 ".." 时处理
    // 或者，如果添加的是目录，子目录的 ".." 会指向父目录，父目录的 link_count 增加。
    // 如果添加的是文件，父目录 link_count 不变。
    // 此处 addEntry 不直接修改 link_count，由调用者（如 mkdir）处理。

    // 写回父目录inode
    if (!inode_manager_->writeInode(parentDirInode.inode_id, parentDirInode)) { //
        std::cerr << "Failed to write parent directory inode after adding entry." << std::endl;
        // TODO: Potential inconsistency, might need to revert the write of DirectoryEntry
        return false;
    }
    return true;
}


int DirectoryManager::findEntry(Inode &dirInode, const std::string &name) { //
    if (dirInode.file_type != FileType::DIRECTORY) { //
        return INVALID_INODE_ID; //
    }
    if (name.length() >= MAX_FILENAME_LENGTH) return INVALID_INODE_ID; //


    char blockBuffer[DEFAULT_BLOCK_SIZE]; //
    DirectoryEntry* entries = reinterpret_cast<DirectoryEntry*>(blockBuffer); //
    int entriesPerBlock = DEFAULT_BLOCK_SIZE / sizeof(DirectoryEntry); //
    long long dirEntriesCount = dirInode.file_size / sizeof(DirectoryEntry); //
    long long entriesRead = 0;


    // 遍历直接块
    for (int i = 0; i < NUM_DIRECT_BLOCKS && entriesRead < dirEntriesCount; ++i) { //
        int blockId = dirInode.direct_blocks[i]; //
        if (blockId == INVALID_BLOCK_ID) continue; //

        if (!db_manager_->vdisk_->readBlock(blockId, blockBuffer, DEFAULT_BLOCK_SIZE)) { //
            // Error reading block
            return INVALID_INODE_ID; //
        }

        for (int j = 0; j < entriesPerBlock && entriesRead < dirEntriesCount; ++j) {
            if (entries[j].inode_id != INVALID_INODE_ID && strncmp(entries[j].filename, name.c_str(), MAX_FILENAME_LENGTH) == 0) { //
                return entries[j].inode_id; //
            }
            entriesRead++;
        }
    }
    // TODO: 遍历间接块
    return INVALID_INODE_ID; //
}


std::vector<DirectoryEntry> DirectoryManager::listEntries(Inode &dirInode) { //
    std::vector<DirectoryEntry> result; //
    if (dirInode.file_type != FileType::DIRECTORY) { //
        return result; // Empty list
    }

    char blockBuffer[DEFAULT_BLOCK_SIZE]; //
    DirectoryEntry* entries = reinterpret_cast<DirectoryEntry*>(blockBuffer); //
    int entriesPerBlock = DEFAULT_BLOCK_SIZE / sizeof(DirectoryEntry); //
    long long dirEntriesCount = dirInode.file_size / sizeof(DirectoryEntry); //
    long long entriesCollected = 0;

    // 遍历直接块
    for (int i = 0; i < NUM_DIRECT_BLOCKS && entriesCollected < dirEntriesCount; ++i) { //
        int blockId = dirInode.direct_blocks[i]; //
        if (blockId == INVALID_BLOCK_ID) continue; //

        if (!db_manager_->vdisk_->readBlock(blockId, blockBuffer, DEFAULT_BLOCK_SIZE)) { //
            // Error reading block, skip or report
            continue;
        }

        for (int j = 0; j < entriesPerBlock && entriesCollected < dirEntriesCount; ++j) {
            if (entries[j].inode_id != INVALID_INODE_ID) { //
                result.push_back(entries[j]);
            }
            entriesCollected++;
        }
    }
    // TODO: 遍历间接块
    return result;
}

int DirectoryManager::createDirectoryInode(short ownerUid, short permissions) { //
    int inodeId = sb_manager_->allocateInode(); //
    if (inodeId == INVALID_INODE_ID) { //
        return INVALID_INODE_ID; //
    }

    Inode newDirInode; //
    newDirInode.inode_id = inodeId; //
    newDirInode.file_type = FileType::DIRECTORY; //
    newDirInode.permissions = permissions; //
    newDirInode.owner_uid = ownerUid; //
    newDirInode.link_count = 0; // Initial: will be incremented by "." and when added to parent's ".."
                                // Or, conventionally, directories start with link_count = 2 (for "." and the entry in parent)
                                // Let's assume it starts at 0 and FileSystem::mkdir will manage it.
                                // Or, more simply, set it to 1 initially (for its future entry in parent).
                                // The "." and ".." entries reference inodes, they don't modify parent's link count for themselves
                                // A directory's link count is:
                                // 1 (for its name in the parent directory)
                                // + 1 (for its own "." entry)
                                // + N (for each subdirectory within it, due to their ".." entries pointing back to it)
                                // So a new, empty directory usually has link_count = 2 (from parent, and its own ".")
                                // Let's set to 2, assuming "." will be added and it will be linked from a parent.
    newDirInode.link_count = 2; // For its entry in parent and its own "."
    newDirInode.file_size = 0; // Initially empty, will grow as entries like "." and ".." are added
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    newDirInode.creation_time = now; //
    newDirInode.modification_time = now; //
    newDirInode.access_time = now; //

    for(int i=0; i < NUM_DIRECT_BLOCKS; ++i) newDirInode.direct_blocks[i] = INVALID_BLOCK_ID; //
    newDirInode.single_indirect_block = INVALID_BLOCK_ID; //
    newDirInode.double_indirect_block = INVALID_BLOCK_ID; //

    if (!inode_manager_->writeInode(inodeId, newDirInode)) { //
        sb_manager_->freeInode(inodeId); //
        return INVALID_INODE_ID; //
    }
    return inodeId;
}


bool DirectoryManager::removeEntry(Inode &parentDirInode, const std::string &name) { //
    if (parentDirInode.file_type != FileType::DIRECTORY) { //
        std::cerr << "Parent is not a directory." << std::endl;
        return false;
    }
    if (name.empty() || name == "." || name == "..") {
        std::cerr << "Cannot remove '" << name << "' using this method." << std::endl;
        return false;
    }

    char blockBuffer[DEFAULT_BLOCK_SIZE]; //
    DirectoryEntry* entries = reinterpret_cast<DirectoryEntry*>(blockBuffer); //
    int entriesPerBlock = DEFAULT_BLOCK_SIZE / sizeof(DirectoryEntry); //
    long long dirEntriesCount = parentDirInode.file_size / sizeof(DirectoryEntry); //
    long long entriesProcessed = 0;
    bool foundAndRemoved = false;
    int targetInodeId = INVALID_INODE_ID; //

    for (int i = 0; i < NUM_DIRECT_BLOCKS && entriesProcessed < dirEntriesCount && !foundAndRemoved; ++i) { //
        int blockId = parentDirInode.direct_blocks[i]; //
        if (blockId == INVALID_BLOCK_ID) continue; //

        if (!db_manager_->vdisk_->readBlock(blockId, blockBuffer, DEFAULT_BLOCK_SIZE)) { //
            std::cerr << "Error reading directory block " << blockId << std::endl;
            return false; // Critical error
        }

        for (int j = 0; j < entriesPerBlock && entriesProcessed < dirEntriesCount; ++j) {
            if (entries[j].inode_id != INVALID_INODE_ID && strncmp(entries[j].filename, name.c_str(), MAX_FILENAME_LENGTH) == 0) { //
                targetInodeId = entries[j].inode_id; //
                entries[j].inode_id = INVALID_INODE_ID; // Mark as free
                // Optionally clear filename: memset(entries[j].filename, 0, MAX_FILENAME_LENGTH);
                if (!db_manager_->vdisk_->writeBlock(blockId, blockBuffer, DEFAULT_BLOCK_SIZE)) { //
                    std::cerr << "Error writing to directory block " << blockId << " after removal." << std::endl;
                    // Entry is logically removed from inode, but disk state might be inconsistent.
                    // For robustness, may need to mark file system as dirty or attempt recovery.
                    return false;
                }
                foundAndRemoved = true;
                break;
            }
            entriesProcessed++;
        }
    }
    // TODO: Handle indirect blocks

    if (!foundAndRemoved) {
        std::cerr << "Entry '" << name << "' not found in directory." << std::endl;
        return false;
    }

    // Parent directory size doesn't typically shrink by removing an entry,
    // the slot is just marked free for future use.
    // Some file systems might implement compaction, but it's complex.
    // parentDirInode.file_size -= sizeof(DirectoryEntry); // This is usually NOT done.

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    parentDirInode.modification_time = now; //
    parentDirInode.access_time = now; //

    if (!inode_manager_->writeInode(parentDirInode.inode_id, parentDirInode)) { //
        std::cerr << "Failed to write parent directory inode after removing entry." << std::endl;
        return false;
    }

    // The caller (FileSystem::rm) is responsible for:
    // 1. Decrementing link_count of the targetInodeId.
    // 2. If link_count becomes 0 (and open_count is 0 for files), freeing its inode and data blocks.
    // 3. If targetInodeId was a directory, decrementing parentDirInode's link_count (due to ".." no longer pointing to it).

    return true;
}