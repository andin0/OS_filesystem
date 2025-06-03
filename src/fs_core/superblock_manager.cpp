#include "fs_core/superblock_manager.h"
#include "fs_core/virtual_disk.h"
#include "common_defs.h"
#include "data_structures.h"
#include <iostream>
#include <vector>
#include <cstring> // For std::memcpy and std::memset
#include <algorithm> // For std::min

// SuperBlockManager 构造函数
// vdisk: 指向 VirtualDisk 对象的指针。
SuperBlockManager::SuperBlockManager(VirtualDisk *vdisk)
    : vdisk_(vdisk), superblock_({}) 
    {
    if (!vdisk_) {
        throw std::runtime_error("SuperBlockManager: VirtualDisk 指针为空。");
    }
}

// 从虚拟磁盘加载超级块
bool SuperBlockManager::loadSuperBlock() {
    if (!vdisk_) return false;
    char buffer[DEFAULT_BLOCK_SIZE];
    if (!vdisk_->readBlock(0, buffer, vdisk_->getBlockSize())) {
        std::cerr << "错误: SuperBlockManager 无法从磁盘读取块 0 (超级块)。" << std::endl;
        return false;
    }
    std::memcpy(&superblock_, buffer, sizeof(SuperBlock));

    if (superblock_.magic_number != FILESYSTEM_MAGIC_NUMBER) {
        std::cerr << "错误: 无效的文件系统魔数。磁盘可能未格式化或已损坏。" << std::endl;
        superblock_ = {};
        return false;
    }
    if (superblock_.block_size != vdisk_->getBlockSize()) {
        std::cerr << "警告: 超级块中的 block_size (" << superblock_.block_size
                  << ") 与虚拟磁盘的 block_size (" << vdisk_->getBlockSize()
                  << ") 不匹配。这可能导致严重问题。" << std::endl;
        // 考虑返回 false 或采取纠正措施
    }
    std::cout << "信息: 超级块已成功加载。" << std::endl;
    return true;
}

// 将当前内存中的超级块保存到虚拟磁盘
bool SuperBlockManager::saveSuperBlock() {
    if (!vdisk_) return false;
    char buffer[DEFAULT_BLOCK_SIZE];
    std::memset(buffer, 0, vdisk_->getBlockSize());
    std::memcpy(buffer, &superblock_, sizeof(SuperBlock));
    if (!vdisk_->writeBlock(0, buffer, vdisk_->getBlockSize())) {
        std::cerr << "错误: SuperBlockManager 无法将超级块写入磁盘块 0。" << std::endl;
        return false;
    }
    return true;
}

// Helper: 读取 i-node 位图的一个块
bool SuperBlockManager::readInodeBitmapBlock(int bitmap_block_offset, char* buffer) const {
    if (!vdisk_ || bitmap_block_offset < 0 || bitmap_block_offset >= superblock_.inode_bitmap_blocks_count) {
        std::cerr << "错误 (readInodeBitmapBlock): 无效的位图块偏移 " << bitmap_block_offset << std::endl;
        return false;
    }
    int actual_disk_block_id = superblock_.inode_bitmap_start_block_idx + bitmap_block_offset;
    return vdisk_->readBlock(actual_disk_block_id, buffer, superblock_.block_size);
}

// Helper: 写入 i-node 位图的一个块
bool SuperBlockManager::writeInodeBitmapBlock(int bitmap_block_offset, const char* buffer) {
     if (!vdisk_ || bitmap_block_offset < 0 || bitmap_block_offset >= superblock_.inode_bitmap_blocks_count) {
        std::cerr << "错误 (writeInodeBitmapBlock): 无效的位图块偏移 " << bitmap_block_offset << std::endl;
        return false;
    }
    int actual_disk_block_id = superblock_.inode_bitmap_start_block_idx + bitmap_block_offset;
    return vdisk_->writeBlock(actual_disk_block_id, buffer, superblock_.block_size);
}

