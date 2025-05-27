class UserManager
{
public:
    UserManager(InodeManager *inodeManager, DirectoryManager *dirManager, SuperBlockManager *sbManager);
    bool initializeUsers();
    User *login(const std::string &username, const std::string &password); // User 在 data_structures.h
    void logout();
    User *getCurrentUser() const;
    bool addUser(const std::string &username, const std::string &password);
    bool checkAccessPermission(const Inode &inode, PermissionAction action) const; // 检查当前用户对某inode是否有某种操作权限 // PermissionAction 在 common_defs.h
private:
    std::vector<User> user_database_;
    User *current_user_;
};