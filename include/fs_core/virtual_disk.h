#include "all_includes.h"
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
};