// Helper: 获取 i-node 位图中指定 i-node ID 的状态 (是否已使用)
// inodeId: 要检查的 i-node ID。
// isSet: 输出参数，如果 i-node 已使用则为 true，否则为 false。
// 返回值: 操作是否成功。
bool SuperBlockManager::getInodeBit(int inodeId, bool& isSet) const {
    if (inodeId < 0 || inodeId >= superblock_.total_inodes) {
        std::cerr << "错误 (getInodeBit): i-node ID " << inodeId << " 超出范围。" << std::endl;
        return false;
    }

    int bit_offset = inodeId;
    int block_offset_in_bitmap = bit_offset / (superblock_.block_size * 8);
    int byte_offset_in_block = (bit_offset / 8) % superblock_.block_size;
    int bit_offset_in_byte = bit_offset % 8;

    if (block_offset_in_bitmap >= superblock_.inode_bitmap_blocks_count) {
        std::cerr << "错误 (getInodeBit): 计算得到的位图块偏移 " << block_offset_in_bitmap << " 超出范围。" << std::endl;
        return false;
    }

    std::vector<char> bitmap_block_buffer(superblock_.block_size); 
    if (!readInodeBitmapBlock(block_offset_in_bitmap, bitmap_block_buffer.data())) {
        return false;
    }

    isSet = (bitmap_block_buffer[byte_offset_in_block] >> bit_offset_in_byte) & 1;
    return true;
}

// Helper: 设置 i-node 位图中指定 i-node ID 的状态
// inodeId: 要设置的 i-node ID。
// setToUsed: true 表示标记为已使用 (1)，false 表示标记为空闲 (0)。
// 返回值: 操作是否成功。
bool SuperBlockManager::setInodeBit(int inodeId, bool setToUsed) {
    if (inodeId < 0 || inodeId >= superblock_.total_inodes) {
        std::cerr << "错误 (setInodeBit): i-node ID " << inodeId << " 超出范围。" << std::endl;
        return false;
    }

    int bit_offset = inodeId;
    int block_offset_in_bitmap = bit_offset / (superblock_.block_size * 8);
    int byte_offset_in_block = (bit_offset / 8) % superblock_.block_size;
    int bit_offset_in_byte = bit_offset % 8;

    if (block_offset_in_bitmap >= superblock_.inode_bitmap_blocks_count) {
        std::cerr << "错误 (setInodeBit): 计算得到的位图块偏移 " << block_offset_in_bitmap << " 超出范围。" << std::endl;
        return false;
    }

    // char bitmap_block_buffer[superblock_.block_size]; // <--- 原来的问题行
    std::vector<char> bitmap_block_buffer(superblock_.block_size); // <--- 修改后的行

    // 使用 .data() 获取指向vector内部数据的指针传递给C风格API
    if (!readInodeBitmapBlock(block_offset_in_bitmap, bitmap_block_buffer.data())) {
        return false; // 读取失败
    }

    if (setToUsed) {
        //可以直接使用[]访问vector元素
        bitmap_block_buffer[byte_offset_in_block] |= (1 << bit_offset_in_byte);
    } else {
        bitmap_block_buffer[byte_offset_in_block] &= ~(1 << bit_offset_in_byte);
    }

    // 使用 .data() 获取指向vector内部数据的指针传递给C风格API
    if (!writeInodeBitmapBlock(block_offset_in_bitmap, bitmap_block_buffer.data())) {
        return false; // 写入失败
    }
    return true;
}


