#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H
#include <chrono>
#include <algorithm>
#include <vector>
#include <iostream>
#include "fs_core/inode_manager.h"
#include "fs_core/superblock_manager.h"
#include "fs_core/datablock_manager.h"
#include "file_operations/directory_manager.h"
#include "data_structures.h"
#include "common_defs.h"
class DataBlockManager;

class FileManager
{
public:
    FileManager(DataBlockManager *dbManager, InodeManager *inodeManager, SuperBlockManager *sbManager, DirectoryManager *dirManager);
    int createFileInode(short ownerUid, short permissions);
    int openFile(int inodeId, OpenMode mode, std::vector<ProcessOpenFileEntry> &processOpenFileTable, std::vector<SystemOpenFileEntry> &systemOpenFileTable); // OpenMode, ProcessOpenFileEntry, SystemOpenFileEntry
    bool closeFile(int fd, std::vector<ProcessOpenFileEntry> &processOpenFileTable, std::vector<SystemOpenFileEntry> &systemOpenFileTable);
    int readFile(int fd, char *buffer, int length, const std::vector<ProcessOpenFileEntry> &processOpenFileTable, std::vector<SystemOpenFileEntry> &systemOpenFileTable);
    int writeFile(int fd, const char *buffer, int length, std::vector<ProcessOpenFileEntry> &processOpenFileTable, std::vector<SystemOpenFileEntry> &systemOpenFileTable);
    bool deleteFileByInode(int inodeId);

private:
    DataBlockManager *db_manager_;
    InodeManager *inode_manager_;
    SuperBlockManager *sb_manager_;
    DirectoryManager *dir_manager_;
};

#endif // FILE_MANAGER_H