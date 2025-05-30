#ifndef SUPERBLOCK_MANAGER_H // 确保有包含守卫
#define SUPERBLOCK_MANAGER_H

#include "all_includes.h" // 确保这里包含了 VirtualDisk 和 SuperBlock 的定义

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
    const SuperBlock &getSuperBlockInfo() const;

private:
    VirtualDisk* vdisk_;      // 指向虚拟磁盘的指针
    SuperBlock superblock_;   // 超级块的内存副本

    // 用于i-node位图操作的私有辅助方法声明
    bool readInodeBitmapBlock(int bitmap_block_offset, char* buffer) const;
    bool writeInodeBitmapBlock(int bitmap_block_offset, const char* buffer);
    bool getInodeBit(int inodeId, bool& isSet) const;
    bool setInodeBit(int inodeId, bool setToUsed);

    void initializeFreeBlockGroups(); // 这个方法已在您的片段中
};

#endif // SUPERBLOCK_MANAGER_H