// 格式化文件系统
bool SuperBlockManager::formatFileSystem(int totalInodes, int blockSize) {
    if (!vdisk_) return false;
    if (blockSize <= 0 || totalInodes <= 0) {
        std::cerr << "错误: 无效的块大小 (" << blockSize << ") 或 i-node 总数 (" << totalInodes << ")。" << std::endl;
        return false;
    }
    if (vdisk_->getBlockSize() != blockSize) {
        std::cerr << "错误: 请求的块大小 (" << blockSize
                  << ") 与虚拟磁盘的块大小 (" << vdisk_->getBlockSize()
                  << ") 不匹配。" << std::endl;
        return false;
    }

    superblock_ = {}; // 清空现有超级块

    superblock_.magic_number = FILESYSTEM_MAGIC_NUMBER;
    superblock_.block_size = blockSize;
    superblock_.inode_size = INODE_SIZE_BYTES;
    superblock_.total_blocks = vdisk_->getTotalBlocks();
    superblock_.total_inodes = totalInodes;

    // 1. 计算 i-node 位图所需的空间
    int bits_per_block = blockSize * 8;
    superblock_.inode_bitmap_blocks_count = (totalInodes + bits_per_block - 1) / bits_per_block;
    superblock_.inode_bitmap_start_block_idx = 1; // 位图紧随超级块之后

    // 2. 计算 i-node 表所需的空间
    int inodes_per_block = blockSize / superblock_.inode_size;
    if (inodes_per_block == 0) {
        std::cerr << "错误: 块大小 " << blockSize << " 对于 i-node 大小 " << superblock_.inode_size << " 太小。" << std::endl;
        return false;
    }
    int inode_table_blocks_count = (totalInodes + inodes_per_block - 1) / inodes_per_block;
    superblock_.inode_table_start_block_idx = superblock_.inode_bitmap_start_block_idx + superblock_.inode_bitmap_blocks_count;

    // 3. 计算第一个数据块的起始位置
    superblock_.first_data_block_idx = superblock_.inode_table_start_block_idx + inode_table_blocks_count;

    if (superblock_.first_data_block_idx >= superblock_.total_blocks) {
        std::cerr << "错误: 磁盘空间不足以容纳超级块、i-node位图、i-node表和至少一个数据块。" << std::endl;
        std::cerr << "  总块数: " << superblock_.total_blocks << std::endl;
        std::cerr << "  超级块: 1 块" << std::endl;
        std::cerr << "  i-node位图: " << superblock_.inode_bitmap_blocks_count << " 块" << std::endl;
        std::cerr << "  i-node表: " << inode_table_blocks_count << " 块" << std::endl;
        std::cerr << "  所需最小块数 (元数据 + 1数据块): " << superblock_.first_data_block_idx + 1 << std::endl;
        return false;
    }

    superblock_.free_blocks_count = superblock_.total_blocks - superblock_.first_data_block_idx;
    // free_inodes_count 将在初始化位图后设置

    superblock_.root_dir_inode_idx = ROOT_DIRECTORY_INODE_ID;
    superblock_.max_filename_length = MAX_FILENAME_LENGTH;
    superblock_.max_path_length = MAX_PATH_LENGTH;

    // 初始化 i-node 位图 (所有位清零)
    std::vector<char> zero_buffer(blockSize);
    std::memset(zero_buffer.data(), 0, blockSize);
    for (int i = 0; i < superblock_.inode_bitmap_blocks_count; ++i) {
        if (!writeInodeBitmapBlock(i, zero_buffer.data())) { // 使用相对位图块的偏移
            std::cerr << "错误: 格式化期间初始化i-node位图块 " << i << " 失败。" << std::endl;
            return false;
        }
    }

    // 分配根目录的 i-node (标记位图中的第 ROOT_DIRECTORY_INODE_ID 位为1)
    if (!setInodeBit(ROOT_DIRECTORY_INODE_ID, true)) {
        std::cerr << "错误: 格式化期间无法标记根i-node " << ROOT_DIRECTORY_INODE_ID << " 为已使用。" << std::endl;
        return false;
    }
    superblock_.free_inodes_count = superblock_.total_inodes - 1; // 减去根i-node

    // 初始化成组链接法的空闲块堆栈
    initializeFreeBlockGroups();

    if (!saveSuperBlock()) {
        std::cerr << "错误: 格式化期间保存超级块失败。" << std::endl;
        return false;
    }

    // 清空i-node表区域 (可选，但推荐)
    std::memset(zero_buffer.data(), 0, blockSize);
    for (int i = 0; i < inode_table_blocks_count; ++i) {
        if (!vdisk_->writeBlock(superblock_.inode_table_start_block_idx + i, zero_buffer.data(), blockSize)) {
            std::cerr << "警告: 格式化期间清空i-node表块 " << (superblock_.inode_table_start_block_idx + i) << " 失败。" << std::endl;
        }
    }

    std::cout << "信息: 文件系统已成功格式化 (使用i-node位图)。" << std::endl;
    std::cout << "  i-node位图起始块: " << superblock_.inode_bitmap_start_block_idx << ", 占用: " << superblock_.inode_bitmap_blocks_count << " 块" << std::endl;
    std::cout << "  i-node表起始块: " << superblock_.inode_table_start_block_idx << ", 占用: " << inode_table_blocks_count << " 块" << std::endl;
    std::cout << "  第一个数据块索引: " << superblock_.first_data_block_idx << std::endl;
    std::cout << "  空闲i-node数: " << superblock_.free_inodes_count << std::endl;

    return true;
}

