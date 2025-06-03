#include "fs_core/datablock_manager.h"
#include "fs_core/virtual_disk.h"
#include "fs_core/inode_manager.h"      // 需要调用 getBlockIdForFileOffset
#include "fs_core/superblock_manager.h" // 需要访问超级块信息
#include "common_defs.h"
#include "data_structures.h"
#include <iostream>
#include <vector>
#include <cstring>   // For std::memcpy, std::memset
#include <algorithm> // For std::min, std::max

// DataBlockManager 构造函数
// vdisk: 指向 VirtualDisk 对象的指针。
// inodeManager: 指向 InodeManager 对象的指针。
// sbManager: 指向 SuperBlockManager 对象的指针。
DataBlockManager::DataBlockManager(VirtualDisk *vdisk, InodeManager *inodeManager, SuperBlockManager *sbManager)
    : vdisk_(vdisk), inode_manager_(inodeManager), sb_manager_(sbManager)
    {
    if (!vdisk || !inodeManager || !sbManager) {
        throw std::runtime_error("DataBlockManager: VirtualDisk, InodeManager, 或 SuperBlockManager 指针为空。");
    }
}

// 从文件的指定偏移量读取数据
// inode: 文件的i-node（可能被修改，例如更新访问时间）。
// offset: 文件内的逻辑字节偏移量。
// buffer: 用于存储读取数据的缓冲区。
// length: 希望读取的字节数。
// 返回值: 实际读取的字节数；如果发生错误则返回 -1。
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

    // 确保读取不会超出文件实际大小
    if (current_offset >= inode.file_size) {
        return 0; // 尝试从文件末尾或之后读取
    }
    length = static_cast<int>(std::min(static_cast<long long>(length), inode.file_size - current_offset));
    if (length <= 0) return 0;


    std::vector<char> temp_block_buffer(block_size); //用于读取整个磁盘块的临时缓冲区

    while (bytes_read < length) {
        int physical_block_id = inode_manager_->getBlockIdForFileOffset(inode, current_offset, false); // 读取时不分配新块
        if (physical_block_id == INVALID_BLOCK_ID) {
            // 这通常意味着文件在该偏移量处是稀疏的，或者文件比记录的大小要小（数据损坏）
            // 或者到达了文件末尾的未分配部分
            std::cerr << "警告 (readFileData): 在偏移量 " << current_offset << " 处未找到数据块 (inode " << inode.inode_id << ")。" << std::endl;
            break; // 停止读取
        }

        if (!vdisk_->readBlock(physical_block_id, temp_block_buffer.data(), block_size)) {
            std::cerr << "错误 (readFileData): 无法从物理块 " << physical_block_id << " 读取数据。" << std::endl;
            return (bytes_read > 0) ? bytes_read : -1; // 如果已读取部分数据，返回已读取的，否则返回错误
        }

        int offset_in_block = static_cast<int>(current_offset % block_size);
        int bytes_to_read_from_block = std::min(block_size - offset_in_block, length - bytes_read);

        std::memcpy(buffer + bytes_read, temp_block_buffer.data() + offset_in_block, bytes_to_read_from_block);

        bytes_read += bytes_to_read_from_block;
        current_offset += bytes_to_read_from_block;
    }

    // 更新访问时间 (示例，实际时间戳获取需要平台API)
    // inode.access_time = time(nullptr);
    // inode_manager_->writeInode(inode.inode_id, inode); // 写回更新了访问时间的inode

    return bytes_read;
}

