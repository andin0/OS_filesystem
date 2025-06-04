#include "fs_core/datablock_manager.h"
#include "fs_core/virtual_disk.h"
#include "fs_core/inode_manager.h"      // 需要调用 getBlockIdForFileOffset 和 writeInode
#include "fs_core/superblock_manager.h" // 需要访问超级块信息
#include "common_defs.h"
#include "data_structures.h"
#include <iostream>
#include <vector>
#include <cstring>   // For std::memcpy, std::memset
#include <algorithm> // For std::min, std::max
#include <chrono>    // For std::chrono::system_clock
#include <ctime>     // For std::time_t and std::chrono::system_clock::to_time_t

// DataBlockManager 构造函数
DataBlockManager::DataBlockManager(VirtualDisk *vdisk, InodeManager *inodeManager, SuperBlockManager *sbManager)
    : vdisk_(vdisk), inode_manager_(inodeManager), sb_manager_(sbManager)
    {
    if (!vdisk_ || !inode_manager_ || !sb_manager_) {
        throw std::runtime_error("DataBlockManager: VirtualDisk, InodeManager, 或 SuperBlockManager 指针在初始化后仍为空。");
    }
}

// 从文件的指定偏移量读取数据
int DataBlockManager::readFileData(Inode &inode, long long offset, char *buffer, int length) {
    if (!vdisk_ || !inode_manager_ || !sb_manager_) return -1;
    if (length <= 0) return 0;
    if (offset < 0) {
        std::cerr << "错误 (readFileData): 无效的偏移量 " << offset << std::endl;
        return -1;
    }

    const SuperBlock& sb = sb_manager_->getSuperBlockInfo();
    int block_size = sb.block_size;
    int bytes_read = 0;
    long long current_offset = offset;

    if (current_offset >= inode.file_size) {
        return 0; 
    }
    length = static_cast<int>(std::min(static_cast<long long>(length), inode.file_size - current_offset));
    if (length <= 0) return 0;

    std::vector<char> temp_block_buffer_vec(block_size);

    while (bytes_read < length) {
        int physical_block_id = inode_manager_->getBlockIdForFileOffset(inode, current_offset, false); 
        if (physical_block_id == INVALID_BLOCK_ID) {
            std::cerr << "警告 (readFileData): 在偏移量 " << current_offset << " 处未找到数据块 (inode " << inode.inode_id << ")。" << std::endl;
            break; 
        }

        if (!vdisk_->readBlock(physical_block_id, temp_block_buffer_vec.data(), block_size)) {
            std::cerr << "错误 (readFileData): 无法从物理块 " << physical_block_id << " 读取数据。" << std::endl;
            // 如果已经读取了部分数据，仍然尝试更新访问时间并写回inode
            if (bytes_read > 0 && inode.inode_id != INVALID_INODE_ID) {
                auto now = std::chrono::system_clock::now();
                inode.access_time = std::chrono::system_clock::to_time_t(now);
                if (!inode_manager_->writeInode(inode.inode_id, inode)) {
                    std::cerr << "警告 (readFileData): 读取部分数据后，更新访问时间并写回inode " << inode.inode_id << " 失败。" << std::endl;
                }
            }
            return (bytes_read > 0) ? bytes_read : -1; 
        }

        int offset_in_block = static_cast<int>(current_offset % block_size);
        int bytes_to_read_from_block = std::min(block_size - offset_in_block, length - bytes_read);

        std::memcpy(buffer + bytes_read, temp_block_buffer_vec.data() + offset_in_block, bytes_to_read_from_block);

        bytes_read += bytes_to_read_from_block;
        current_offset += bytes_to_read_from_block;
    }

    // 如果成功读取了任何数据，则更新访问时间并写回Inode
    if (bytes_read > 0) {
        if (inode.inode_id != INVALID_INODE_ID) {
            auto now = std::chrono::system_clock::now();
            inode.access_time = std::chrono::system_clock::to_time_t(now);
            if (!inode_manager_->writeInode(inode.inode_id, inode)) {
                std::cerr << "警告 (readFileData): 读取数据后，更新访问时间并写回inode " << inode.inode_id << " 失败。" << std::endl;
                // 即使写回inode失败，读取操作本身可能已成功，所以仍然返回bytes_read
            }
        } else {
             std::cerr << "警告 (readFileData): inode_id 无效，无法更新访问时间并写回inode。" << std::endl;
        }
    }
    return bytes_read;
}