// 初始化成组链接法的空闲块组 (与之前版本基本一致)
void SuperBlockManager::initializeFreeBlockGroups() {
    if (superblock_.free_blocks_count == 0) {
        superblock_.free_block_stack_top_idx = INVALID_BLOCK_ID;
        return;
    }

    std::vector<int> available_blocks_for_groups_and_data;
    for (long long i = superblock_.first_data_block_idx; i < superblock_.total_blocks; ++i) {
        available_blocks_for_groups_and_data.push_back(static_cast<int>(i));
    }
    // superblock_.free_blocks_count 已经在 formatFileSystem 中正确设置了初始值

    if (available_blocks_for_groups_and_data.empty()) {
        superblock_.free_block_stack_top_idx = INVALID_BLOCK_ID;
        return;
    }

    const int ids_per_group_block = (superblock_.block_size / sizeof(int)) - 1;
    if (ids_per_group_block <= 0) {
        std::cerr << "错误 (initializeFreeBlockGroups): block_size " << superblock_.block_size << " 太小。" << std::endl;
        superblock_.free_block_stack_top_idx = INVALID_BLOCK_ID;
        return;
    }

    std::vector<char> block_buffer(superblock_.block_size);
    FreeBlockGroup* current_group_struct = reinterpret_cast<FreeBlockGroup*>(block_buffer.data());
    int next_super_group_block_id = INVALID_BLOCK_ID;

    // 从后向前取块作为组头，这样栈顶组的ID较高
    while (!available_blocks_for_groups_and_data.empty()) {
        int current_s_group_block_id = available_blocks_for_groups_and_data.back();
        available_blocks_for_groups_and_data.pop_back();

        std::memset(block_buffer.data(), 0, superblock_.block_size);
        current_group_struct->count = 0;

        if (next_super_group_block_id != INVALID_BLOCK_ID) {
            if (current_group_struct->count < N_FREE_BLOCKS_PER_GROUP) { // N_FREE_BLOCKS_PER_GROUP 是数组大小
                 current_group_struct->next_group_block_ids[current_group_struct->count++] = next_super_group_block_id;
            } else { // 这种情况不应该发生，因为我们刚取出一个块做组头，count 应该是0
                std::cerr << "错误 (initializeFreeBlockGroups): 组已满，无法存储 next_super_group_block_id。" << std::endl;
                available_blocks_for_groups_and_data.push_back(current_s_group_block_id); // 归还
                break;
            }
        }

        while(current_group_struct->count < N_FREE_BLOCKS_PER_GROUP && !available_blocks_for_groups_and_data.empty()){
            current_group_struct->next_group_block_ids[current_group_struct->count++] = available_blocks_for_groups_and_data.back();
            available_blocks_for_groups_and_data.pop_back();
        }
        vdisk_->writeBlock(current_s_group_block_id, block_buffer.data(), superblock_.block_size);
        next_super_group_block_id = current_s_group_block_id;
    }
    superblock_.free_block_stack_top_idx = next_super_group_block_id;
}


