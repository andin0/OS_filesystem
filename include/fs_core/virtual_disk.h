#ifndef VIRTUAL_DISK_H
#define VIRTUAL_DISK_H
#include <string>
class VirtualDisk
{
public:
    VirtualDisk(const std::string &diskFilePath, long long diskSize);
    ~VirtualDisk();
    bool readBlock(int blockId, char *buffer, int bufferSize);
    bool writeBlock(int blockId, const char *buffer, int bufferSize);
    long long getTotalBlocks() const;
    int getBlockSize() const;
    bool exists() const;
    bool createDiskFile();

private:
    std::string diskFilePath_; // ✅ 添加此行：磁盘文件路径
    long long diskSize_;
    long long totalBlocks_;
    long long blockSize_;
};
#endif // !VIRTUAL_DISK_H