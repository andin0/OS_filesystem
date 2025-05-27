#ifndef DATA_BLOCK_MANAGER_H
#define DATA_BLOCK_MANAGER_H
#include "all_includes.h"
class DataBlockManager
{
public:
    DataBlockManager(VirtualDisk *vdisk, InodeManager *inodeManager, SuperBlockManager *sbManager);
    int readFileData(Inode &inode, long long offset, char *buffer, int length);
    int writeFileData(Inode &inode, long long offset, const char *buffer, int length, bool &sizeChanged);
    void clearInodeDataBlocks(Inode &inode);
};
#endif // DATA_BLOCK_MANAGER_H