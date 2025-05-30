#include "fs_core/inode_manager.h"
#include "fs_core/virtual_disk.h"
#include "fs_core/superblock_manager.h"
#include "common_defs.h"
#include "data_structures.h"
#include <iostream>
#include <vector>
#include <cstring> // For std::memcpy, std::memset
#include <algorithm> // For std::min, std::max

// InodeManager 构造函数
InodeManager::InodeManager(VirtualDisk *vdisk, SuperBlockManager *sbManager)
    : vdisk_(vdisk), sb_manager_(sbManager) {
    if (!vdisk_ || !sb_manager_) {
        throw std::runtime_error("InodeManager: VirtualDisk 或 SuperBlockManager 指针为空。");
    }
}

// 从磁盘读取指定的i-node
bool InodeManager::readInode(int inodeId, Inode &inode) {
    if (!vdisk_ || !sb_manager_) return false;

    const SuperBlock& sb = sb_manager_->getSuperBlockInfo();
    if (inodeId < 0 || inodeId >= sb.total_inodes) {
        std::cerr << "错误 (readInode): i-node ID " << inodeId << " 超出范围 (0-" << sb.total_inodes - 1 << ")." << std::endl;
        return false;
    }

    int inode_table_actual_start_block = sb.inode_table_start_block_idx;
    int inode_size = sb.inode_size;
    int inodes_per_block = sb.block_size / inode_size;

    if (inodes_per_block == 0) {
        std::cerr << "错误 (readInode): block_size " << sb.block_size << " 对于 inode_size " << inode_size << " 太小。" << std::endl;
        return false;
    }

    int block_num_for_inode = inode_table_actual_start_block + (inodeId / inodes_per_block);
    int offset_in_block = (inodeId % inodes_per_block) * inode_size;

    int inode_table_blocks_count = (sb.total_inodes + inodes_per_block - 1) / inodes_per_block;
    if (block_num_for_inode >= inode_table_actual_start_block + inode_table_blocks_count ||
        block_num_for_inode >= sb.first_data_block_idx) {
        std::cerr << "错误 (readInode): 计算得到的i-node块 " << block_num_for_inode
                  << " 超出i-node表范围或侵入数据块区域。" << std::endl;
        std::cerr << "  i-node表起始: " << inode_table_actual_start_block << ", 占用: " << inode_table_blocks_count << " 块." <<std::endl;
        std::cerr << "  第一个数据块: " << sb.first_data_block_idx << std::endl;
        return false;
    }

    // char block_buffer[sb.block_size]; // 原来的问题行
    std::vector<char> block_buffer_vec(sb.block_size); // 修改后的行

    if (!vdisk_->readBlock(block_num_for_inode, block_buffer_vec.data(), sb.block_size)) {
        std::cerr << "错误 (readInode): 无法从磁盘读取包含i-node " << inodeId << " 的块 " << block_num_for_inode << "。" << std::endl;
        return false;
    }

    std::memcpy(&inode, block_buffer_vec.data() + offset_in_block, sizeof(Inode));
    return true;
}

// 将指定的i-node写回磁盘
bool InodeManager::writeInode(int inodeId, const Inode &inode) {
    if (!vdisk_ || !sb_manager_) return false;

    const SuperBlock& sb = sb_manager_->getSuperBlockInfo();
    if (inodeId < 0 || inodeId >= sb.total_inodes) {
        std::cerr << "错误 (writeInode): i-node ID " << inodeId << " 超出范围 (0-" << sb.total_inodes - 1 << ")." << std::endl;
        return false;
    }

    int inode_table_actual_start_block = sb.inode_table_start_block_idx;
    int inode_size = sb.inode_size;
    int inodes_per_block = sb.block_size / inode_size;

    if (inodes_per_block == 0) {
        std::cerr << "错误 (writeInode): block_size " << sb.block_size << " 对于 inode_size " << inode_size << " 太小。" << std::endl;
        return false;
    }

    int block_num_for_inode = inode_table_actual_start_block + (inodeId / inodes_per_block);
    int offset_in_block = (inodeId % inodes_per_block) * inode_size;

    int inode_table_blocks_count = (sb.total_inodes + inodes_per_block - 1) / inodes_per_block;
    if (block_num_for_inode >= inode_table_actual_start_block + inode_table_blocks_count ||
        block_num_for_inode >= sb.first_data_block_idx) {
        std::cerr << "错误 (writeInode): 计算得到的i-node块 " << block_num_for_inode
                  << " 超出i-node表范围或侵入数据块区域。" << std::endl;
        return false;
    }

    // char block_buffer[sb.block_size]; // 原来的问题行
    std::vector<char> block_buffer_vec(sb.block_size); // 修改后的行

    // 为了只修改目标i-node，需要先读取整个块，修改，再写回
    if (!vdisk_->readBlock(block_num_for_inode, block_buffer_vec.data(), sb.block_size)) {
        std::cerr << "错误 (writeInode): 写入i-node " << inodeId << " 前无法读取块 " << block_num_for_inode << "。" << std::endl;
        return false;
    }

    std::memcpy(block_buffer_vec.data() + offset_in_block, &inode, sizeof(Inode));

    if (!vdisk_->writeBlock(block_num_for_inode, block_buffer_vec.data(), sb.block_size)) {
        std::cerr << "错误 (writeInode): 无法将包含i-node " << inodeId << " 的块 " << block_num_for_inode << " 写回磁盘。" << std::endl;
        return false;
    }
    return true;
}