// 分配一个空闲数据块 (与之前版本基本一致)
int SuperBlockManager::allocateBlock() {
    if (superblock_.free_blocks_count == 0 || superblock_.free_block_stack_top_idx == INVALID_BLOCK_ID) {
        std::cerr << "错误: 没有空闲数据块可分配。" << std::endl;
        return INVALID_BLOCK_ID;
    }

    std::vector<char> buffer(superblock_.block_size);
    FreeBlockGroup* group_block = reinterpret_cast<FreeBlockGroup*>(buffer.data());

    if (!vdisk_->readBlock(superblock_.free_block_stack_top_idx, buffer.data(), superblock_.block_size)) {
        std::cerr << "错误: 无法读取空闲块组 " << superblock_.free_block_stack_top_idx << std::endl;
        return INVALID_BLOCK_ID;
    }

    if (group_block->count == 0) {
        std::cerr << "错误: 空闲块组 " << superblock_.free_block_stack_top_idx << " 为空 (count=0)。" << std::endl;
        return INVALID_BLOCK_ID;
    }

    group_block->count--;
    int allocated_block_id = group_block->next_group_block_ids[group_block->count];


    if (group_block->count == 0) { // 组内指针用完，切换到下一组
        // 假设 next_group_block_ids[0] (当count减为0之前，它是最后一个有效指针的前一个)
        // 或者说，当count最初是1时，next_group_block_ids[0]是最后一个数据块指针，
        // 此时，这个组块本身需要变成下一个组（如果它之前链接了的话）。
        // 经典成组链接：当一个组的count减到0，它所指向的下一个组（通常是它自己存储的第一个指针）成为新的栈顶
        // 并且这个旧的组块被释放。
        // 简化：如果count为0，我们假设这个组块已空，并且在初始化时，
        // next_group_block_ids[0] (当count > 0时) 指向下一个“超级组”。
        // 当分配到只剩一个指针（即count=1，这个指针是next_group_block_ids[0]），
        // 分配它之后，count=0。此时，superblock_.free_block_stack_top_idx 应该更新为
        // 之前存储在这个组块中的“下一组”的指针。
        // 这个逻辑在 initializeFreeBlockGroups 和 freeBlock 中更为关键。
        // 在 allocateBlock 中，如果 count 变为0，则意味着这个组块的最后一个“数据块指针”被分配了。
        // 它内部应该有一个指针指向下一个组。
        // 假设这个指针是 next_group_block_ids[0] （在 count 减为 0 之前，它被视为数据指针，
        // 但如果它是最后一个，且这个组是“超级组”，它实际上是链接指针）。
        // 这是一个复杂点。
        // 按照《操作系统概念》的成组链接法：
        // S.count 是组内空闲块数（不包括指向下一组的指针）
        // S.free[0] 是下一组的指针
        // S.free[1]...S.free[S.count] 是空闲块
        // 分配 S.free[S.count]，然后 S.count--.
        // 如果 S.count == 0，则 superblock_.free_block_stack_top_idx = S.free[0].
        // 我们的 FreeBlockGroup 结构是：count, next_group_block_ids[N_FREE_BLOCKS_PER_GROUP]
        // 分配：allocated_block_id = group_block->next_group_block_ids[group_block->count]; (count已经是减1后的)
        // 如果 group_block->count == 0 (表示这个组的“数据块指针”用完了)
        // 那么，这个组块本身（superblock_.free_block_stack_top_idx）需要被处理。
        // 它应该包含指向下一个“超级组”的指针。
        // 假设这个指针存储在 group_block->next_group_block_ids[0] 当 count 为特殊值时，
        // 或者在初始化时，如果一个组满了，它的最后一个指针指向下一个组。

        // 采用《操作系统概念》模型：
        // allocated_block_id = group_block->next_group_block_ids[group_block->count]; (count已经是减1后的)
        // 如果 group_block->count == 0 (表示这个组的“数据块指针”用完了)
        // 此时，group_block->next_group_block_ids[0] （在count减为0之前，它是第一个数据指针）
        // 应该被视为指向下一个组的指针。
        // 这个逻辑在 `freeBlock` 中当栈顶组满时，新的块成为新的栈顶组，
        // 它的 `next_group_block_ids[0]` 会存储旧的栈顶。
        // 所以，当 `count` 变为0时，`next_group_block_ids[0]` 就是下一个栈顶。
        int next_stack_top = group_block->next_group_block_ids[0]; // 假设这是链接
        // 旧的组块 superblock_.free_block_stack_top_idx 现在是空闲的，但它不是一个“数据块”
        // 在成组链接法中，当一个组被耗尽，它通常不会立即被“释放”为普通数据块，
        // 而是链表指针更新。
        superblock_.free_block_stack_top_idx = next_stack_top;
        // 注意：这里没有写回 group_block 的修改（count=0），因为这个块即将不再是栈顶。
    } else {
        // 如果组未空，需要将修改后的组（count减少）写回磁盘
        if (!vdisk_->writeBlock(superblock_.free_block_stack_top_idx, buffer.data(), superblock_.block_size)) {
            std::cerr << "错误: 更新空闲块组 " << superblock_.free_block_stack_top_idx << " 失败。" << std::endl;
            // 回滚操作？这很复杂。可能需要标记文件系统为不一致状态。
            // superblock_.free_blocks_count++; // 尝试恢复计数，但不安全
            return INVALID_BLOCK_ID; // 分配失败
        }
    }
    superblock_.free_blocks_count--; // 这个应该在分配成功后执行
    if (!saveSuperBlock()) {
        std::cerr << "警告: 分配块后保存超级块失败。" << std::endl;
    }
    return allocated_block_id;
}

