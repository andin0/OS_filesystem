#include "all_includes.h"

int main(int argc, char *argv[])
{
    // Check command line arguments
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <disk_file_path> [disk_size_in_bytes]" << std::endl;
        return 1;
    }

    std::string diskFilePath = argv[1];
    long long diskSize = (argc >= 3) ? std::stoll(argv[2]) : DEFAULT_DISK_SIZE;

    // Initialize the file system
    FileSystem fs(diskFilePath, diskSize);

    // Mount the file system
    if (!fs.mount())
    {
        std::cerr << "Failed to mount the file system." << std::endl;
        return 1;
    }

    // Start the shell
    Shell shell(&fs);
    shell.run();

    return 0;
}