//向文件的指定偏移量写入数据
int DataBlockManager::writeFileData(Inode &inode, long long offset, const char *buffer, int length, bool &sizeChanged) {
    if (!vdisk_ || !inode_manager_ || !sb_manager_) return -1;
    if (length <= 0) {
        sizeChanged = false;
        return 0;
    }
     if (offset < 0) {
        std::cerr << "错误 (writeFileData): 无效的偏移量 " << offset << std::endl;
        sizeChanged = false;
        return -1;
    }

    const SuperBlock& sb = sb_manager_->getSuperBlockInfo();
    int block_size = sb.block_size;
    int bytes_written = 0;
    long long current_offset = offset;
    sizeChanged = false;
    bool inode_modified_by_block_alloc = false; // 标记inode的块指针是否因分配而改变

    std::vector<char> temp_block_buffer_vec(block_size);

    while (bytes_written < length) {
        long long original_single_indirect = inode.single_indirect_block; // 记录分配前的间接块指针
        long long original_double_indirect = inode.double_indirect_block;
        // 可以进一步记录 direct_blocks 的原始状态，但这会更复杂
        // 主要目的是检测 getBlockIdForFileOffset 是否分配了新的 *顶层* 间接块指针

        int physical_block_id = inode_manager_->getBlockIdForFileOffset(inode, current_offset, true); 
        
        if (physical_block_id == INVALID_BLOCK_ID) {
            std::cerr << "错误 (writeFileData): 无法在偏移量 " << current_offset << " 处获取或分配数据块 (inode " << inode.inode_id << ")。" << std::endl;
            break; 
        }

        // 检查 inode 的顶层间接块指针是否因 getBlockIdForFileOffset 而改变
        if (inode.single_indirect_block != original_single_indirect || 
            inode.double_indirect_block != original_double_indirect) {
            inode_modified_by_block_alloc = true;
        }
        // 更精细的检查可以比较所有 direct_blocks，但这可能过于频繁
        // 简单假设如果 getBlockIdForFileOffset 被调用且 allocateIfMissing=true，inode 可能已改动


        int offset_in_block = static_cast<int>(current_offset % block_size);
        int bytes_to_write_to_block = std::min(block_size - offset_in_block, length - bytes_written);

        if (offset_in_block != 0 || bytes_to_write_to_block < block_size) {
            if (!vdisk_->readBlock(physical_block_id, temp_block_buffer_vec.data(), block_size)) {
                 std::cerr << "错误 (writeFileData): 无法从物理块 " << physical_block_id << " 读取数据以进行部分写入。" << std::endl;
                 break;
            }
        }

        std::memcpy(temp_block_buffer_vec.data() + offset_in_block, buffer + bytes_written, bytes_to_write_to_block);

        if (!vdisk_->writeBlock(physical_block_id, temp_block_buffer_vec.data(), block_size)) {
            std::cerr << "错误 (writeFileData): 无法向物理块 " << physical_block_id << " 写入数据。" << std::endl;
            break; 
        }

        bytes_written += bytes_to_write_to_block;
        current_offset += bytes_to_write_to_block;

        if (current_offset > inode.file_size) {
            inode.file_size = current_offset;
            sizeChanged = true;
        }
    }

    // 如果确实发生了写入或者inode的块指针结构被修改，则更新时间戳并写回Inode
    if (bytes_written > 0 || sizeChanged || inode_modified_by_block_alloc) {
        if (inode.inode_id != INVALID_INODE_ID) {
            auto now = std::chrono::system_clock::now();
            std::time_t current_time_t = std::chrono::system_clock::to_time_t(now);
            
            inode.modification_time = current_time_t;
            inode.access_time = current_time_t; // 写入操作通常也会更新访问时间

            if (!inode_manager_->writeInode(inode.inode_id, inode)) {
                std::cerr << "警告 (writeFileData): 在数据写入后，更新时间戳并写回inode " << inode.inode_id << " 失败。" << std::endl;
                // 这是一个潜在的不一致状态
            }
        } else {
            std::cerr << "警告 (writeFileData): inode_id 无效，无法更新时间戳并写回inode。" << std::endl;
        }
    }
    return bytes_written;
}

