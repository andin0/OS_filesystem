#ifndef INODE_MANAGER_H
#define INODE_MANAGER_H
#include "all_includes.h"
class InodeManager
{
public:
    InodeManager(VirtualDisk *vdisk, SuperBlockManager *sbManager);
    bool readInode(int inodeId, Inode &inode); // Inode 结构体在 data_structures.h
    bool writeInode(int inodeId, const Inode &inode);
    int getBlockIdForFileOffset(Inode &inode, long long offset, bool allocateIfMissing);
};
#endif // INODE_MANAGER_H