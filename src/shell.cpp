#include "all_includes.h" // 确保包含了 shell.h 和 filesystem.h

Shell::Shell(FileSystem *fs) : fs_(fs) {}

void Shell::displayPrompt()
{
    // 调用 FileSystem 的方法获取当前路径和用户信息来构建提示符
    std::cout << fs_->getCurrentPathPrompt() << "$ ";
}

// 将用户输入的字符串分割成命令和参数
std::vector<std::string> Shell::parseCommand(const std::string &input)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(input);
    while (std::getline(tokenStream, token, ' '))
    {
        if (!token.empty())
        {
            tokens.push_back(token);
        }
    }
    return tokens;
}

void Shell::run()
{
    std::string input;
    std::cout << "Welcome to MyFileSystem!" << std::endl; // 欢迎信息
    // 初始尝试自动登录或提示登录
    // if (!fs_->isUserLoggedIn()) { // 假设 FileSystem 有此方法
    //     std::cout << "Please login to continue." << std::endl;
    //     // 可以选择在这里直接调用 handleLogin 或者在 executeCommand 中处理
    // }

    while (true)
    {
        displayPrompt();
        std::getline(std::cin, input);
        std::vector<std::string> tokens = parseCommand(input);

        if (tokens.empty())
        {
            continue;
        }

        std::string command = tokens[0];
        if (command == "exit")
        {
            std::cout << "Exiting MyFileSystem. Goodbye!" << std::endl;
            break;
        }
        else
        {
            executeCommand(tokens);
        }
    }
}

void Shell::executeCommand(const std::vector<std::string> &tokens)
{
    if (tokens.empty())
    {
        return;
    }
    const std::string &command = tokens[0];

    // 根据命令分发
    if (command == "login")
    {
        handleLogin(tokens);
    }
    else if (command == "logout")
    {
        handleLogout(tokens);
    }
    else if (command == "mkdir")
    {
        handleMkdir(tokens);
    }
    else if (command == "cd" || command == "chdir")
    {
        handleCd(tokens);
    }
    else if (command == "ls" || command == "dir")
    { //
        handleLs(tokens);
    }
    else if (command == "create")
    { //
        handleCreate(tokens);
    }
    else if (command == "rm")
    { //
        handleRm(tokens);
    }
    else if (command == "open")
    {
        handleOpen(tokens);
    }
    else if (command == "close")
    {
        handleClose(tokens);
    }
    else if (command == "write")
    {
        handleWrite(tokens);
    }
    else if (command == "read")
    {
        handleRead(tokens);
    }

    else if (command == "help")
    {                       //
        handleHelp(tokens); //
    }
    else
    {
        std::cerr << "Unknown command: " << command << std::endl;
    }
}

void Shell::handleLogin(const std::vector<std::string> &args)
{ //
    if (args.size() < 3)
    {
        std::cerr << "Usage: login <username> <password>" << std::endl;
        return;
    }
    if (fs_->loginUser(args[1], args[2]))
    { //
        std::cout << "User " << args[1] << " logged in successfully." << std::endl;
    }
    else
    {
        std::cerr << "Login failed." << std::endl;
    }
}

void Shell::handleCd(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        std::cerr << "Usage: cd <path>" << std::endl;
    }
    else
    {
        if (!fs_->chdir(args[1]))
        {
            std::cerr << "cd: Failed to change directory to " << args[1] << std::endl;
        }
    }
}

void Shell::handleLs(const std::vector<std::string> &args)
{
    std::string path = (args.size() > 1) ? args[1] : "."; // 默认为当前目录
    std::string result = fs_->dir(path);                  //
    std::cout << result;                                  // dir方法应返回格式化好的字符串
}

void Shell::handleCreate(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        std::cerr << "Usage: create <path>" << std::endl;
    }
    else
    {
        if (!fs_->create(args[1]))
        { //
            std::cerr << "create: Failed to create file " << args[1] << std::endl;
        }
    }
}

