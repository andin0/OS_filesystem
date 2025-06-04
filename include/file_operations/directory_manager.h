#ifndef DIRECTORY_MANAGER_H
#define DIRECTORY_MANAGER_H

#include "fs_core/inode_manager.h"
#include "fs_core/datablock_manager.h"
#include "fs_core/superblock_manager.h"
#include "data_structures.h"
#include "common_defs.h"
#include <vector>
#include <string>
#include <iostream>

class DirectoryManager
{
public:
    DirectoryManager(DataBlockManager *dbManager, InodeManager *inodeManager, SuperBlockManager *sbManager);
    bool addEntry(Inode &parentDirInode, const std::string &name, int entryInodeId, FileType type); // FileType 在 common_defs.h
    bool removeEntry(Inode &parentDirInode, const std::string &name);
    int findEntry(Inode &dirInode, const std::string &name) const;
    std::vector<DirectoryEntry> listEntries(Inode &dirInode) const;                                                           // DirectoryEntry 在 data_structures.h
    int resolvePathToInode(const std::string &path, int currentDirInodeId, int rootDirInodeId, const User *currentUser, // User 在 data_structures.h
                           int *parentInodeId = nullptr, std::string *lastName = nullptr, bool followLastLink = true);
    int createDirectoryInode(short ownerUid, short permissions);

private:
    DataBlockManager *db_manager_;
    InodeManager *inode_manager_;
    SuperBlockManager *sb_manager_;
};
#endif // DIRECTORY_MANAGER_H