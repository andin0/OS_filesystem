#ifndef SUPERBLOCK_MANAGER_H
#define SUPERBLOCK_MANAGER_H
#include "all_includes.h"
class SuperBlockManager
{
public:
    SuperBlockManager(VirtualDisk *vdisk);
    bool loadSuperBlock();
    bool saveSuperBlock();
    bool formatFileSystem(int totalInodes, int blockSize);
    int allocateBlock();
    void freeBlock(int blockId);
    int allocateInode();
    void freeInode(int inodeId);
    const SuperBlock &getSuperBlockInfo() const; // 注意：SuperBlock 结构体在 data_structures.h
private:
    void initializeFreeBlockGroups();
};
#endif // SUPERBLOCK_MANAGER_H