// 清除一个i-node所占用的所有数据块
void DataBlockManager::clearInodeDataBlocks(Inode &inode) {
    if (!vdisk_ || !inode_manager_ || !sb_manager_) return;

    const SuperBlock& sb = sb_manager_->getSuperBlockInfo();
    int block_size = sb.block_size;
    int pointers_per_block = block_size / sizeof(int);
     if (pointers_per_block == 0) {
         std::cerr << "错误 (clearInodeDataBlocks): block_size " << block_size << " 太小，无法容纳任何块指针。" << std::endl;
         return;
    }

    bool inode_changed = false; // 标记inode是否有实质性改变（除了file_size）

    for (int i = 0; i < NUM_DIRECT_BLOCKS; ++i) {
        if (inode.direct_blocks[i] != INVALID_BLOCK_ID) {
            sb_manager_->freeBlock(inode.direct_blocks[i]);
            inode.direct_blocks[i] = INVALID_BLOCK_ID;
            inode_changed = true;
        }
    }

    if (inode.single_indirect_block != INVALID_BLOCK_ID) {
        std::vector<char> indirect_block_buffer_vec(block_size);
        if (vdisk_->readBlock(inode.single_indirect_block, indirect_block_buffer_vec.data(), block_size)) {
            int* indirect_pointers = reinterpret_cast<int*>(indirect_block_buffer_vec.data());
            for (int i = 0; i < pointers_per_block; ++i) {
                if (indirect_pointers[i] != INVALID_BLOCK_ID) {
                    sb_manager_->freeBlock(indirect_pointers[i]);
                }
            }
        } else {
            std::cerr << "警告 (clearInodeDataBlocks): 无法读取一级间接块 "
                      << inode.single_indirect_block << " 来释放其指向的数据块。" << std::endl;
        }
        sb_manager_->freeBlock(inode.single_indirect_block);
        inode.single_indirect_block = INVALID_BLOCK_ID;
        inode_changed = true;
    }

    if (inode.double_indirect_block != INVALID_BLOCK_ID) {
        std::vector<char> l1_indirect_buffer_vec(block_size);
        if (vdisk_->readBlock(inode.double_indirect_block, l1_indirect_buffer_vec.data(), block_size)) {
            int* l1_pointers = reinterpret_cast<int*>(l1_indirect_buffer_vec.data());
            for (int i = 0; i < pointers_per_block; ++i) {
                if (l1_pointers[i] != INVALID_BLOCK_ID) { 
                    std::vector<char> l2_indirect_buffer_vec(block_size);
                    if (vdisk_->readBlock(l1_pointers[i], l2_indirect_buffer_vec.data(), block_size)) {
                        int* l2_pointers = reinterpret_cast<int*>(l2_indirect_buffer_vec.data());
                        for (int j = 0; j < pointers_per_block; ++j) {
                            if (l2_pointers[j] != INVALID_BLOCK_ID) {
                                sb_manager_->freeBlock(l2_pointers[j]);
                            }
                        }
                    } else {
                         std::cerr << "警告 (clearInodeDataBlocks): 无法读取二级间接L2块 "
                                   << l1_pointers[i] << " 来释放其指向的数据块。" << std::endl;
                    }
                    sb_manager_->freeBlock(l1_pointers[i]); 
                }
            }
        } else {
             std::cerr << "警告 (clearInodeDataBlocks): 无法读取二级间接L1块 "
                       << inode.double_indirect_block << " 来释放其指向的L2块。" << std::endl;
        }
        sb_manager_->freeBlock(inode.double_indirect_block); 
        inode.double_indirect_block = INVALID_BLOCK_ID;
        inode_changed = true;
    }
    
    bool size_was_non_zero = (inode.file_size > 0);
    inode.file_size = 0;

    // 如果inode的块指针或文件大小（从非零变为零）发生了改变，则更新时间戳并写回
    if (inode_changed || size_was_non_zero) {
        if (inode.inode_id != INVALID_INODE_ID) {
            auto now = std::chrono::system_clock::now();
            std::time_t current_time_t = std::chrono::system_clock::to_time_t(now);
            inode.modification_time = current_time_t;
            inode.access_time = current_time_t; // 清除数据也视为一种修改和访问

            if (!inode_manager_->writeInode(inode.inode_id, inode)) {
                std::cerr << "警告 (clearInodeDataBlocks): 清除数据块后，更新时间戳并写回inode " << inode.inode_id << " 失败。" << std::endl;
            }
        } else {
            std::cerr << "警告 (clearInodeDataBlocks): inode_id 无效，无法更新时间戳并写回inode。" << std::endl;
        }
    }
}