// 根据文件内的逻辑偏移量获取对应的数据块号
int InodeManager::getBlockIdForFileOffset(Inode &inode, long long offset, bool allocateIfMissing) {
    if (!vdisk_ || !sb_manager_) return INVALID_BLOCK_ID;
    const SuperBlock& sb = sb_manager_->getSuperBlockInfo();
    int block_size = sb.block_size;

    if (offset < 0) {
        std::cerr << "错误: 文件偏移量 " << offset << " 无效。" << std::endl;
        return INVALID_BLOCK_ID;
    }

    int logical_block_index = static_cast<int>(offset / block_size);

    // 1. 处理直接块
    if (logical_block_index < NUM_DIRECT_BLOCKS) {
        if (inode.direct_blocks[logical_block_index] == INVALID_BLOCK_ID) {
            if (allocateIfMissing) {
                int new_block_id = sb_manager_->allocateBlock();
                if (new_block_id == INVALID_BLOCK_ID) {
                    std::cerr << "错误: 无法为直接块 " << logical_block_index << " 分配新的数据块。" << std::endl;
                    return INVALID_BLOCK_ID;
                }
                inode.direct_blocks[logical_block_index] = new_block_id;
                // 文件大小和inode的写回由上层DataBlockManager或FileManager处理
            } else {
                return INVALID_BLOCK_ID;
            }
        }
        return inode.direct_blocks[logical_block_index];
    }

    int pointers_per_block = block_size / sizeof(int);
    if (pointers_per_block == 0) {
         std::cerr << "错误 (getBlockIdForFileOffset): block_size " << block_size << " 太小，无法容纳任何块指针。" << std::endl;
         return INVALID_BLOCK_ID;
    }

    // 2. 处理一级间接块
    int single_indirect_start_idx = NUM_DIRECT_BLOCKS;
    if (logical_block_index < single_indirect_start_idx + pointers_per_block) {
        if (inode.single_indirect_block == INVALID_BLOCK_ID) {
            if (allocateIfMissing) {
                int new_indirect_block_id = sb_manager_->allocateBlock();
                if (new_indirect_block_id == INVALID_BLOCK_ID) {
                     std::cerr << "错误: 无法为一级间接块本身分配新的元数据块。" << std::endl;
                    return INVALID_BLOCK_ID;
                }
                inode.single_indirect_block = new_indirect_block_id;
                
                // 初始化新分配的一级间接块 (所有指针设为INVALID_BLOCK_ID)
                std::vector<char> indirect_block_buffer_vec(block_size);
                std::vector<int> indirect_pointers_init_vec(pointers_per_block, INVALID_BLOCK_ID);
                std::memcpy(indirect_block_buffer_vec.data(), indirect_pointers_init_vec.data(), pointers_per_block * sizeof(int));
                // 如果块大小大于指针数组大小，用0填充剩余部分
                if (static_cast<size_t>(block_size) > pointers_per_block * sizeof(int)) {
                    std::memset(indirect_block_buffer_vec.data() + pointers_per_block * sizeof(int), 0, 
                                block_size - (pointers_per_block * sizeof(int)));
                }

                if(!vdisk_->writeBlock(inode.single_indirect_block, indirect_block_buffer_vec.data(), block_size)){
                    std::cerr << "错误: 初始化新分配的一级间接块 " << inode.single_indirect_block << " 失败。" << std::endl;
                    sb_manager_->freeBlock(inode.single_indirect_block); // 回滚分配
                    inode.single_indirect_block = INVALID_BLOCK_ID;
                    return INVALID_BLOCK_ID;
                }
            } else {
                return INVALID_BLOCK_ID; // 一级间接块不存在且不分配
            }
        }
        
        // 读取一级间接块
        std::vector<char> indirect_block_buffer_vec(block_size);
        if (!vdisk_->readBlock(inode.single_indirect_block, indirect_block_buffer_vec.data(), block_size)) {
            std::cerr << "错误: 无法读取一级间接块 " << inode.single_indirect_block << "。" << std::endl;
            return INVALID_BLOCK_ID;
        }
        
        int* indirect_pointers = reinterpret_cast<int*>(indirect_block_buffer_vec.data());
        int index_in_indirect_block = logical_block_index - single_indirect_start_idx;
        
        if (indirect_pointers[index_in_indirect_block] == INVALID_BLOCK_ID) {
            if (allocateIfMissing) {
                int new_data_block_id = sb_manager_->allocateBlock();
                if (new_data_block_id == INVALID_BLOCK_ID) {
                    std::cerr << "错误: 无法为一级间接寻址的数据块分配新块 (逻辑块 " << logical_block_index << ")。" << std::endl;
                    return INVALID_BLOCK_ID;
                }
                indirect_pointers[index_in_indirect_block] = new_data_block_id;
                // 写回修改后的一级间接块
                if (!vdisk_->writeBlock(inode.single_indirect_block, indirect_block_buffer_vec.data(), block_size)) {
                    std::cerr << "错误: 更新一级间接块 " << inode.single_indirect_block << " 失败。" << std::endl;
                    sb_manager_->freeBlock(new_data_block_id); // 回滚数据块分配
                    // indirect_pointers[index_in_indirect_block] 已经在内存中，但磁盘未更新
                    return INVALID_BLOCK_ID;
                }
            } else {
                return INVALID_BLOCK_ID; // 数据块不存在且不分配
            }
        }
        return indirect_pointers[index_in_indirect_block];
    }

    // 3. 处理二级间接块
    int double_indirect_start_idx = single_indirect_start_idx + pointers_per_block;
    if (logical_block_index < double_indirect_start_idx + pointers_per_block * pointers_per_block) {
        // 检查二级间接块本身（L1间接块）是否存在
        if (inode.double_indirect_block == INVALID_BLOCK_ID) { 
            if (allocateIfMissing) {
                int new_l1_indirect_id = sb_manager_->allocateBlock();
                if (new_l1_indirect_id == INVALID_BLOCK_ID) {
                    std::cerr << "错误: 无法为二级间接块的L1元数据块分配新块。" << std::endl;
                    return INVALID_BLOCK_ID;
                }
                inode.double_indirect_block = new_l1_indirect_id;
                
                std::vector<char> l1_buffer_vec(block_size);
                std::vector<int> l1_pointers_init_vec(pointers_per_block, INVALID_BLOCK_ID);
                std::memcpy(l1_buffer_vec.data(), l1_pointers_init_vec.data(), pointers_per_block * sizeof(int));
                if (static_cast<size_t>(block_size) > pointers_per_block * sizeof(int)) {
                     std::memset(l1_buffer_vec.data() + pointers_per_block * sizeof(int), 0, 
                                 block_size - (pointers_per_block * sizeof(int)));
                }

                if(!vdisk_->writeBlock(inode.double_indirect_block, l1_buffer_vec.data(), block_size)){
                    std::cerr << "错误: 初始化新分配的二级间接L1块 " << inode.double_indirect_block << " 失败。" << std::endl;
                    sb_manager_->freeBlock(inode.double_indirect_block);
                    inode.double_indirect_block = INVALID_BLOCK_ID;
                    return INVALID_BLOCK_ID;
                }
            } else {
                return INVALID_BLOCK_ID;
            }
        }
        
        // 读取L1间接块
        std::vector<char> l1_buffer_vec(block_size);
        if (!vdisk_->readBlock(inode.double_indirect_block, l1_buffer_vec.data(), block_size)) {
            std::cerr << "错误: 无法读取二级间接块的L1元数据块 " << inode.double_indirect_block << "。" << std::endl;
            return INVALID_BLOCK_ID;
        }
        int* l1_pointers = reinterpret_cast<int*>(l1_buffer_vec.data());
        int index_in_l1 = (logical_block_index - double_indirect_start_idx) / pointers_per_block;

        // 检查L2间接块是否存在
        if (l1_pointers[index_in_l1] == INVALID_BLOCK_ID) { 
            if (allocateIfMissing) {
                int new_l2_indirect_id = sb_manager_->allocateBlock();
                if (new_l2_indirect_id == INVALID_BLOCK_ID) {
                    std::cerr << "错误: 无法为二级间接块的L2元数据块分配新块。" << std::endl;
                    return INVALID_BLOCK_ID;
                }
                l1_pointers[index_in_l1] = new_l2_indirect_id;
                
                std::vector<char> l2_buffer_vec(block_size);
                std::vector<int> l2_pointers_init_vec(pointers_per_block, INVALID_BLOCK_ID);
                std::memcpy(l2_buffer_vec.data(), l2_pointers_init_vec.data(), pointers_per_block * sizeof(int));
                 if (static_cast<size_t>(block_size) > pointers_per_block * sizeof(int)) {
                     std::memset(l2_buffer_vec.data() + pointers_per_block * sizeof(int), 0, 
                                 block_size - (pointers_per_block * sizeof(int)));
                }

                if(!vdisk_->writeBlock(l1_pointers[index_in_l1], l2_buffer_vec.data(), block_size)){ // Write L2 content
                     std::cerr << "错误: 初始化新分配的二级间接L2块 " << l1_pointers[index_in_l1] << " 失败。" << std::endl;
                     sb_manager_->freeBlock(l1_pointers[index_in_l1]); // Free L2 block
                     l1_pointers[index_in_l1] = INVALID_BLOCK_ID; // Rollback pointer in L1 (memory)
                     // Note: L1 block on disk is not updated yet to reflect this rollback
                     return INVALID_BLOCK_ID;
                }
                // 写回修改后的L1间接块 (now containing pointer to L2)
                if (!vdisk_->writeBlock(inode.double_indirect_block, l1_buffer_vec.data(), block_size)) {
                    std::cerr << "错误: 更新二级间接块的L1元数据块 " << inode.double_indirect_block << " 失败。" << std::endl;
                    sb_manager_->freeBlock(new_l2_indirect_id); // Rollback L2 allocation
                    l1_pointers[index_in_l1] = INVALID_BLOCK_ID; // Rollback pointer in L1 (memory)
                    return INVALID_BLOCK_ID;
                }
            } else {
                return INVALID_BLOCK_ID;
            }
        }
        
        // 读取L2间接块
        int l2_block_id = l1_pointers[index_in_l1];
        std::vector<char> l2_buffer_vec(block_size);
        if (!vdisk_->readBlock(l2_block_id, l2_buffer_vec.data(), block_size)) {
            std::cerr << "错误: 无法读取二级间接块的L2元数据块 " << l2_block_id << "。" << std::endl;
            return INVALID_BLOCK_ID;
        }
        int* l2_pointers = reinterpret_cast<int*>(l2_buffer_vec.data());
        int index_in_l2 = (logical_block_index - double_indirect_start_idx) % pointers_per_block;

        // 检查实际数据块是否存在
        if (l2_pointers[index_in_l2] == INVALID_BLOCK_ID) { 
            if (allocateIfMissing) {
                int new_data_block_id = sb_manager_->allocateBlock();
                if (new_data_block_id == INVALID_BLOCK_ID) {
                    std::cerr << "错误: 无法为二级间接寻址的数据块分配新块 (逻辑块 " << logical_block_index << ")。" << std::endl;
                    return INVALID_BLOCK_ID;
                }
                l2_pointers[index_in_l2] = new_data_block_id;
                // 写回修改后的L2间接块
                if (!vdisk_->writeBlock(l2_block_id, l2_buffer_vec.data(), block_size)) {
                    std::cerr << "错误: 更新二级间接块的L2元数据块 " << l2_block_id << " 失败。" << std::endl;
                    sb_manager_->freeBlock(new_data_block_id); // Rollback data block allocation
                    return INVALID_BLOCK_ID;
                }
            } else {
                return INVALID_BLOCK_ID;
            }
        }
        return l2_pointers[index_in_l2];
    }

    std::cerr << "错误: 逻辑块索引 " << logical_block_index << " 超出文件系统支持的最大范围。" << std::endl;
    return INVALID_BLOCK_ID;
}