// 释放一个数据块 (与之前版本基本一致，但适配了成组链接法模型)
void SuperBlockManager::freeBlock(int blockId) {
    if (blockId < superblock_.first_data_block_idx || blockId >= superblock_.total_blocks) {
        std::cerr << "警告: 尝试释放一个无效的数据块ID " << blockId << "." << std::endl;
        return;
    }

    std::vector<char> buffer(superblock_.block_size);
    FreeBlockGroup* group_block_struct = reinterpret_cast<FreeBlockGroup*>(buffer.data());
    // const int ids_per_group_block = (superblock_.block_size / sizeof(int)) - 1;
    // N_FREE_BLOCKS_PER_GROUP 是数组大小，不是ids_per_group_block

    bool stack_top_is_full = false;
    if (superblock_.free_block_stack_top_idx != INVALID_BLOCK_ID) {
        if (!vdisk_->readBlock(superblock_.free_block_stack_top_idx, buffer.data(), superblock_.block_size)) {
            std::cerr << "错误: 释放块时无法读取栈顶空闲组 " << superblock_.free_block_stack_top_idx << std::endl;
            return;
        }
        if (group_block_struct->count >= N_FREE_BLOCKS_PER_GROUP) { // 组内指针数组已满
            stack_top_is_full = true;
        }
    } else {
        stack_top_is_full = true; // 没有栈顶组，视为已满
    }

    if (stack_top_is_full) {
        // 当前栈顶组已满，将要释放的 blockId 自身变成新的栈顶组。
        // 新组的第一个指针指向旧的栈顶组。
        std::memset(buffer.data(), 0, superblock_.block_size);
        group_block_struct->count = 0; // 初始化计数
        if (superblock_.free_block_stack_top_idx != INVALID_BLOCK_ID) {
             group_block_struct->next_group_block_ids[group_block_struct->count++] = superblock_.free_block_stack_top_idx;
        }
        // 新的栈顶是 blockId
        superblock_.free_block_stack_top_idx = blockId;
        if (!vdisk_->writeBlock(superblock_.free_block_stack_top_idx, buffer.data(), superblock_.block_size)) {
            std::cerr << "错误: 无法将块 " << blockId << " 初始化为新的空闲组。" << std::endl;
            // 回滚 superblock_.free_block_stack_top_idx ?
            return;
        }
    } else {
        // 当前栈顶组未满，直接将 blockId 添加到其中
        // (此时 buffer 中已经是栈顶组的内容)
        group_block_struct->next_group_block_ids[group_block_struct->count++] = blockId;
        if (!vdisk_->writeBlock(superblock_.free_block_stack_top_idx, buffer.data(), superblock_.block_size)) {
            std::cerr << "错误: 无法将块 " << blockId << " 添加到空闲组 " << superblock_.free_block_stack_top_idx << std::endl;
            return;
        }
    }

    superblock_.free_blocks_count++;
    if (!saveSuperBlock()) {
        std::cerr << "警告: 释放块后保存超级块失败。" << std::endl;
    }
}

