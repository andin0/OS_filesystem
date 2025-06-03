#include "fs_core/virtual_disk.h"
#include "common_defs.h"
#include "data_structures.h"
#include <fstream>
#include <iostream>
#include <vector>

// VirtualDisk 构造函数
// 初始化虚拟磁盘对象，记录磁盘文件路径和期望大小。
// diskFilePath: 虚拟磁盘文件的路径。
// diskSize: 虚拟磁盘的总大小（字节）。
VirtualDisk::VirtualDisk(const std::string &diskFilePath, long long diskSize)
    : diskFilePath_(diskFilePath), diskSize_(diskSize), totalBlocks_(0), blockSize_(DEFAULT_BLOCK_SIZE)
{
    // 尝试打开文件以确定实际大小和块大小（如果文件已存在）
    std::fstream diskFile(diskFilePath_, std::ios::in | std::ios::binary | std::ios::ate);
    if (diskFile.is_open())
    {
        long long existingSize = diskFile.tellg();
        if (existingSize > 0 && existingSize >= blockSize_)
        {                             // 假设 blockSize_ 至少是有效的
            diskSize_ = existingSize; // 使用现有文件大小
            totalBlocks_ = diskSize_ / blockSize_;
        }
        else if (existingSize == 0 && diskSize > 0)
        { // 文件存在但为空，或大小无效
            totalBlocks_ = diskSize_ / blockSize_;
        }
        // 如果 diskSize_ 仍然为0或者 blockSize_ 为0，则表示有问题
        // 但我们允许在 createDiskFile 时再最终确定
        diskFile.close();
    }
    else
    {
        // 文件不存在，将在 createDiskFile 时创建
        if (diskSize_ > 0 && blockSize_ > 0)
        {
            totalBlocks_ = diskSize_ / blockSize_;
        }
    }
}

// VirtualDisk 析构函数
// 目前不需要特殊操作。
VirtualDisk::~VirtualDisk()
{
    // 如果有文件流成员变量，确保在这里关闭
}

// 从虚拟磁盘读取一个数据块
// blockId: 要读取的块的ID。
// buffer: 用于存储读取数据的缓冲区。
// bufferSize: 缓冲区的实际大小，应等于或大于块大小。
// 返回值: 如果读取成功则为 true，否则为 false。
bool VirtualDisk::readBlock(int blockId, char *buffer, int bufferSize)
{
    if (blockId < 0 || blockId >= totalBlocks_)
    {
        std::cerr << "错误: 块ID " << blockId << " 超出范围 (0-" << totalBlocks_ - 1 << ")." << std::endl;
        return false;
    }
    if (bufferSize < blockSize_)
    {
        std::cerr << "错误: 缓冲区大小 " << bufferSize << " 小于块大小 " << blockSize_ << "." << std::endl;
        return false;
    }

    std::fstream diskFile(diskFilePath_, std::ios::in | std::ios::binary);
    if (!diskFile.is_open())
    {
        std::cerr << "错误: 无法打开磁盘文件 '" << diskFilePath_ << "' 进行读取。" << std::endl;
        return false;
    }

    diskFile.seekg(static_cast<long long>(blockId) * blockSize_, std::ios::beg);
    if (diskFile.fail())
    {
        std::cerr << "错误: 定位到块 " << blockId << " 失败。" << std::endl;
        diskFile.close();
        return false;
    }

    diskFile.read(buffer, blockSize_);
    if (diskFile.gcount() != blockSize_)
    {
        // 文件末尾或者读取错误，但对于模拟磁盘，我们期望总是能读满一个块（除非是最后一个不完整的块，但这不应该发生）
        std::cerr << "警告: 从块 " << blockId << " 读取的字节数 (" << diskFile.gcount() << ") 与期望的块大小 (" << blockSize_ << ") 不符。" << std::endl;
        // 根据实际情况，这里可能返回 false，或者用0填充剩余部分
        // 为简单起见，如果读取的字节数少于块大小，我们认为这是一个问题
        if (diskFile.eof() && diskFile.gcount() < blockSize_ && diskFile.gcount() > 0)
        {
            // 如果是文件末尾并且读到了一些数据，可以考虑填充
        }
        else if (diskFile.fail() && !diskFile.eof())
        {
            std::cerr << "错误: 从块 " << blockId << " 读取数据失败。" << std::endl;
            diskFile.close();
            return false;
        }
    }

    diskFile.close();
    return true;
}

// 向虚拟磁盘写入一个数据块
//  blockId: 要写入的块的ID。
//  buffer: 包含要写入数据的缓冲区。
//  bufferSize: 要写入的数据的大小，应等于块大小。
//  返回值: 如果写入成功则为 true，否则为 false。
bool VirtualDisk::writeBlock(int blockId, const char *buffer, int bufferSize)
{
    if (blockId < 0 || blockId >= totalBlocks_)
    {
        std::cerr << "错误: 块ID " << blockId << " 超出范围 (0-" << totalBlocks_ - 1 << ")." << std::endl;
        return false;
    }
    // 允许写入小于块大小的数据，但通常应该写入整个块
    if (bufferSize > blockSize_)
    {
        std::cerr << "警告: 写入数据大小 " << bufferSize << " 大于块大小 " << blockSize_ << ". 将只写入 " << blockSize_ << " 字节。" << std::endl;
        bufferSize = blockSize_; // 截断
    }

    // 使用 std::ios::in | std::ios::out 以便文件不存在时不会创建，而是依赖 createDiskFile
    // 但如果文件已存在，我们需要能够写入
    std::fstream diskFile(diskFilePath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!diskFile.is_open())
    {
        std::cerr << "错误: 无法打开磁盘文件 '" << diskFilePath_ << "' 进行写入。请确保文件已通过 createDiskFile 创建。" << std::endl;
        return false;
    }

    diskFile.seekp(static_cast<long long>(blockId) * blockSize_, std::ios::beg);
    if (diskFile.fail())
    {
        std::cerr << "错误: 定位到块 " << blockId << " 进行写入失败。" << std::endl;
        diskFile.close();
        return false;
    }

    diskFile.write(buffer, bufferSize);
    if (diskFile.fail())
    {
        std::cerr << "错误: 向块 " << blockId << " 写入数据失败。" << std::endl;
        diskFile.close();
        return false;
    }
    // 如果 bufferSize < blockSize_，块的剩余部分将保持原样或未定义，这取决于文件系统如何处理

    diskFile.close();
    return true;
}

