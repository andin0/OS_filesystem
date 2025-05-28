#include "user_manager.h"

UserManager::UserManager()
{
    this->current_user_ = nullptr;
}

User *UserManager::login(const std::string &username, const std::string &password)
{
    for (auto &user : UserManager::user_database_)
    {
        if (user.username == username && user.password == password)
        {
            this->current_user_ = new User(user);
            return this->current_user_;
        }
    }
    return nullptr; // 登录失败，用户名或密码错误
}

void UserManager::logout()
{
    if (this->current_user_ != nullptr)
    {
        delete this->current_user_;
        this->current_user_ = nullptr;
    }
}

User *UserManager::getCurrentUser() const
{
    return this->current_user_;
}

bool UserManager::checkAccessPermission(const Inode &inode, PermissionAction action) const
{
    if (this->current_user_ == nullptr)
    {
        return false; // No user logged in
    }

    short permissions = inode.permissions;

    switch (action)
    {
    case PermissionAction::ACTION_READ:
        return (permissions & PERM_USER_READ) || (permissions & PERM_GROUP_READ) || (permissions & PERM_OTHER_READ);
    case PermissionAction::ACTION_WRITE:
        return (permissions & PERM_USER_WRITE) || (permissions & PERM_GROUP_WRITE) || (permissions & PERM_OTHER_WRITE);
    case PermissionAction::ACTION_EXECUTE:
        return (permissions & PERM_USER_EXEC) || (permissions & PERM_GROUP_EXEC) || (permissions & PERM_OTHER_EXEC);
    default:
        return false; // Unknown action
    }
}
