from enum import Enum
import os
import struct
import pickle
from pathlib import Path

# 常量定义
BLOCK_SIZE = 1024  # 每个块 1KB
TOTAL_BLOCKS = 10240  # 总块数，约 10MB
INODE_COUNT = 1024  # 索引节点数
DISK_PATH = "./disk/disk1.img"
HOME_DIR = "/home"

# 用户定义
USERS = [
    {"uid": 0, "username": "admin", "password": "admin", "home_inode": 1},
    {"uid": 1, "username": "ming", "password": "ming", "home_inode": 2},
    {"uid": 2, "username": "lugod", "password": "lugod", "home_inode": 3},
    {"uid": 3, "username": "xman", "password": "xman", "home_inode": 4},
    {"uid": 4, "username": "mamba", "password": "mamba", "home_inode": 5},
    {"uid": 5, "username": "neu", "password": "neu", "home_inode": 6},
    {"uid": 6, "username": "cse", "password": "cse", "home_inode": 7},
    {"uid": 7, "username": "2203", "password": "2203", "home_inode": 8},
]


class OpenMode(Enum):
    READ = "r"
    WRITE = "w"
    APPEND = "a"
    READ_WRITE = "r+"
    WRITE_READ = "w+"


# 超级块结构
class SuperBlock:
    def __init__(self):
        self.block_size = BLOCK_SIZE
        self.total_blocks = TOTAL_BLOCKS
        self.inode_count = INODE_COUNT
        self.free_blocks = list(range(10, TOTAL_BLOCKS))  # 成组链接法，从第10块开始
        self.free_inodes = list(range(10, INODE_COUNT))

    def allocate_block(self):
        if self.free_blocks:
            return self.free_blocks.pop(0)
        raise Exception("No free blocks available")

    def free_block(self, block_id):
        self.free_blocks.append(block_id)

    def allocate_inode(self):
        if self.free_inodes:
            return self.free_inodes.pop(0)
        raise Exception("No free inodes available")

    def free_inode(self, inode_id):
        self.free_inodes.append(inode_id)


# 索引节点（Inode）
class Inode:
    def __init__(self, inode_id, is_dir=False, owner=0, perms=0o777):
        self.id = inode_id
        self.is_dir = is_dir
        self.owner = owner
        self.perms = perms
        self.size = 0
        self.blocks = []  # 数据块列表
        self.links = []  # 硬链接或符号链接


