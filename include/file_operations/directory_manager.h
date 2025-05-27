#ifndef DIRECTORY_MANAGER_H
#define DIRECTORY_MANAGER_H
#include "all_includes.h"
class DirectoryManager
{
public:
    DirectoryManager(DataBlockManager *dbManager, InodeManager *inodeManager, SuperBlockManager *sbManager);
    bool addEntry(Inode &parentDirInode, const std::string &name, int entryInodeId, FileType type); // FileType 在 common_defs.h
    bool removeEntry(Inode &parentDirInode, const std::string &name);
    int findEntry(Inode &dirInode, const std::string &name);
    std::vector<DirectoryEntry> listEntries(Inode &dirInode);                                                           // DirectoryEntry 在 data_structures.h
    int resolvePathToInode(const std::string &path, int currentDirInodeId, int rootDirInodeId, const User *currentUser, // User 在 data_structures.h
                           int *parentInodeId = nullptr, std::string *lastName = nullptr, bool followLastLink = true);
    int createDirectoryInode(short ownerUid, short permissions);
};
#endif // DIRECTORY_MANAGER_H