// 获取虚拟磁盘的总块数
// 返回值: 总块数。
long long VirtualDisk::getTotalBlocks() const
{
    return totalBlocks_;
}

// 获取虚拟磁盘的块大小
// 返回值: 块大小（字节）。
int VirtualDisk::getBlockSize() const
{
    return blockSize_;
}

// 检查虚拟磁盘文件是否存在
// 返回值: 如果文件存在且可访问则为 true，否则为 false。
bool VirtualDisk::exists() const
{
    std::ifstream diskFile(diskFilePath_);
    return diskFile.good();
}

// 创建虚拟磁盘文件
// 如果文件已存在，此函数可能什么都不做，或者根据实现来调整文件大小。
// 当前实现：如果文件不存在，则创建并填充为指定大小；如果存在，验证大小。
// 返回值: 如果操作成功或文件已符合要求，则为 true，否则为 false。
bool VirtualDisk::createDiskFile()
{
    if (diskSize_ <= 0 || blockSize_ <= 0)
    {
        std::cerr << "错误: 无效的磁盘大小 (" << diskSize_ << ") 或块大小 (" << blockSize_ << ")。" << std::endl;
        return false;
    }

    totalBlocks_ = diskSize_ / blockSize_; // 重新计算以确保一致
    if (totalBlocks_ == 0 && diskSize_ > 0)
    { // 避免除以0后 totalBlocks_ 仍为0的情况
        std::cerr << "错误: 磁盘大小 " << diskSize_ << " 对于块大小 " << blockSize_ << " 太小，无法形成至少一个块。" << std::endl;
        return false;
    }

    std::fstream diskFile(diskFilePath_, std::ios::in | std::ios::out | std::ios::binary);

    if (diskFile.is_open())
    { // 文件已存在
        diskFile.seekg(0, std::ios::end);
        long long existingSize = diskFile.tellg();
        if (existingSize == diskSize_)
        {
            std::cout << "信息: 磁盘文件 '" << diskFilePath_ << "' 已存在且大小正确 (" << diskSize_ << " 字节)。" << std::endl;
            diskFile.close();
            return true;
        }
        else if (existingSize > 0)
        {
            std::cout << "警告: 磁盘文件 '" << diskFilePath_ << "' 已存在，但大小 (" << existingSize
                      << " 字节) 与期望大小 (" << diskSize_ << " 字节) 不符。将使用现有大小。" << std::endl;
            diskSize_ = existingSize;
            totalBlocks_ = diskSize_ / blockSize_;
            if (totalBlocks_ == 0 && diskSize_ > 0)
            {
                std::cerr << "错误: 现有磁盘大小 " << diskSize_ << " 对于块大小 " << blockSize_ << " 太小。" << std::endl;
                diskFile.close();
                return false;
            }
            diskFile.close();
            return true; // 或许应该返回false并要求用户处理？当前选择是接受现有文件。
        }
        // 如果 existingSize 为 0，则行为类似于文件不存在，继续创建
        diskFile.close(); // 关闭后以 trunc 模式重新打开以清空并设置大小
    }

    // 文件不存在或为空，创建新文件
    std::ofstream newDiskFile(diskFilePath_, std::ios::binary | std::ios::trunc);
    if (!newDiskFile.is_open())
    {
        std::cerr << "错误: 无法创建或打开磁盘文件 '" << diskFilePath_ << "' 进行初始化。" << std::endl;
        return false;
    }

    // 将文件扩展到指定大小，用零填充
    // 注意：直接 seekp 然后 write 一个字节可能不足以保证文件在所有系统上都被正确分配空间
    // 更可靠（但可能较慢）的方法是逐块写入或写入一个大的零缓冲区
    std::vector<char> zeroBlock(blockSize_, 0); // 创建一个全零的块
    for (long long i = 0; i < totalBlocks_; ++i)
    {
        if (!newDiskFile.write(zeroBlock.data(), blockSize_))
        {
            std::cerr << "错误: 初始化磁盘文件时写入块 " << i << " 失败。" << std::endl;
            newDiskFile.close();
            // 可以考虑删除部分创建的文件
            std::remove(diskFilePath_.c_str());
            return false;
        }
    }
    // 处理磁盘大小不是块大小整数倍的剩余部分
    long long remainder = diskSize_ % blockSize_;
    if (remainder > 0)
    {
        if (!newDiskFile.write(zeroBlock.data(), remainder))
        {
            std::cerr << "错误: 初始化磁盘文件时写入剩余 " << remainder << " 字节失败。" << std::endl;
            newDiskFile.close();
            std::remove(diskFilePath_.c_str());
            return false;
        }
    }

    std::cout << "信息: 虚拟磁盘文件 '" << diskFilePath_ << "' 已成功创建并初始化为 " << diskSize_ << " 字节。" << std::endl;
    newDiskFile.close();
    return true;
}