# 文件系统类
class FileSystem:
    def __init__(self):
        self.superblock = None
        self.inodes = {}
        self.data_blocks = {}
        self.current_user = None
        self.current_dir_inode = None
        self.open_files = {}  # 文件描述符表

        # 初始化虚拟磁盘
        Path("./disk").mkdir(exist_ok=True)
        if not os.path.exists(DISK_PATH):
            self.format()
        else:
            self.load_disk()

    def save_disk(self):
        with open(DISK_PATH, "wb") as f:
            pickle.dump((self.superblock, self.inodes, self.data_blocks), f)

    def load_disk(self):
        with open(DISK_PATH, "rb") as f:
            self.superblock, self.inodes, self.data_blocks = pickle.load(f)

    def _get_mode_string(self, perms, is_dir):
        prefix = "d" if is_dir else "-"
        octal = oct(perms)[-3:]  # 获取三位八进制权限字符串
        result = prefix
        for c in octal:
            d = int(c)
            result += "r" if d & 4 else "-"
            result += "w" if d & 2 else "-"
            result += "x" if d & 1 else "-"
        return result

    def format(self):
        self.superblock = SuperBlock()
        self.inodes = {}
        self.data_blocks = {}
        # 创建根目录 /
        root_inode = Inode(0, is_dir=True, owner=0)
        self.inodes[0] = root_inode
        # 创建 /home
        home_inode = Inode(1, is_dir=True, owner=0)
        self.inodes[1] = home_inode
        self.data_blocks[1] = {".": 1, "..": 0}  # home 的目录项
        self.inodes[0].blocks.append(1)
        self.data_blocks[0] = {"home": 1}  # 根目录包含 home
        # 为每个用户创建 home 目录
        for i, user in enumerate(USERS[1:], start=2):
            inode = Inode(i, is_dir=True, owner=user["uid"])
            self.inodes[i] = inode
            self.data_blocks[i] = {".": i, "..": 1}
            self.data_blocks[1][user["username"]] = i
        self.save_disk()

    def login(self, username, password):
        for user in USERS:
            if user["username"] == username and user["password"] == password:
                self.current_user = user
                # self.current_dir_inode = user["home_inode"]
                self.chdir(f"/home/{username}")
                return f"Welcome, {username}!"
        return "Login failed: invalid username or password"

    def logout(self):
        if not self.current_user:
            return "Not logged in"
        self.current_user = None
        self.current_dir_inode = None
        self.save_disk()
        return "Logged out"

    def resolve_path(self, path):
        if not self.current_user:
            raise Exception("Not logged in")
        if path.startswith("~"):
            path = f"/home/{self.current_user['username']}{path[1:]}"
        parts = path.strip("/").split("/")
        inode_id = 0 if path.startswith("/") else self.current_dir_inode
        for part in parts:
            if not part:
                continue
            if part == ".":
                continue
            if part == "..":
                inode_id = self.data_blocks[inode_id][".."]
            else:
                inode_id = self.data_blocks[inode_id].get(part)
                if inode_id is None:
                    raise Exception(f"Path not found: {path}")
        return inode_id

    def mkdir(self, dir_name):
        parent_inode = self.resolve_path(os.path.dirname(dir_name))
        name = os.path.basename(dir_name)
        new_inode_id = self.superblock.allocate_inode()
        new_inode = Inode(new_inode_id, is_dir=True, owner=self.current_user["uid"])
        block_id = self.superblock.allocate_block()
        self.inodes[new_inode_id] = new_inode
        self.data_blocks[block_id] = {".": new_inode_id, "..": parent_inode}
        self.data_blocks[parent_inode][name] = new_inode_id
        self.inodes[parent_inode].blocks.append(block_id)
        self.save_disk()
        return f"Directory {dir_name} created"

    def chdir(self, path):
        inode_id = self.resolve_path(path)
        if not self.inodes[inode_id].is_dir:
            return f"{path} is not a directory"
        self.current_dir_inode = inode_id
        return f"Changed to {path}"

    def _perms_to_str(self, perms):
        perm_chars = [
            "r" if perms & 0o400 else "-",
            "w" if perms & 0o200 else "-",
            "x" if perms & 0o100 else "-",
            "r" if perms & 0o040 else "-",
            "w" if perms & 0o020 else "-",
            "x" if perms & 0o010 else "-",
            "r" if perms & 0o004 else "-",
            "w" if perms & 0o002 else "-",
            "x" if perms & 0o001 else "-",
        ]

        return "".join(perm_chars)

    def dir(self):
        current_inode = self.current_dir_inode
        items = self.data_blocks.get(current_inode, {})
        result = []

        # 表头
        headers = ["Permissions", "Type", "Owner", "Size", "Name"]
        result.append(
            f"{headers[0]:<11} {headers[1]:>5} {headers[2]:<10} {headers[3]:>6} {headers[4]}"
        )
        # result.append("---------------------------------------------------")

        for name, inode_id in items.items():
            if name in [".", ".."]:
                continue

            inode = self.inodes.get(inode_id)
            if not inode:
                continue

            # 权限字符串
            perm_str = self._perms_to_str(inode.perms)
            if inode.is_dir:
                perm_str = "d" + perm_str[1:]
            else:
                perm_str = "-" + perm_str[1:]

            # 硬链接数
            link_count = "d" if inode.is_dir else "f"

            # 所有者用户名
            owner_uid = inode.owner
            owner = next(
                (u["username"] for u in USERS if u["uid"] == owner_uid), str(owner_uid)
            )

            # 文件大小
            size = inode.size  # if not inode.is_dir else 0

            # 格式化输出
            line = f"{perm_str:<12} {link_count:>4} {owner:<10} {size:>6} {name}"
            result.append(line)

        return "\n".join(result)

    def create(self, file_name):
        parent_inode = self.resolve_path(os.path.dirname(file_name))
        name = os.path.basename(file_name)
        new_inode_id = self.superblock.allocate_inode()
        new_inode = Inode(new_inode_id, is_dir=False, owner=self.current_user["uid"])
        self.inodes[new_inode_id] = new_inode
        self.data_blocks[parent_inode][name] = new_inode_id
        self.save_disk()
        return f"File {file_name} created"

    def open(self, file_name):
        inode_id = self.resolve_path(file_name)
        if self.inodes[inode_id].is_dir:
            return f"{file_name} is a directory"
        fd = len(self.open_files)
        self.open_files[fd] = inode_id
        return f"File opened with fd {fd}"

    def write(self, fd, data):
        if fd not in self.open_files:
            return "Invalid file descriptor"
        inode_id = self.open_files[fd]
        inode = self.inodes[inode_id]
        block_id = self.superblock.allocate_block()
        self.data_blocks[block_id] = data.encode()
        inode.blocks.append(block_id)
        inode.size += len(data)
        self.save_disk()
        return "Write successful"

    def read(self, fd):
        if fd not in self.open_files:
            return "Invalid file descriptor"
        inode_id = self.open_files[fd]
        inode = self.inodes[inode_id]
        content = ""
        for block_id in inode.blocks:
            content += self.data_blocks[block_id].decode()
        return content

    def close(self, fd):
        if fd not in self.open_files:
            return "Invalid file descriptor"
        del self.open_files[fd]
        return "File closed"

    def delete(self, file_name):
        parent_inode = self.resolve_path(os.path.dirname(file_name))
        name = os.path.basename(file_name)
        inode_id = self.data_blocks[parent_inode].pop(name)
        inode = self.inodes[inode_id]
        for block_id in inode.blocks:
            self.superblock.free_block(block_id)
        self.superblock.free_inode(inode_id)
        del self.inodes[inode_id]
        self.save_disk()
        return f"{file_name} deleted"

    def cp(self, src, dst):
        src_inode = self.resolve_path(src)
        dst_parent = self.resolve_path(os.path.dirname(dst))
        name = os.path.basename(dst)
        new_inode_id = self.superblock.allocate_inode()
        new_inode = Inode(new_inode_id, is_dir=False, owner=self.current_user["uid"])
        new_inode.blocks = self.inodes[src_inode].blocks.copy()
        new_inode.size = self.inodes[src_inode].size
        self.inodes[new_inode_id] = new_inode
        self.data_blocks[dst_parent][name] = new_inode_id
        self.save_disk()
        return f"Copied {src} to {dst}"

    def mv(self, src, dst):
        self.cp(src, dst)
        self.delete(src)
        return f"Moved {src} to {dst}"

    def chmod(self, file_name, perms):
        inode_id = self.resolve_path(file_name)
        self.inodes[inode_id].perms = int(perms, 8)
        self.save_disk()
        return f"Permissions of {file_name} changed to {perms}"

    def chown(self, file_name, username):
        inode_id = self.resolve_path(file_name)
        for user in USERS:
            if user["username"] == username:
                self.inodes[inode_id].owner = user["uid"]
                self.save_disk()
                return f"Owner of {file_name} changed to {username}"
        return "User not found"

    def ln(self, src, dst):
        src_inode = self.resolve_path(src)
        dst_parent = self.resolve_path(os.path.dirname(dst))
        name = os.path.basename(dst)
        self.data_blocks[dst_parent][name] = src_inode
        self.inodes[src_inode].links.append(dst)
        self.save_disk()
        return f"Link {dst} created for {src}"

    def find(self, name):
        def search(inode_id, path):
            if name in self.data_blocks[inode_id]:
                return f"{path}/{name}"
            for item, child_inode in self.data_blocks[inode_id].items():
                if item in [".", ".."]:
                    continue
                if self.inodes[child_inode].is_dir:
                    result = search(child_inode, f"{path}/{item}")
                    if result:
                        return result
            return None

        return search(0, "") or f"{name} not found"

    def help(self):
        print("Available commands:")
        help_texts = {
            "login <username> <password>": "Log in as a user.",
            "logout": "Log out the current user.",
            "format": "Initialize and format the disk (WARNING: erases all data).",
            "mkdir <dirname>": "Create a new directory in the current location.",
            "cd <path>": "Change current directory. Supports '.', '..', '~', and simple absolute/relative paths.",
            "dir/ls": "List contents of the current directory.",
            "create <filename>": "Create a new empty file in the current location.",
            "open <filename> <mode>": "Open a file. Mode can be 'r' (read) or 'w' (write).",
            "write <fd> <data>": "Write data string to an open file descriptor (fd).",
            "read <fd>": "Read data from an open file descriptor (fd).",
            "close <fd>": "Close an open file descriptor (fd).",
            "delete <filename>": "Delete a file from the current directory.",
            "rm <path> [-r]": "Remove a file or directory. Use -r for recursive (currently basic).",
            "cp <src> <dest>": "Copy file or directory (Not fully implemented).",
            "mv <src> <dest>": "Move/rename file or directory (Not fully implemented).",
            "chmod <path> <octal_perms>": "Change file permissions (Not fully implemented).",
            "chown <path> <uid>": "Change file owner (Not fully implemented).",
            "ln <src> <dest>": "Create a hard link (Not fully implemented).",
            "find <path> <name>": "Find files or directories (Not fully implemented).",
            "help": "Show this help message.",
            "exit": "Exit the shell.",
        }
        txt_to_ret = ""
        for cmd_syntax, desc in help_texts.items():
            txt_to_ret += f"{cmd_syntax.ljust(30)} - {desc}\n"
        return txt_to_ret.strip()

    def get_prompt(self):
        if not self.current_user:
            return "myFS> "
        path = ""
        inode_id = self.current_dir_inode
        while inode_id != 0:
            for name, id in self.data_blocks[self.data_blocks[inode_id][".."]].items():
                if id == inode_id and name not in [".", ".."]:
                    path = f"/{name}{path}"
                    break
            inode_id = self.data_blocks[inode_id][".."]
        path = "/" if not path else path
        return f"{self.current_user['username']}@myFS:{path} $ "


