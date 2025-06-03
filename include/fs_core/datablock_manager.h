#ifndef DATA_BLOCK_MANAGER_H
#define DATA_BLOCK_MANAGER_H
#include "fs_core/virtual_disk.h"
#include "fs_core/inode_manager.h"      // 需要调用 getBlockIdForFileOffset
#include "fs_core/superblock_manager.h" // 需要访问超级块信息
#include "data_structures.h"
#include <vector>
class DataBlockManager
{
public:
    DataBlockManager(VirtualDisk *vdisk, InodeManager *inodeManager, SuperBlockManager *sbManager);
    int readFileData(Inode &inode, long long offset, char *buffer, int length);
    int writeFileData(Inode &inode, long long offset, const char *buffer, int length, bool &sizeChanged);
    void clearInodeDataBlocks(Inode &inode);
    VirtualDisk *vdisk_;

private: // 添加私有成员变量
    InodeManager *inode_manager_;
    SuperBlockManager *sb_manager_;
};
#endif // DATA_BLOCK_MANAGER_H