//向文件的指定偏移量写入数据
// inode: 文件的i-node（可能被修改，例如文件大小、修改时间）。
// offset: 文件内的逻辑字节偏移量。
// buffer: 包含要写入数据的缓冲区。
// length: 希望写入的字节数。
// sizeChanged: 输出参数，如果文件大小因此次写入而改变，则设为 true。
// 返回值: 实际写入的字节数；如果发生错误则返回 -1。
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

    std::vector<char> temp_block_buffer(block_size); // 用于读写整个磁盘块的临时缓冲区

    while (bytes_written < length) {
        int physical_block_id = inode_manager_->getBlockIdForFileOffset(inode, current_offset, true); // 写入时允许分配新块
        if (physical_block_id == INVALID_BLOCK_ID) {
            std::cerr << "错误 (writeFileData): 无法在偏移量 " << current_offset << " 处获取或分配数据块 (inode " << inode.inode_id << ")。" << std::endl;
            // 如果已经写入了一部分数据，需要决定如何处理
            // 可能需要释放刚刚为失败的getBlockIdForFileOffset分配的块（如果它内部有分配）
            break; // 停止写入
        }

        int offset_in_block = static_cast<int>(current_offset % block_size);
        int bytes_to_write_to_block = std::min(block_size - offset_in_block, length - bytes_written);

        // 如果写入不是从块的开始，或者写入的不是整个块，需要先读取旧内容（读-修改-写）
        // 以避免破坏块中不属于本次写入的部分。
        if (offset_in_block != 0 || bytes_to_write_to_block < block_size) {
            // 只有当这个物理块之前已经存在并且包含数据时，才需要读取。
            // 如果这个块是新分配的，它应该是空的（或包含垃圾数据，将被覆盖）。
            // InodeManager::getBlockIdForFileOffset 在分配新块时，新块的内容是未定义的（或由OS决定）。
            // 好的做法是，如果块是新分配的，则不需要读取。
            // 但为了简单和安全，除非能明确知道块是全新的且空的，否则读取。
            // inode_manager_->getBlockIdForFileOffset 返回后，我们不知道这个块是旧的还是新的。
            // 一个改进：getBlockIdForFileOffset 可以返回一个指示块是否为新分配的标志。
            // 暂时总是读取（除非是写满整个块从头开始）。
            if (!vdisk_->readBlock(physical_block_id, temp_block_buffer.data(), block_size)) {
                 std::cerr << "错误 (writeFileData): 无法从物理块 " << physical_block_id << " 读取数据以进行部分写入。" << std::endl;
                 // 如果是新分配的块，读取失败可能不那么严重，因为我们会覆盖它。
                 // 但如果不是新块，则这是一个问题。
                 // 为了安全，如果读取失败，我们停止。
                 break;
            }
        } else {
            // 如果是从块的0偏移开始写满整个块，则不需要预读，直接覆盖。
            // (可以考虑用 std::memset(temp_block_buffer, 0, block_size) 清零，但这非必需，因为会被覆盖)
        }


        std::memcpy(temp_block_buffer.data() + offset_in_block, buffer + bytes_written, bytes_to_write_to_block);

        if (!vdisk_->writeBlock(physical_block_id, temp_block_buffer.data(), block_size)) {
            std::cerr << "错误 (writeFileData): 无法向物理块 " << physical_block_id << " 写入数据。" << std::endl;
            break; // 停止写入
        }

        bytes_written += bytes_to_write_to_block;
        current_offset += bytes_to_write_to_block;

        // 更新文件大小
        if (current_offset > inode.file_size) {
            inode.file_size = current_offset;
            sizeChanged = true;
        }
    }

    // 更新修改时间等 (示例)
    // inode.modification_time = time(nullptr);
    // inode.access_time = inode.modification_time;

    // 注意：inode 的修改（如 file_size, direct_blocks 等指针的更新）
    // 应该在 InodeManager::getBlockIdForFileOffset 中发生时，或者在这里之后，
    // 通过调用 InodeManager::writeInode 来持久化。
    // 如果 getBlockIdForFileOffset 修改了 inode（例如分配了新块并更新了指针），
    // 那么在循环结束后，或者在 getBlockIdForFileOffset 内部（如果它负责写回），
    // 需要确保 inode 被写回磁盘。
    // 目前的 InodeManager::getBlockIdForFileOffset 签名是传递 inode 引用，
    // 它可能会修改 inode 的 direct_blocks/indirect_blocks 字段。
    // 因此，在 writeFileData 成功返回前，需要确保这些修改被写回。
    // 这通常意味着在循环结束后调用 inode_manager_->writeInode(inode.inode_id, inode);
    // 但这应该由更高层（如 FileManager）来协调，因为它拥有 inode 的“所有权”和事务边界。
    // DataBlockManager 只负责数据块的IO和通过InodeManager获取块ID。

    // 如果在循环中 getBlockIdForFileOffset 修改了 inode 并立即写回，
    // 那么这里只需要处理文件大小的最终写回。
    // 假设 getBlockIdForFileOffset 内部 *不* 写回 inode，它只修改内存中的 inode 副本。
    // 那么，最终的 inode 写回是必要的。

    return bytes_written;
}

