from enum import Enum
import os
import struct
import pickle
from pathlib import Path

# 常量定义
BLOCK_SIZE = 1024  # 每个块 1KB
TOTAL_BLOCKS = 10240  # 总块数，约 10MB
INODE_COUNT = 1024  # 索引节点数
DISK_PATH = "./disk/disk.img"
HOME_DIR = "/home"

# 用户定义
USERS = [
    {"uid": 0, "username": "admin", "password": "admin"},
    {"uid": 1, "username": "ming", "password": "ming"},
    {"uid": 2, "username": "lugod", "password": "lugod"},
    {"uid": 3, "username": "xman", "password": "xman"},
    {"uid": 4, "username": "mamba", "password": "mamba"},
    {"uid": 5, "username": "neu", "password": "neu"},
    {"uid": 6, "username": "cse", "password": "cse"},
    {"uid": 7, "username": "2203", "password": "2203"},
]


class OpenMode(Enum):
    READ = "r"
    WRITE = "w"
    READ_WRITE = "rw"


def convert_fd(s: str):
    if s.isdigit():
        return int(s)
    else:
        return s


def path_join(path: str, *args: str):
    res = path
    for p in args:
        res += "/" + p
    return res


# 超级块结构
class SuperBlock:
    def __init__(self):
        self.block_size = BLOCK_SIZE
        self.total_blocks = TOTAL_BLOCKS
        self.inode_count = INODE_COUNT
        self.free_blocks = list(range(10, TOTAL_BLOCKS))
        self.free_inodes = list(range(10, INODE_COUNT))

    def allocate_block(self):
        if self.free_blocks:
            return self.free_blocks.pop(0)
        raise Exception("No free blocks available")

    def free_block(self, block_id):
        self.free_blocks.insert(0, block_id)

    def allocate_inode(self):
        if self.free_inodes:
            return self.free_inodes.pop(0)
        raise Exception("No free inodes available")

    def free_inode(self, inode_id):
        self.free_inodes.insert(0, inode_id)


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
        self.link_count = (
            1 if not is_dir else 2
        )  # Files start with 1 link, directories with 2 (self and parent)
        self.lock = False


class OpenFileEntry:
    def __init__(self, inode_id: int, mode: OpenMode):
        self.id = inode_id
        self.openMode = mode