# 主程序
def main():
    fs = FileSystem()
    print("欢迎使用模拟文件系统！输入 'help' 查看命令。")
    while True:
        cmd = input(fs.get_prompt()).strip()
        inQuotes = False
        tokens = []
        current_token = ""
        for char in cmd:
            if char == '"' or char == "'":
                inQuotes = not inQuotes
                if not inQuotes and current_token:
                    tokens.append(current_token)
                    current_token = ""
            elif char.isspace() and not inQuotes:
                if current_token:
                    tokens.append(current_token)
                    current_token = ""
            else:
                current_token += char
        if current_token:
            tokens.append(current_token)
        cmd = tokens
        if not cmd:
            continue
        command = cmd[0]
        args = cmd[1:]
        try:
            if command == "login" and len(args) == 2:
                print(fs.login(args[0], args[1]))
            elif command == "logout":
                print(fs.logout())
            elif command == "help":
                print(fs.help())
            elif command == "exit":
                break
            elif not fs.current_user:
                print("请先登录")
            elif command == "mkdir" and len(args) == 1:
                print(fs.mkdir(args[0]))
            elif command in ["chdir", "cd"] and len(args) == 1:
                print(fs.chdir(args[0]))
            elif command in ["dir", "ls"]:
                print(fs.dir())
            elif command == "create" and len(args) == 1:
                print(fs.create(args[0]))
            elif command == "open" and len(args) == 1:
                print(fs.open(args[0]))
            elif command == "write" and len(args) == 2:
                print(fs.write(int(args[0]), args[1]))
            elif command == "read" and len(args) == 1:
                print(fs.read(int(args[0])))
            elif command == "close" and len(args) == 1:
                print(fs.close(int(args[0])))
            elif command in ["delete", "rm"] and len(args) == 1:
                print(fs.delete(args[0]))
            elif command == "format":
                fs.format()
                print("文件系统已格式化")
            elif command == "cp" and len(args) == 2:
                print(fs.cp(args[0], args[1]))
            elif command == "mv" and len(args) == 2:
                print(fs.mv(args[0], args[1]))
            elif command == "chmod" and len(args) == 2:
                print(fs.chmod(args[0], args[1]))
            elif command == "chown" and len(args) == 2:
                print(fs.chown(args[0], args[1]))
            elif command == "ln" and len(args) == 2:
                print(fs.ln(args[0], args[1]))
            elif command == "find" and len(args) == 1:
                print(fs.find(args[0]))
            else:
                print("未知命令或参数错误，输入 'help' 查看帮助")
        except Exception as e:
            print(f"错误: {e}")


if __name__ == "__main__":
    main()
