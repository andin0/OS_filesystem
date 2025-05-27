class FileSystem
{
public:
    FileSystem(const std::string &diskFilePath, long long diskSize);
    ~FileSystem();
    bool mount();
    bool format();
    bool loginUser(const std::string &username, const std::string &password);
    void logoutUser();
    bool mkdir(const std::string &path);
    bool chdir(const std::string &path);
    std::string dir(const std::string &path);
    bool create(const std::string &path);
    int open(const std::string &path, OpenMode mode); // OpenMode 在 common_defs.h
    bool close(int fd);
    int read(int fd, char *buffer, int length);
    int write(int fd, const char *buffer, int length);
    bool rm(const std::string &path, bool recursive, bool force);
    bool cp(const std::string &sourcePath, const std::string &destPath, bool recursive);
    bool mv(const std::string &sourcePath, const std::string &destPath);
    bool ln(const std::string &targetPath, const std::string &linkPath);
    bool chmod(const std::string &path, short mode);
    bool chown(const std::string &path, const std::string &newOwnerUsername);
    std::vector<std::string> find(const std::string &startPath, const std::string &filename);
    std::string getCurrentPathPrompt() const;

private:
    VirtualDisk vdisk_;
    SuperBlockManager sb_manager_;
    InodeManager inode_manager_;
    DataBlockManager db_manager_;
    DirectoryManager dir_manager_;
    FileManager file_manager_;
    UserManager user_manager_;
    int current_dir_inode_id_;
    int root_dir_inode_id_;
    std::vector<ProcessOpenFileEntry> process_open_file_table_; // ProcessOpenFileEntry 在 data_structures.h
    std::vector<SystemOpenFileEntry> system_open_file_table_;   // SystemOpenFileEntry 在 data_structures.h
    int getFreeFd();
    void releaseFd(int fd);
    // bool recursiveDelete(int dirInodeId); // This logic will be part of rm or a helper called by rm
    // bool recursiveCopy(int sourceDirInodeId, int destParentDirInodeId, const std::string& newName); // This logic will be part of cp or a helper called by cp
};