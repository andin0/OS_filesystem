#ifndef SHELL_H
#define SHELL_H
#include "filesystem.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include "data_structures.h" // 包含 ProcessOpenFileEntry 和 SystemOpenFileEntry

class Shell
{
public:
    Shell(FileSystem *fs);
    void run();

private:
    FileSystem *fs_;
    void displayPrompt();
    std::vector<std::string> parseCommand(const std::string &input);
    void executeCommand(const std::vector<std::string> &tokens);
    // Specific command handlers would parse flags like -r, -f and pass them to FileSystem methods
    // e.g., void handleRm(const std::vector<std::string>& args);
    // e.g., void handleCp(const std::vector<std::string>& args);
    void handleLogin(const std::vector<std::string> &args);
    void handleLogout(const std::vector<std::string> &args);
    void handleMkdir(const std::vector<std::string> &args);
    void handleHelp(const std::vector<std::string> &args);
    void handleOpen(const std::vector<std::string> &args);
    void handleClose(const std::vector<std::string> &args);
    void handleWrite(const std::vector<std::string> &args);
    void handleRead(const std::vector<std::string> &args);
    void handleCd(const std::vector<std::string> &args);
    void handleLs(const std::vector<std::string> &args);
    void handleCreate(const std::vector<std::string> &args);
    void handleRm(const std::vector<std::string> &args);
};

#endif // SHELL_H