// 清除一个i-node所占用的所有数据块
// inode: 要清除数据块的i-node。其 direct_blocks, indirect_blocks 等将被清空（设为INVALID_BLOCK_ID），
//        并且 file_size 将被设为0。修改后的 inode 需要由调用者写回。
void DataBlockManager::clearInodeDataBlocks(Inode &inode) {
    if (!vdisk_ || !inode_manager_ || !sb_manager_) return;

    const SuperBlock& sb = sb_manager_->getSuperBlockInfo();
    int block_size = sb.block_size;
    int pointers_per_block = block_size / sizeof(int);
     if (pointers_per_block == 0) {
         std::cerr << "错误 (clearInodeDataBlocks): block_size " << block_size << " 太小，无法容纳任何块指针。" << std::endl;
         return;
    }

    // 1. 释放直接块
    for (int i = 0; i < NUM_DIRECT_BLOCKS; ++i) {
        if (inode.direct_blocks[i] != INVALID_BLOCK_ID) {
            sb_manager_->freeBlock(inode.direct_blocks[i]);
            inode.direct_blocks[i] = INVALID_BLOCK_ID;
        }
    }

    // 2. 释放一级间接块
    if (inode.single_indirect_block != INVALID_BLOCK_ID) {
        std::vector<char> indirect_block_buffer(block_size);
        if (vdisk_->readBlock(inode.single_indirect_block, indirect_block_buffer.data(), block_size)) {
            int* indirect_pointers = reinterpret_cast<int*>(indirect_block_buffer.data());
            for (int i = 0; i < pointers_per_block; ++i) {
                if (indirect_pointers[i] != INVALID_BLOCK_ID) {
                    sb_manager_->freeBlock(indirect_pointers[i]);
                    // indirect_pointers[i] = INVALID_BLOCK_ID; // 不需要写回这个修改，因为间接块本身要被释放
                }
            }
        } else {
            std::cerr << "警告 (clearInodeDataBlocks): 无法读取一级间接块 "
                      << inode.single_indirect_block << " 来释放其指向的数据块。" << std::endl;
            // 即使读取失败，仍然尝试释放间接块本身
        }
        sb_manager_->freeBlock(inode.single_indirect_block);
        inode.single_indirect_block = INVALID_BLOCK_ID;
    }

    // 3. 释放二级间接块
    if (inode.double_indirect_block != INVALID_BLOCK_ID) {
        std::vector<char> l1_indirect_buffer(block_size);
        if (vdisk_->readBlock(inode.double_indirect_block, l1_indirect_buffer.data(), block_size)) {
            int* l1_pointers = reinterpret_cast<int*>(l1_indirect_buffer.data());
            for (int i = 0; i < pointers_per_block; ++i) {
                if (l1_pointers[i] != INVALID_BLOCK_ID) { // l1_pointers[i] 是 L2 间接块的 ID
                    std::vector<char> l2_indirect_buffer(block_size);
                    if (vdisk_->readBlock(l1_pointers[i], l2_indirect_buffer.data(), block_size)) {
                        int* l2_pointers = reinterpret_cast<int*>(l2_indirect_buffer.data());
                        for (int j = 0; j < pointers_per_block; ++j) {
                            if (l2_pointers[j] != INVALID_BLOCK_ID) {
                                sb_manager_->freeBlock(l2_pointers[j]);
                            }
                        }
                    } else {
                         std::cerr << "警告 (clearInodeDataBlocks): 无法读取二级间接L2块 "
                                   << l1_pointers[i] << " 来释放其指向的数据块。" << std::endl;
                    }
                    sb_manager_->freeBlock(l1_pointers[i]); // 释放 L2 间接块本身
                }
            }
        } else {
             std::cerr << "警告 (clearInodeDataBlocks): 无法读取二级间接L1块 "
                       << inode.double_indirect_block << " 来释放其指向的L2块。" << std::endl;
        }
        sb_manager_->freeBlock(inode.double_indirect_block); // 释放 L1 间接块本身
        inode.double_indirect_block = INVALID_BLOCK_ID;
    }

    // 如果有三级间接块，也需要在这里处理

    inode.file_size = 0;
    // inode 的修改（指针和大小）需要由调用者通过 InodeManager::writeInode 写回。
}
