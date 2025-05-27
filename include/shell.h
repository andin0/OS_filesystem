#ifndef SHELL_H
#define SHELL_H
#include "all_includes.h"

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
};

#endif // SHELL_H