# 文件系统类
class FileSystem:
    def __init__(self):
        self.superblock = None
        self.inodes = {}
        self.data_blocks = {}
        self.current_user = None
        self.current_dir_inode = None
        self.open_files = {}  # 文件描述符表
        self.isSudo = False

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

    def has_perms(self, file_inode: Inode, required_perm_char: str):
        if not self.current_user:
            return False
        if self.current_user["uid"] == 0 or self.isSudo:
            return True
        perms = file_inode.perms
        owner_uid = file_inode.owner
        current_uid = self.current_user["uid"]
        effective_perms_octet = 0
        if current_uid == owner_uid:
            effective_perms_octet = (perms >> 6) & 0o7
        else:
            effective_perms_octet = perms & 0o7
        if required_perm_char == "r":
            return bool(effective_perms_octet & 0o4)
        elif required_perm_char == "w":
            return bool(effective_perms_octet & 0o2)
        elif required_perm_char == "x":
            return bool(effective_perms_octet & 0o1)
        else:
            return False

    def format(self):
        self.superblock = SuperBlock()
        self.inodes = {}
        self.data_blocks = {}

        # Create root directory inode
        root_inode = Inode(0, is_dir=True, owner=0)
        self.inodes[0] = root_inode

        # Assign block 0 to root directory and set its entries
        self.data_blocks[0] = {
            ".": 0,
            "..": 0,
            "home": 1,
        }  # Root's entries, including "home"
        self.inodes[0].blocks.append(0)  # Root inode points to block 0

        # Create /home directory inode
        home_inode = Inode(1, is_dir=True, owner=0)
        self.inodes[1] = home_inode

        # Assign block 1 to /home directory and set its entries
        self.data_blocks[1] = {".": 1, "..": 0}  # /home's entries
        self.inodes[1].blocks.append(1)  # /home inode points to block 1

        self.save_disk()
        curr_user = None
        if self.current_user:
            curr_user = self.current_user["username"]
        self.current_user = USERS[0]
        self.chdir("/home")
        for i, user in enumerate(USERS):
            self.current_user = USERS[i]
            self.mkdir(user["username"])
        self.logout()
        if curr_user:
            self.login(curr_user, curr_user)

    def login(self, username, password):
        for user in USERS:
            if user["username"] == username and user["password"] == password:
                self.current_user = user
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
                if inode_id == 0:
                    continue  # 根目录的".."仍然是根目录
                dir_block_id = self.inodes[inode_id].blocks[0]
                parent_inode_id = self.data_blocks[dir_block_id][".."]
                inode_id = parent_inode_id
            else:
                if self.inodes[inode_id].is_dir:
                    dir_block_id = self.inodes[inode_id].blocks[0]
                    inode_id = self.data_blocks[dir_block_id].get(part)
                    if inode_id is None:
                        raise Exception(f"Path not found: {path}")
                else:
                    raise Exception(f"{path} is not a directory")
        return inode_id

    def mkdir(self, dir_name):
        parent_inode = self.resolve_path(os.path.dirname(dir_name))
        name = os.path.basename(dir_name)
        parent_dir_block_id = self.inodes[parent_inode].blocks[0]
        if name in self.data_blocks.get(parent_dir_block_id, {}):
            return f"Directory {dir_name} already exists"
        if not self.has_perms(self.inodes[parent_inode], "w"):
            return "Permission denied"
        new_inode_id = self.superblock.allocate_inode()
        new_inode = Inode(
            new_inode_id, is_dir=True, owner=self.current_user["uid"], perms=0o755
        )
        block_id = self.superblock.allocate_block()
        self.inodes[new_inode_id] = new_inode
        self.data_blocks[block_id] = {".": new_inode_id, "..": parent_inode}
        new_inode.blocks.append(block_id)
        self.data_blocks.setdefault(parent_dir_block_id, {})[name] = new_inode_id
        self.save_disk()
        return f"Directory {dir_name} created"

    def chdir(self, path):
        if not self.current_user:
            return "Not logged in"
        inode_id = self.resolve_path(path)
        if inode_id not in self.inodes:
            return f"{path} does not exist"
        if not self.inodes[inode_id].is_dir:
            return f"{path} is not a directory"
        if not self.has_perms(self.inodes[inode_id], "x"):
            return "Permission denied"
        self.current_dir_inode = inode_id
        self.save_disk()
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

    def dir(self, path=""):
        if path == "" or path == ".":
            current_inode = self.current_dir_inode
        else:
            current_inode = self.resolve_path(path)
        if not self.has_perms(self.inodes[current_inode], "r"):
            return "Permission denied"
        if self.inodes[current_inode].is_dir:
            dir_block_id = self.inodes[current_inode].blocks[0]
            items = self.data_blocks.get(dir_block_id, {})
        else:
            return f"{path} is not a directory"
        items = self.data_blocks.get(current_inode, {})
        result = []
        headers = ["Permissions", "Type", "Owner", "Size", "Name"]
        result.append(
            f"{headers[0]:<11} {headers[1]:>5} {headers[2]:<10} {headers[3]:>6} {headers[4]}"
        )
        for name, inode_id in items.items():
            if name in [".", ".."]:
                continue
            inode = self.inodes.get(inode_id)
            if not inode:
                continue
            perm_str = self._perms_to_str(inode.perms)
            if inode.is_dir:
                perm_str = "d" + perm_str
            else:
                perm_str = "-" + perm_str
            file_type = "dir" if inode.is_dir else "file"
            owner_uid = inode.owner
            owner = next(
                (u["username"] for u in USERS if u["uid"] == owner_uid),
                str(owner_uid),
            )
            size = inode.size if not inode.is_dir else ""
            line = f"{perm_str:<12} {file_type:>4} {owner:<10} {size:>6} {name}"
            result.append(line)
        return "\n".join(result)

    def create(self, file_name):
        parent_inode = self.resolve_path(os.path.dirname(file_name))
        name = os.path.basename(file_name)
        if not self.has_perms(self.inodes[parent_inode], "w"):  # 检查写权限
            return "Permission denied"
        parent_block_id = self.inodes[parent_inode].blocks[0]  # 获取父目录的数据块 ID
        if name in self.data_blocks.get(parent_block_id, {}):  # 检查文件是否已存在
            return f"File {file_name} already exists"
        new_inode_id = self.superblock.allocate_inode()
        new_inode = Inode(
            new_inode_id, is_dir=False, owner=self.current_user["uid"], perms=0o644
        )
        self.inodes[new_inode_id] = new_inode
        self.data_blocks[parent_block_id][name] = new_inode_id  # 添加到正确的数据块
        self.save_disk()
        return f"File {file_name} created"

    def open(self, file_name, openmode="r"):
        try:
            mode = OpenMode(openmode)
        except ValueError:
            return f"{openmode} is not a valid open type."
        inode_id = self.resolve_path(file_name)
        if self.inodes[inode_id].is_dir:
            return f"{file_name} is a directory"
        inode = self.inodes[inode_id]
        if "r" in openmode and not self.has_perms(inode, "r"):
            return "Permission denied"
        if "w" in openmode and not self.has_perms(inode, "w"):
            return "Permission denied"
        fd = len(self.open_files)
        self.inodes[inode_id].lock = True
        self.open_files[fd] = OpenFileEntry(inode_id, mode)
        return f"File opened with fd {fd}"

    def write(self, fd, data):
        if type(fd) is not int or fd not in self.open_files:
            return "Invalid file descriptor"
        entry = self.open_files[fd]
        inode_id = entry.id
        if entry.openMode not in {OpenMode.WRITE, OpenMode.READ_WRITE}:
            return "Error. File opened in readonly mode"
        inode = self.inodes[inode_id]
        block_id = self.superblock.allocate_block()
        self.data_blocks[block_id] = data.encode()
        inode.blocks.append(block_id)
        inode.size += len(data)
        self.save_disk()
        return "Write successful"

    def read(self, fd):
        if type(fd) is not int or fd not in self.open_files:
            return "Invalid file descriptor"
        entry = self.open_files[fd]
        inode_id = entry.id
        if entry.openMode not in {OpenMode.READ, OpenMode.READ_WRITE}:
            return "Error. File opened in writeonly mode"
        inode = self.inodes[inode_id]
        content = ""
        for block_id in inode.blocks:
            content += self.data_blocks[block_id].decode()
        return content

    def close(self, fd):
        if type(fd) is not int or fd not in self.open_files:
            return "Invalid file descriptor"
        inode_id = self.open_files[fd].id
        self.inodes[inode_id].lock = False
        del self.open_files[fd]

        return "File closed"

    def delete(self, file_name, recursive=False):
        parent_inode = self.resolve_path(os.path.dirname(file_name))
        name = os.path.basename(file_name)
        if not self.has_perms(self.inodes[parent_inode], "w"):
            return "Permission denied"
        parent_dir_block_id = self.inodes[parent_inode].blocks[0]
        if name not in self.data_blocks.get(parent_dir_block_id, {}):
            return f"{file_name} not found"
        inode_id = self.data_blocks[parent_dir_block_id][name]
        inode = self.inodes[inode_id]
        if inode.lock == True:
            return f"File {file_name} is in use. Close it and try again"

        if inode.is_dir and not recursive:
            dir_contents = self.data_blocks.get(inode_id, {})
            if any(k not in [".", ".."] for k in dir_contents):
                return (
                    f"Directory {file_name} is not empty; use -r to delete recursively"
                )

        if inode.is_dir and recursive:
            # Recursively delete directory contents
            dir_contents = self.data_blocks.get(inode_id, {}).copy()
            for item, child_inode_id in dir_contents.items():
                if item in [".", ".."]:
                    continue
                child_path = f"{file_name}/{item}"
                self.delete(child_path, recursive=True)

        # Decrease link count
        inode.link_count -= 1
        if inode.link_count == 0:
            # Free all data blocks
            for block_id in inode.blocks:
                self.superblock.free_block(block_id)
            # Free the inode
            self.superblock.free_inode(inode_id)
            del self.inodes[inode_id]
            # Remove directory data block if it exists
            if inode.is_dir and inode_id in self.data_blocks:
                del self.data_blocks[inode_id]

        # Remove from parent directory
        del self.data_blocks[parent_dir_block_id][name]
        self.save_disk()
        return f"{file_name} deleted"

    def cp(self, src, dst, recursive=False):
        try:
            src_inode_id = self.resolve_path(src)
            src_inode = self.inodes[src_inode_id]

            if src_inode.is_dir and not recursive:
                return f"cp: omitting directory '{src}' (use -r to copy directories)"

            dst_parent_path = os.path.dirname(dst)
            dst_name = os.path.basename(dst)

            if dst_parent_path:
                dst_parent_id = self.resolve_path(dst_parent_path)
            else:
                dst_parent_id = self.current_dir_inode

            dst_parent_inode = self.inodes[dst_parent_id]

            if not self.has_perms(src_inode, "r"):
                return f"Permission denied to read source '{src}'."
            if not self.has_perms(dst_parent_inode, "w"):
                return f"Permission denied to write in destination directory '{dst}'."

            # Check if destination exists
            try:
                dst_inode_id = self.resolve_path(dst)
                dst_inode = self.inodes[dst_inode_id]
                if dst_inode.is_dir:
                    new_dst_path = os.path.join(dst, os.path.basename(src))
                    return self.cp(src, new_dst_path, recursive)
                else:
                    self.delete(dst)
            except Exception:
                pass

            if src_inode.is_dir:
                # Create new directory at destination
                new_inode_id = self.superblock.allocate_inode()
                new_inode = Inode(
                    new_inode_id,
                    is_dir=True,
                    owner=self.current_user["uid"],
                    perms=src_inode.perms,
                )
                self.inodes[new_inode_id] = new_inode
                block_id = self.superblock.allocate_block()
                self.data_blocks[block_id] = {".": new_inode_id, "..": dst_parent_id}
                new_inode.blocks.append(
                    block_id
                )  # This line is correct as-is in some implementations
                self.data_blocks[dst_parent_id][dst_name] = new_inode_id

                # Recursively copy contents
                src_block_id = src_inode.blocks[0]  # Fix this line
                for item, child_inode_id in self.data_blocks[src_block_id].items():
                    if item in [".", ".."]:
                        continue
                    child_src_path = src + "/" + item
                    child_dst_path = dst + "/" + item
                    self.cp(child_src_path, child_dst_path, recursive=True)
            else:
                # Copy file
                new_inode_id = self.superblock.allocate_inode()
                new_inode = Inode(
                    new_inode_id,
                    is_dir=False,
                    owner=self.current_user["uid"],
                    perms=src_inode.perms,
                )
                new_inode.blocks = src_inode.blocks.copy()
                new_inode.size = src_inode.size
                self.inodes[new_inode_id] = new_inode
                self.data_blocks[dst_parent_id][dst_name] = new_inode_id

            self.save_disk()
            return f"Copied {src} to {dst}"
        except Exception as e:
            return f"Error copying {src} to {dst}: {e}"

    def mv(self, src, dst):
        try:
            src_inode_id = self.resolve_path(src)
            src_inode = self.inodes[src_inode_id]
            dst_parent_path = os.path.dirname(dst)
            dst_name = os.path.basename(dst)

            if dst_parent_path:
                dst_parent_id = self.resolve_path(dst_parent_path)
            else:
                dst_parent_id = self.current_dir_inode

            dst_parent_inode = self.inodes[dst_parent_id]

            if not self.has_perms(src_inode, "r"):
                return f"Permission denied to read source '{src}'."
            if not self.has_perms(dst_parent_inode, "w"):
                return f"Permission denied to write in destination directory '{dst}'."

            # 检查目标是否存在
            try:
                dst_inode_id = self.resolve_path(dst)
                dst_inode = self.inodes[dst_inode_id]
                if dst_inode.is_dir:
                    # 如果目标是目录，将源移动到该目录内
                    new_dst_path = os.path.join(dst, os.path.basename(src))
                    return self.mv(src, new_dst_path)
                else:
                    # 如果目标是文件，覆盖它
                    self.delete(dst)
            except Exception:
                # 目标不存在，继续移动
                pass

            # 执行移动操作
            src_parent_id = self.resolve_path(os.path.dirname(src))
            src_parent_block_id = self.inodes[src_parent_id].blocks[0]
            dst_parent_block_id = self.inodes[dst_parent_id].blocks[0]

            # 从源父目录中移除
            del self.data_blocks[src_parent_block_id][os.path.basename(src)]

            # 添加到目标父目录
            self.data_blocks[dst_parent_block_id][dst_name] = src_inode_id

            # 如果移动的是目录，更新 '..' 条目
            if src_inode.is_dir:
                src_block_id = src_inode.blocks[0]
                self.data_blocks[src_block_id][".."] = dst_parent_id

            self.save_disk()
            return f"Moved {src} to {dst}"
        except Exception as e:
            return f"Error moving {src} to {dst}: {e}"

    def chmod(self, file_name, perms):
        inode_id = self.resolve_path(file_name)
        inode = self.inodes[inode_id]
        if not (
            self.current_user["uid"] == inode.owner
            or self.current_user["uid"] == 0
            or self.isSudo
        ):
            return "Permission denied"
        try:
            new_perms = int(perms, 8)
            if not (0 <= new_perms <= 0o777):
                raise ValueError("Permissions out of range.")
        except ValueError:
            raise Exception(
                f"Invalid permission format '{perms}'. Use octal (e.g., '755')."
            )
        inode.perms = new_perms
        self.save_disk()
        return f"Permissions of '{file_name}' changed to {oct(new_perms)[2:]}"

    def chown(self, file_name, username):
        inode_id = self.resolve_path(file_name)
        inode = self.inodes[inode_id]
        if self.current_user["uid"] != 0 and not self.isSudo:
            return (
                f"Permission denied: Only admin can change ownership for '{file_name}'."
            )
        for user in USERS:
            if user["username"] == username:
                self.inodes[inode_id].owner = user["uid"]
                self.save_disk()
                return f"Owner of {file_name} changed to {username}"
        return "User not found"

    def ln(self, src, dst):
        src_inode_id = self.resolve_path(src)
        dst_parent = self.resolve_path(os.path.dirname(dst))
        name = os.path.basename(dst)
        if not self.has_perms(self.inodes[src_inode_id], "r"):
            return "Permission denied to read source"
        if not self.has_perms(self.inodes[dst_parent], "w"):
            return "Permission denied to write to destination directory"
        if name in self.data_blocks.get(dst_parent, {}):
            return f"Destination {dst} already exists"
        self.data_blocks.setdefault(dst_parent, {})[name] = src_inode_id
        self.inodes[src_inode_id].link_count += 1
        self.inodes[src_inode_id].links.append(dst)
        self.save_disk()
        return f"Link {dst} created for {src}"

    def find(self, name):
        res = []

        def search(inode_id, path):
            if not self.has_perms(self.inodes[inode_id], "r"):
                return
            dir_block_id = self.inodes[inode_id].blocks[0]
            for item, child_inode_id in self.data_blocks[dir_block_id].items():
                if item == name:
                    res.append(path_join(path, item))
                if self.inodes[child_inode_id].is_dir and item not in [".", ".."]:
                    search(child_inode_id, path_join(path, item))

        current_path = self.get_current_path()
        search(self.current_dir_inode, current_path)
        return "\n".join(res) if res else f"{name} not found in {current_path}"

    def get_current_path(self):
        path = []
        inode_id = self.current_dir_inode
        while inode_id != 0:
            parent_inode_id = self.data_blocks[self.inodes[inode_id].blocks[0]][".."]
            parent_block_id = self.inodes[parent_inode_id].blocks[0]
            for name, id in self.data_blocks[parent_block_id].items():
                if id == inode_id:
                    path.append(name)
                    break
            inode_id = parent_inode_id
        return "/" + "/".join(reversed(path)) if path else "/"

    def help(self):
        help_texts = {
            "login <username> <password>": "Log in as a user.",
            "logout": "Log out the current user.",
            "format": "Initialize and format the disk (WARNING: erases all data).",
            "mkdir <dirname>": "Create a new directory in the current location.",
            "cd <path>": "Change current directory. Supports '.', '..', '~', and simple absolute/relative paths.",
            "dir/ls <path>": "List contents of the directory.",
            "create <filename>": "Create a new empty file in the current location.",
            "open <filename> <mode>": "Open a file. Mode can be 'r' (read) or 'w' (write) or rw.",
            "write <fd> <data>": "Write data string to an open file descriptor (fd).",
            "read <fd>": "Read data from an open file descriptor (fd).",
            "close <fd>": "Close an open file descriptor (fd).",
            "delete <filename>": "Delete a file from the current directory.",
            "rm <path> [-r]": "Remove a file or directory. Use -r for recursive deletion.",
            "cp <src> <dest>": "Copy file or directory.",
            "mv <src> <dest>": "Move/rename file or directory.",
            "chmod <path> <octal_perms>": "Change file permissions.",
            "chown <path> <uid>": "Change file owner.",
            "ln <src> <dest>": "Create a hard link.",
            "find <path> <name>": "Find files or directories.",
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
        path = []
        inode_id = self.current_dir_inode
        visited = set()
        while inode_id != 0 and inode_id not in visited:
            parent_inode = self.data_blocks.get(inode_id, {}).get("..")
            if parent_inode is None:
                return f"{self.current_user['username']}@myFS:[invalid path] $ "
            visited.add(inode_id)
            parent_data = self.data_blocks.get(parent_inode, {})
            name = None
            for n, id in parent_data.items():
                if id == inode_id and n not in [".", ".."]:
                    name = n
                    break
            if name:
                path.append(name)
            inode_id = parent_inode
        path = "/".join(reversed(path)) if path else "/"
        return f"{self.current_user['username']}@myFS:/{path} $ "


def tokenizer(prompt):
    inQuotes = False
    tokens = []
    current_token = ""
    for char in prompt:
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
    return tokens


# 主程序
def main():
    fs = FileSystem()
    print("欢迎使用模拟文件系统！输入 'help' 查看命令。")
    while True:
        try:
            cmd = input(fs.get_prompt()).strip()
        except KeyboardInterrupt:
            print()
            continue
        cmd = tokenizer(cmd)
        if not cmd:
            continue
        command = cmd[0]
        if command == "sudo":
            command = cmd[1]
            args = cmd[2:]
            try:
                passwd = input("Enter admin password:")
            except KeyboardInterrupt:
                print(f"Command {command} did not executed.")
                continue
            if passwd != "admin":
                print(f"Wrong password. Command {command} did not executed.")
                continue
            fs.isSudo = True
        else:
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
                print(fs.dir(*args))
            elif command == "create" and len(args) == 1:
                print(fs.create(args[0]))
            elif command == "open" and len(args) == 2:
                print(fs.open(*args))
            elif command == "write" and len(args) == 2:
                print(fs.write(convert_fd(args[0]), args[1]))
            elif command == "read" and len(args) == 1:
                print(fs.read(convert_fd(args[0])))
            elif command == "close" and len(args) == 1:
                print(fs.close(convert_fd(args[0])))
            elif command in ["delete", "rm"] and len(args) in [1, 2]:
                recursive = len(args) == 2 and args[1] == "-r"
                if len(args) == 2 and not recursive:
                    print("Invalid flag; use -r for recursive deletion")
                else:
                    print(fs.delete(args[0], recursive=recursive))
            elif command == "format":
                fs.format()
                print("文件系统已格式化")
            elif command == "cp" and 2 <= len(args) <= 3:
                recursive = len(args) == 3 and args[2] == "-r"
                if len(args) == 3 and not recursive:
                    print("Invalid flag; use -r for recursive copy")
                else:
                    print(fs.cp(args[0], args[1], recursive))
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
        fs.isSudo = False


if __name__ == "__main__":
    main()
