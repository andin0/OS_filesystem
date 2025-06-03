#ifndef INODE_MANAGER_H
#define INODE_MANAGER_H
#include "fs_core/virtual_disk.h"
#include "fs_core/superblock_manager.h"
#include "data_structures.h"

class InodeManager
{
public:
    InodeManager(VirtualDisk *vdisk, SuperBlockManager *sbManager);
    bool readInode(int inodeId, Inode &inode); // Inode 结构体在 data_structures.h
    bool writeInode(int inodeId, const Inode &inode);
    int getBlockIdForFileOffset(Inode &inode, long long offset, bool allocateIfMissing);

private:                            // 添加私有成员变量
    VirtualDisk *vdisk_;            // 指向虚拟磁盘对象的指针
    SuperBlockManager *sb_manager_; // 指向超级块管理器对象的指针
};
#endif // INODE_MANAGER_H