#ifndef USER_MANAGER_H
#define USER_MANAGER_H

#include <string>
#include <vector>
#include "data_structures.h"
#include "common_defs.h"

class UserManager
{
public:
    UserManager();
    User *login(const std::string &username, const std::string &password); // User 在 data_structures.h
    void logout();
    User *getCurrentUser() const;
    bool checkAccessPermission(const Inode &inode, PermissionAction action) const;
    bool initializeUsers()
    {
        return true;
    }

private:
    User admin = {0, "admin", "admin", 0};
    User ming = {1, "ming", "ming", 1};
    User lugod = {2, "lugod", "lugod", 2};
    User xman = {3, "xman", "xman", 3};
    User mamba = {4, "mamba", "mamba", 4};
    User neu = {5, "neu", "neu", 5};
    User cse = {6, "cse", "cse", 6};
    User user_2203 = {7, "2203", "2203", 7};
    const std::vector<User> user_database_ = {admin, ming, lugod, xman, mamba, neu, cse, user_2203};
    User *current_user_;
};

#endif // !USER_MANAGER_H