// 分配一个空闲i-node (使用i-node位图)
int SuperBlockManager::allocateInode() {
    if (superblock_.free_inodes_count == 0) {
        std::cerr << "信息: 没有空闲i-node可分配。" << std::endl;
        return INVALID_INODE_ID;
    }

    for (int i = 0; i < superblock_.total_inodes; ++i) {
        bool is_used;
        if (!getInodeBit(i, is_used)) {
            std::cerr << "错误: 检查i-node " << i << " 状态失败。" << std::endl;
            return INVALID_INODE_ID; // 严重错误，无法继续
        }
        if (!is_used) {
            // 找到一个空闲i-node
            if (!setInodeBit(i, true)) {
                std::cerr << "错误: 标记i-node " << i << " 为已使用失败。" << std::endl;
                return INVALID_INODE_ID; // 严重错误
            }
            superblock_.free_inodes_count--;
            if (!saveSuperBlock()) {
                std::cerr << "警告: 分配i-node " << i << " 后保存超级块失败。" << std::endl;
                // 应该回滚 setInodeBit 和 free_inodes_count 吗？复杂。
                // 暂时不回滚，但标记文件系统可能不一致。
            }
            // 在这里可以进行新分配inode的初始化清零操作，或者由InodeManager负责
            // Inode new_inode_content = {}; // 创建一个全零的inode
            // new_inode_content.inode_id = i; // 如果inode结构体中存储id
            // InodeManager temp_im(vdisk_, this); // 不推荐这样临时创建
            // if (!temp_im.writeInode(i, new_inode_content)) {
            //    std::cerr << "警告: 初始化新分配的inode " << i << " 失败。" << std::endl;
            // }
            return i;
        }
    }

    std::cerr << "错误: free_inodes_count > 0 但未能在位图中找到空闲i-node。位图可能已损坏。" << std::endl;
    return INVALID_INODE_ID; // 未找到 (理论上不应发生如果free_inodes_count正确)
}

// 释放一个i-node (使用i-node位图)
void SuperBlockManager::freeInode(int inodeId) {
    if (inodeId < 0 || inodeId >= superblock_.total_inodes) {
        std::cerr << "警告: 尝试释放一个无效的i-node ID " << inodeId << "." << std::endl;
        return;
    }

    bool is_used;
    if (!getInodeBit(inodeId, is_used)) {
        std::cerr << "错误: 检查i-node " << inodeId << " 状态以进行释放失败。" << std::endl;
        return;
    }

    if (!is_used) {
        std::cerr << "警告: 尝试释放一个已经是空闲的i-node " << inodeId << "." << std::endl;
        return;
    }

    if (!setInodeBit(inodeId, false)) {
        std::cerr << "错误: 标记i-node " << inodeId << " 为空闲失败。" << std::endl;
        return;
    }

    superblock_.free_inodes_count++;
    if (superblock_.free_inodes_count > superblock_.total_inodes) {
         std::cerr << "警告: free_inodes_count (" << superblock_.free_inodes_count
                   << ") 超出 total_inodes (" << superblock_.total_inodes
                   << ") 在释放 inode " << inodeId << " 后。" << std::endl;
        superblock_.free_inodes_count = superblock_.total_inodes; // 校正
    }

    if (!saveSuperBlock()) {
        std::cerr << "警告: 释放i-node " << inodeId << " 后保存超级块失败。" << std::endl;
    }
}

const SuperBlock &SuperBlockManager::getSuperBlockInfo() const {
    return superblock_;
}