void Shell::handleRm(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        std::cerr << "Usage: rm [-r] [-f] <path>" << std::endl;
    }
    else
    {
        // 解析 -r 和 -f 参数
        bool recursive = false;
        bool force = false;
        std::string path;
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (args[i] == "-r")
                recursive = true;
            else if (args[i] == "-f")
                force = true;
            else
                path = args[i];
        }
        if (path.empty())
        {
            std::cerr << "Usage: rm [-r] [-f] <path>" << std::endl;
        }
        else
        {
            if (!fs_->rm(path, recursive, force))
            { //
                std::cerr << "rm: Failed to remove " << path << std::endl;
            }
        }
    }
}

void Shell::handleLogout(const std::vector<std::string> &args)
{                      //
    fs_->logoutUser(); //
    std::cout << "User logged out." << std::endl;
}

void Shell::handleMkdir(const std::vector<std::string> &args)
{ //
    if (args.size() < 2)
    {
        std::cerr << "Usage: mkdir <directory_path>" << std::endl;
        return;
    }
    if (fs_->mkdir(args[1]))
    { //
        std::cout << "Directory '" << args[1] << "' created successfully." << std::endl;
    }
    else
    {
        std::cerr << "Failed to create directory '" << args[1] << "'." << std::endl;
    }
}

void Shell::handleOpen(const std::vector<std::string> &args)
{
    if (args.size() < 3)
    {
        std::cerr << "Usage: open <path> <mode>" << std::endl;
        std::cerr << "Modes: r (read), w (write), rw (read-write), a (append)" << std::endl;
        return;
    }
    const std::string &path = args[1];
    const std::string &mode_str = args[2];
    OpenMode mode;

    if (mode_str == "r")
        mode = OpenMode::MODE_READ;
    else if (mode_str == "w")
        mode = OpenMode::MODE_WRITE;
    else if (mode_str == "rw")
        mode = OpenMode::MODE_READ_WRITE;
    else if (mode_str == "a")
        mode = OpenMode::MODE_APPEND;
    else
    {
        std::cerr << "Invalid open mode: " << mode_str << std::endl;
        std::cerr << "Valid modes are: r, w, rw, a" << std::endl;
        return;
    }

    int fd = fs_->open(path, mode);
    if (fd != INVALID_FD)
    {
        std::cout << "File '" << path << "' opened successfully. File descriptor: " << fd << std::endl;
    }
    else
    {
        std::cerr << "Failed to open file '" << path << "'." << std::endl;
    }
}

void Shell::handleClose(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        std::cerr << "Usage: close <fd>" << std::endl;
        return;
    }
    try
    {
        int fd = std::stoi(args[1]);
        if (fs_->close(fd))
        {
            std::cout << "File descriptor " << fd << " closed successfully." << std::endl;
        }
        else
        {
            std::cerr << "Failed to close file descriptor " << fd << "." << std::endl;
        }
    }
    catch (const std::invalid_argument &ia)
    {
        std::cerr << "Invalid file descriptor format: " << args[1] << ". Must be an integer." << std::endl;
    }
    catch (const std::out_of_range &oor)
    {
        std::cerr << "File descriptor " << args[1] << " is out of range." << std::endl;
    }
}

