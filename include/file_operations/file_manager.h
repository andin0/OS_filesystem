#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H
#include "all_includes.h"
class FileManager
{
public:
    FileManager(DataBlockManager *dbManager, InodeManager *inodeManager, SuperBlockManager *sbManager, DirectoryManager *dirManager);
    int createFileInode(short ownerUid, short permissions);
    int openFile(int inodeId, OpenMode mode, std::vector<ProcessOpenFileEntry> &processOpenFileTable, std::vector<SystemOpenFileEntry> &systemOpenFileTable); // OpenMode, ProcessOpenFileEntry, SystemOpenFileEntry
    bool closeFile(int fd, std::vector<ProcessOpenFileEntry> &processOpenFileTable, std::vector<SystemOpenFileEntry> &systemOpenFileTable);
    int readFile(int fd, char *buffer, int length, const std::vector<ProcessOpenFileEntry> &processOpenFileTable, std::vector<SystemOpenFileEntry> &systemOpenFileTable);
    int writeFile(int fd, const char *buffer, int length, std::vector<ProcessOpenFileEntry> &processOpenFileTable, std::vector<SystemOpenFileEntry> &systemOpenFileTable);
    bool deleteFileByInode(int inodeId); // This might need adjustment based on new rm logic (e.g. handling directory deletion logic elsewhere)
};

#endif // FILE_MANAGER_H