void Shell::handleRead(const std::vector<std::string> &args)
{
    if (args.size() < 3)
    {
        std::cerr << "Usage: read <fd> <length>" << std::endl;
        return;
    }
    try
    {
        int fd = std::stoi(args[1]);
        int length = std::stoi(args[2]);

        if (length <= 0)
        {
            std::cerr << "Read length must be positive." << std::endl;
            return;
        }
        if (length > 1024 * 10)
        { // 一个任意的限制，防止过大的缓冲区
            std::cerr << "Read length is too large (max 10240)." << std::endl;
            return;
        }

        char *buffer = new char[length + 1]; // +1 for null terminator for string display
        int bytes_read = fs_->read(fd, buffer, length);

        if (bytes_read > 0)
        {
            buffer[bytes_read] = '\0'; // Null-terminate the buffer to print as string
            std::cout << "Read " << bytes_read << " bytes: " << std::endl;
            // 直接打印可能遇到非文本内容，更好的方式是十六进制打印或提示
            // 对于简单文本文件，直接打印是OK的
            std::cout.write(buffer, bytes_read);
            std::cout << std::endl;
            if (bytes_read < length)
            {
                std::cout << "(End of file reached or read limit hit)" << std::endl;
            }
        }
        else if (bytes_read == 0)
        {
            std::cout << "Read 0 bytes (end of file or empty read)." << std::endl;
        }
        else
        {
            std::cerr << "Failed to read from file descriptor " << fd << "." << std::endl;
        }
        delete[] buffer;
    }
    catch (const std::invalid_argument &ia)
    {
        std::cerr << "Invalid argument format for fd or length. Must be integers." << std::endl;
    }
    catch (const std::out_of_range &oor)
    {
        std::cerr << "Argument for fd or length is out of range." << std::endl;
    }
}

void Shell::handleWrite(const std::vector<std::string> &args)
{
    if (args.size() < 3)
    {
        std::cerr << "Usage: write <fd> \"<data_to_write>\"" << std::endl;
        return;
    }
    try
    {
        int fd = std::stoi(args[1]);
        const std::string &data = args[2]; // parseCommand 现在应该能处理带引号的字符串

        int bytes_written = fs_->write(fd, data.c_str(), data.length());
        if (bytes_written >= 0)
        { // write 可能返回0表示未写入任何内容但非错误
            std::cout << "Successfully wrote " << bytes_written << " bytes to file descriptor " << fd << "." << std::endl;
        }
        else
        {
            std::cerr << "Failed to write to file descriptor " << fd << "." << std::endl;
        }
    }
    catch (const std::invalid_argument &ia)
    {
        std::cerr << "Invalid file descriptor format: " << args[1] << ". Must be an integer." << std::endl;
    }
    catch (const std::out_of_range &oor)
    {
        std::cerr << "File descriptor " << args[1] << " is out of range." << std::endl;
    }
}

void Shell::handleHelp(const std::vector<std::string> &args)
{ //
    std::cout << "Available commands:" << std::endl;
    std::cout << "  login <username> <password>   - Log in as a user" << std::endl;
    std::cout << "  logout                        - Log out current user" << std::endl;
    std::cout << "  mkdir <directory_path>        - Create a new directory" << std::endl;
    std::cout << "  cd <path> /chdir <path>       - Change current directory" << std::endl;
    std::cout << "  ls [path] / dir [path]        - List directory contents" << std::endl;                     //
    std::cout << "  create <path>                 - Create a new empty file" << std::endl;                     //
    std::cout << "  rm [-r] [-f] <path>           - Remove a file or directory" << std::endl;                  //
    std::cout << "  open <path> <mode>            - Open a file (modes: r, w, rw, a)" << std::endl;            //
    std::cout << "  close <fd>                    - Close an open file descriptor" << std::endl;               //
    std::cout << "  read <fd> <length>            - Read from an open file" << std::endl;                      //
    std::cout << "  write <fd> <data>             - Write to an open file" << std::endl;                       //
    std::cout << "  cp [-r] <source> <destination> - Copy a file or directory" << std::endl;                   //
    std::cout << "  mv <source> <destination>     - Move/rename a file or directory" << std::endl;             //
    std::cout << "  ln <target> <link_name>       - Create a hard link" << std::endl;                          //
    std::cout << "  chmod <path> <mode>           - Change file permissions (e.g., 755)" << std::endl;         //
    std::cout << "  chown <path> <username>       - Change file owner" << std::endl;                           //
    std::cout << "  find [start_path] <filename>  - Find a file" << std::endl;                                 //
    std::cout << "  format                        - Format the disk (CAUTION: deletes all data)" << std::endl; //
    std::cout << "  help                          - Display this help message" << std::endl;
    std::cout << "  exit                          - Exit the shell" << std::endl;
}
