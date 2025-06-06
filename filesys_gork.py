import os
import ast
import pickle
from pathlib import Path
from enum import Enum

# 常量定义
BLOCK_SIZE = 1024  # 每个块 1KB
TOTAL_BLOCKS = 10240  # 总块数，约 10MB
INODE_COUNT = 1024  # 索引节点数
DISK_PATH = "./disk/disk.img"
HOME_DIR = "/home"
NICFREE = 50  # 成组链接法中，每组包含的空闲块数量（超级块缓存以及磁盘链块）

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
    APPEND = "a"
    READ_APPEND = "ra"


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

class SeekMode(Enum):
    SEEK_SET = 0  # 从文件开头
    SEEK_CUR = 1  # 从当前位置
    SEEK_END = 2  # 从文件末尾

# 超级块结构
class SuperBlock:
    def __init__(self):
        self.block_size = BLOCK_SIZE
        self.total_blocks = TOTAL_BLOCKS
        self.inode_count = INODE_COUNT
        self.free_inodes = list(range(10, INODE_COUNT))

        # 成组链接法相关变量
        self.s_nfree = 0  # s_free 栈中当前空闲块的数量
        self.s_free = [0] * NICFREE  # 存储空闲块块号的栈

    def allocate_block(self, fs):  # fs 是 FileSystem 实例，用于访问 data_blocks
        """
        分配一个空闲磁盘块。
        使用成组链接法。
        """
        if self.s_nfree <= 0:
            # s_nfree 为0表示超级块缓存已空，理论上在上次分配后已尝试加载。
            # 如果仍然为0，表示磁盘已满或自由链表损坏。
            raise Exception("磁盘已满或超级块空闲块列表缓存为空且无法补充。")

        self.s_nfree -= 1
        block_id = self.s_free[self.s_nfree]  # 从栈顶获取块号

        if block_id == 0:  # 块号0通常是无效的或保留的，不应被分配
            self.s_nfree += 1  # 恢复计数
            raise Exception("尝试分配块号0，此块号无效或表示空闲链表结束。")

        if self.s_nfree == 0:
            # 超级块中的栈已空，刚分配的 block_id 本身应包含下一组空闲块的信息
            # (它之前是栈顶，现在被分配出去，同时它也是下一组的“头块”)
            try:
                # print(f"调试：分配块 {block_id}，s_nfree 变为 0。从块 {block_id} 加载下一组。")
                next_group_data = fs.data_blocks[
                    block_id
                ]  # 该块应存储 [数量, 块号1, 块号2, ...]

                if not isinstance(next_group_data, list) or not next_group_data:
                    # 如果 block_id 的内容不是预期的列表格式，说明空闲链可能已损坏
                    # 这可能是因为该块被错误地覆写了
                    self.s_nfree = 0  # 确保 s_nfree 保持为0，下次分配会失败
                    raise Exception(
                        f"空闲链表损坏：块 {block_id} (应为头块) 包含无效内容: {next_group_data}"
                    )

                count_in_disk_group = next_group_data[0]  # 列表第一个元素是数量
                if not (0 <= count_in_disk_group <= NICFREE):  # 数量有效性检查
                    self.s_nfree = 0
                    raise Exception(
                        f"空闲链表损坏：在头块 {block_id} 中发现无效的空闲块数量 {count_in_disk_group}"
                    )

                self.s_nfree = count_in_disk_group
                for i in range(self.s_nfree):
                    self.s_free[i] = next_group_data[i + 1]  # 后续元素是块号

                # print(f"调试：已加载 {self.s_nfree} 个块到超级块缓存。新的 s_free 栈顶: {self.s_free[self.s_nfree-1] if self.s_nfree > 0 else '空'}")
                # 如果加载后 s_nfree 仍然是0 (例如，从磁盘加载了一个空列表)，
                # 那么下一次调用 allocate_block 时，顶部的 s_nfree <= 0 检查会捕获到“磁盘满”的情况。

            except KeyError:
                # 如果 block_id 不在 fs.data_blocks 中，说明链断裂或磁盘确实满了。
                # 此时 s_nfree 已经是0，下次分配会失败。
                self.s_nfree = 0  # 确保 s_nfree 为0，表示耗尽
                # print(f"调试：空闲链表结束或链断裂。无法从块 {block_id} 加载下一组。s_nfree 为 0。")
            except IndexError:
                self.s_nfree = 0  # 出错时确保 s_nfree 为0
                raise Exception(f"空闲链表损坏：块 {block_id} (头块) 数据格式错误。")

        # print(f"调试：已分配块 {block_id}。超级块 s_nfree 现在是 {self.s_nfree}")
        return block_id

    def free_block(self, block_id, fs):  # fs 是 FileSystem 实例
        """
        释放一个磁盘块。
        使用成组链接法。
        """
        if block_id <= 0:  # 不能释放块0或无效块
            print(f"警告：尝试释放无效的块 ID {block_id}")
            return

        if self.s_nfree >= NICFREE:
            # 超级块的 s_free 栈已满。
            # 将当前 s_free 的内容写入到正被释放的 block_id 块中，
            # 使 block_id 成为一个新的包含一组空闲块号的“头块”。
            # print(f"调试：释放块 {block_id}。超级块缓存已满。将缓存写入块 {block_id}。")
            data_to_write_to_disk = [self.s_nfree] + [
                self.s_free[i] for i in range(self.s_nfree)
            ]
            fs.data_blocks[block_id] = data_to_write_to_disk  # 写入 [数量, 块号1, ...]
            self.s_nfree = 0  # 清空超级块缓存计数

        # 将刚释放的 block_id 加入到超级块的 s_free 栈中
        self.s_free[self.s_nfree] = block_id
        self.s_nfree += 1
        # print(f"调试：已释放块 {block_id}。超级块 s_nfree 现在是 {self.s_nfree}。s_free 栈顶: {self.s_free[self.s_nfree-1]}")

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
        self.link_count = (
            1 if not is_dir else 2
        )  # Files start with 1 link, directories with 2 (self and parent)
        self.lock = False


class OpenFileEntry:
    def __init__(self, inode_id: int, mode: OpenMode):
        self.id = inode_id
        self.openMode = mode
        self.offset = 0  # 新增：文件指针偏移量

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
        home_inode = Inode(1, is_dir=True, owner=0, perms=0o755)
        self.inodes[1] = home_inode

        # Assign block 1 to /home directory and set its entries
        self.data_blocks[1] = {".": 1, "..": 0}  # /home's entries
        self.inodes[1].blocks.append(1)  # /home inode points to block 1

        # 初始化空闲块列表 (使用成组链接法)
        # 假设块0-9被保留或已用于上述目录。实际空闲数据块从10开始。
        first_actual_data_block = 10
        # 将所有可用块（从后向前）加入空闲列表，这样较低编号的块会先被分配
        for i in range(TOTAL_BLOCKS - 1, first_actual_data_block - 1, -1):
            self.superblock.free_block(i, self)  # 将 FileSystem 实例传递给 free_block
        self.save_disk()
        self.isSudo = True
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
        self.isSudo = False

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
        block_id = self.superblock.allocate_block(self)
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
        # items = self.data_blocks.get(current_inode, {})
        result = []
        headers = ["Permissions", "Links", "Owner", "Size", "Name"]
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
            owner_uid = inode.owner
            owner = next(
                (u["username"] for u in USERS if u["uid"] == owner_uid),
                str(owner_uid),
            )
            size = inode.size if not inode.is_dir else ""
            line = f"{perm_str:<12} {inode.link_count:>4} {owner:<10} {size:>6} {name}"
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
    
    def seek(self, fd, offset, whence=SeekMode.SEEK_SET):
        """
        移动文件描述符 fd 的文件指针到指定位置。
        参数：
            fd: 文件描述符
            offset: 偏移量（可以为正或负，取决于 whence）
            whence: 定位方式（SEEK_SET, SEEK_CUR, SEEK_END）
        返回：
            当前文件指针位置（成功）或错误信息（失败）
        """
        if not isinstance(fd, int) or fd not in self.open_files:
            return "无效的文件描述符"
        
        entry = self.open_files[fd]
        inode_id = entry.id
        inode = self.inodes[inode_id]
        
        if inode.is_dir:
            return f"文件描述符 {fd} 指向一个目录，无法执行 seek 操作"
        
        # 计算新的偏移量
        if whence == SeekMode.SEEK_SET:
            new_offset = offset
        elif whence == SeekMode.SEEK_CUR:
            new_offset = entry.offset + offset
        elif whence == SeekMode.SEEK_END:
            new_offset = inode.size + offset
        else:
            return "无效的 whence 参数"
        
        # 检查偏移量合法性
        if new_offset < 0:
            return "偏移量不能为负"
        
        # 对于只读模式，偏移量不能超过文件大小
        if entry.openMode == OpenMode.READ and new_offset > inode.size:
            return f"偏移量 {new_offset} 超出文件大小 {inode.size}"
        
        # 更新偏移量
        entry.offset = new_offset
        return f"文件指针移动到位置 {entry.offset}"

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
        if "a" in openmode and not self.has_perms(inode, "w"):
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
        if entry.openMode not in {OpenMode.WRITE, OpenMode.READ_WRITE, OpenMode.APPEND, OpenMode.READ_APPEND}:
            return "Error. File opened in readonly mode"
        inode = self.inodes[inode_id]

        if entry.openMode == OpenMode.APPEND or entry.openMode == OpenMode.READ_APPEND:
            block_id = self.superblock.allocate_block(self)
            self.data_blocks[block_id] = data.encode()
            inode.blocks.append(block_id)
            inode.size += len(data)
        else:
            # 覆写模式：从 entry.offset 开始写入
            write_size = len(data)
            cur_offset = entry.offset
            data_bytes = data.encode()  # 转换为字节以便分块处理
            bytes_written = 0
            block_index = 0

            # 遍历现有块，寻找写入起始位置
            while block_index < len(inode.blocks) and bytes_written < write_size:
                block_id = inode.blocks[block_index]
                block_data = self.data_blocks[block_id].decode()
                block_len = len(block_data)

                if cur_offset < block_len:
                    # 当前块包含写入起始位置
                    available_in_block = block_len - cur_offset
                    bytes_to_write = min(write_size - bytes_written, available_in_block)

                    # 写入当前块
                    new_block_data = (
                        block_data[:cur_offset]
                        + data[bytes_written:bytes_written + bytes_to_write]
                        + block_data[cur_offset + bytes_to_write:]
                    )
                    self.data_blocks[block_id] = new_block_data.encode()

                    bytes_written += bytes_to_write
                    cur_offset += bytes_to_write  # 更新块内偏移量

                    if bytes_written < write_size:
                        # 还有数据未写入，移动到下一个块
                        block_index += 1
                        cur_offset = 0  # 下一个块从头开始
                    else:
                        break
                else:
                    # 偏移量超出当前块，跳到下一个块
                    cur_offset -= block_len
                    block_index += 1

            # 如果数据未写完，分配新块
            while bytes_written < write_size:
                block_id = self.superblock.allocate_block(self)
                bytes_to_write = min(write_size - bytes_written, BLOCK_SIZE)
                new_block_data = data[bytes_written:bytes_written + bytes_to_write]
                self.data_blocks[block_id] = new_block_data.encode()
                inode.blocks.append(block_id)
                bytes_written += bytes_to_write

            # 如果写入位置超出原文件大小，更新文件大小
            if entry.offset + write_size > inode.size:
                inode.size = entry.offset + write_size

            # 清理可能不再需要的块
            if block_index < len(inode.blocks):
                # 释放从 block_index 之后的块（如果它们被完全覆盖且无需保留）
                blocks_to_free = inode.blocks[block_index + 1:]
                for block_id in blocks_to_free:
                    self.superblock.free_block(block_id, self)
                inode.blocks = inode.blocks[:block_index + 1]
        self.save_disk()
        return "Write successful"

    def read(self, fd, length=None):
        if not isinstance(fd, int) or fd not in self.open_files:
            return "无效的文件描述符"
        entry = self.open_files[fd]
        inode_id = entry.id
        if entry.openMode not in {OpenMode.READ, OpenMode.READ_WRITE, OpenMode.READ_APPEND}:
            return "错误：文件以只写模式打开"
        inode = self.inodes[inode_id]
        
        # 检查是否为目录或空文件
        if inode.is_dir:
            return f"文件描述符 {fd} 指向一个目录，无法读取"
        if inode.size == 0:
            return ""
        
        # 检查偏移量是否有效
        if entry.offset > inode.size:
            return f"偏移量 {entry.offset} 超出文件大小 {inode.size}"
        
        # 计算剩余可读字节数
        remaining_size = inode.size - entry.offset
        if remaining_size <= 0:
            return ""
        
        # 确定读取长度
        read_length = remaining_size if length is None else min(length, remaining_size)
        if read_length <= 0:
            return ""
        
        print(f"读取 {read_length} 字节，从偏移量 {entry.offset} 开始")
        
        content = ""
        bytes_read = 0
        bytes_to_read = read_length
        
        cur_offset = entry.offset
        
        # 遍历数据块，从 offset 开始读取指定长度
        for block_id in inode.blocks:
            if bytes_to_read <= 0:
                break
            block_data = self.data_blocks[block_id].decode()
            
            # 如果当当前块的长度小于我们要读的offset，就跳过这个块
            if cur_offset >= len(block_data):
                cur_offset -= len(block_data)
                continue
            
            # 忽略前 cur_offset 字节
            if cur_offset > 0:
                block_data = block_data[cur_offset:]
                cur_offset = 0
            # 读取剩余的字节
            if bytes_to_read > len(block_data):
                content += block_data
                bytes_to_read -= len(block_data)
            else:
                content += block_data[:bytes_to_read]
                bytes_to_read = 0

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
            errors = []
            for item, child_inode_id in dir_contents.items():
                if item in [".", ".."]:
                    continue
                child_path = f"{file_name}/{item}"
                res = self.delete(child_path, recursive=True)
                if "in use" in res or "Permission denied" in res:
                    errors.append(res)
            if errors:
                return "\n".join(errors)

        # Decrease link count
        inode.link_count -= 1
        if inode.link_count == 0:
            # Free all data blocks
            for block_id in inode.blocks:
                self.superblock.free_block(block_id, self)
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
            # Resolve source inode
            src_inode_id = self.resolve_path(src)
            src_inode = self.inodes[src_inode_id]

            # Check if source is a directory and -r is not specified
            if src_inode.is_dir and not recursive:
                return f"cp: omitting directory '{src}' (use -r to copy directories)"

            # Resolve destination parent and name
            dst_parent_path = os.path.dirname(dst)
            dst_name = os.path.basename(dst)

            if dst_parent_path:
                dst_parent_id = self.resolve_path(dst_parent_path)
            else:
                dst_parent_id = self.current_dir_inode

            dst_parent_inode = self.inodes[dst_parent_id]
            dst_parent_block_id = dst_parent_inode.blocks[0]

            # Check permissions
            if not self.has_perms(src_inode, "r"):
                return f"Permission denied to read source '{src}'."
            if not self.has_perms(dst_parent_inode, "w"):
                return f"Permission denied to write in destination directory '{dst}'."

            # Check if destination exists
            try:
                dst_inode_id = self.resolve_path(dst)
                dst_inode = self.inodes[dst_inode_id]
                if dst_inode.is_dir:
                    # If destination is a directory, copy into it
                    new_dst_path = os.path.join(dst, os.path.basename(src))
                    return self.cp(src, new_dst_path, recursive)
                else:
                    # If destination is a file, overwrite it
                    self.delete(dst)
            except Exception:
                pass  # Destination doesn't exist, proceed

            if src_inode.is_dir:
                # Create new directory
                new_inode_id = self.superblock.allocate_inode()
                new_inode = Inode(
                    new_inode_id,
                    is_dir=True,
                    owner=self.current_user["uid"],
                    perms=src_inode.perms,
                )
                self.inodes[new_inode_id] = new_inode
                block_id = self.superblock.allocate_block(self)
                # Initialize directory data block
                self.data_blocks[block_id] = {".": new_inode_id, "..": dst_parent_id}
                new_inode.blocks.append(block_id)
                # Add new directory to parent
                self.data_blocks[dst_parent_block_id][dst_name] = new_inode_id

                # Recursively copy directory contents
                src_block_id = src_inode.blocks[0]
                if not isinstance(self.data_blocks.get(src_block_id, {}), dict):
                    raise Exception(
                        f"Source directory block {src_block_id} is not a dictionary"
                    )
                for item, child_inode_id in self.data_blocks.get(
                    src_block_id, {}
                ).items():
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
                # Copy file data blocks
                for block_id in src_inode.blocks:
                    if not isinstance(self.data_blocks.get(block_id, {}), bytes):
                        raise Exception(f"Source file block {block_id} is not bytes")
                    new_block_id = self.superblock.allocate_block(self)
                    self.data_blocks[new_block_id] = self.data_blocks[block_id]
                    new_inode.blocks.append(new_block_id)
                new_inode.size = src_inode.size
                self.inodes[new_inode_id] = new_inode
                # Add new file to parent
                self.data_blocks[dst_parent_block_id][dst_name] = new_inode_id

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
                    new_dst_path = path_join(dst, os.path.basename(src))
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
        if self.inodes[src_inode_id].is_dir:
            return "ln only supports hard link files."
        if name in self.data_blocks.get(dst_parent, {}):
            return f"Destination {dst} already exists"
        self.data_blocks.setdefault(dst_parent, {})[name] = src_inode_id
        self.inodes[src_inode_id].link_count += 1
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
            "seek <fd> <offset> [whence]": "Move the file pointer of an open file descriptor (fd) to a specified position. 'whence' can be SEEK_SET, SEEK_CUR, or SEEK_END.",
            "open <filename> <mode>": "Open a file. Mode can be 'r' (read) or 'w' (write) or rw (read/write) or a (append) or ra (read/append).",
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
            dir_block_id = self.inodes[inode_id].blocks[0]
            if not isinstance(self.data_blocks.get(dir_block_id, {}), dict):
                return (
                    f"{self.current_user['username']}@myFS:[invalid directory block] $ "
                )
            parent_inode = self.data_blocks[dir_block_id].get("..")
            if parent_inode is None:
                return f"{self.current_user['username']}@myFS:[invalid path] $ "
            visited.add(inode_id)
            parent_data = self.data_blocks.get(self.inodes[parent_inode].blocks[0], {})
            name = None
            for n, id in parent_data.items():
                if id == inode_id and n not in [".", ".."]:
                    name = n
                    break
            if name:
                path.append(name)
            inode_id = parent_inode
        path = "/".join(reversed(path)) if path else ""
        return f"{self.current_user['username']}@myFS:/{path} $ "


def tokenizer(prompt):
    tokens = []
    current_token = ""
    in_single_quote = False
    in_double_quote = False
    i = 0
    while i < len(prompt):
        char = prompt[i]

        # 处理转义字符
        if char == "\\" and (in_single_quote or in_double_quote):
            i += 1
            if i >= len(prompt):
                break
            next_char = prompt[i]
            if next_char == "n" and in_double_quote:
                current_token += "\n"
            elif next_char == "t" and in_double_quote:
                current_token += "\t"
            elif next_char == '"' and in_double_quote:
                current_token += '"'
            elif next_char == "'" and in_single_quote:
                current_token += "'"
            elif next_char == "\\":
                current_token += "\\"
            else:
                current_token += "\\" + next_char  # 保留原样
            i += 1
            continue

        # 引号处理
        if char == '"':
            if in_double_quote:
                in_double_quote = False
                if current_token:
                    tokens.append(current_token)
                    current_token = ""
            else:
                if not in_single_quote:
                    in_double_quote = True
                    if current_token:
                        tokens.append(current_token)
                        current_token = ""
            i += 1
            continue
        elif char == "'":
            if in_single_quote:
                in_single_quote = False
                if current_token:
                    tokens.append(current_token)
                    current_token = ""
            else:
                if not in_double_quote:
                    in_single_quote = True
                    if current_token:
                        tokens.append(current_token)
                        current_token = ""
            i += 1
            continue

        # 空格分隔逻辑
        if char.isspace():
            if in_single_quote or in_double_quote:
                current_token += char
            else:
                if current_token:
                    tokens.append(current_token)
                    current_token = ""
        else:
            current_token += char
        i += 1

    # 结尾处理
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
                fs.chdir(args[0])
            elif command in ["dir", "ls"]:
                print(fs.dir(*args))
            elif command == "create" and len(args) == 1:
                print(fs.create(args[0]))
            elif command == "open" and len(args) == 2:
                print(fs.open(*args))
            elif command == "write" and len(args) == 2:
                try:
                    data = ast.literal_eval(f'"{args[1]}"')  # 将 "\n" 转换为换行符
                except (SyntaxError, ValueError):
                    data = args[1]  # 如果解析失败，保留原始字符串
                print(fs.write(convert_fd(args[0]), data))
            elif command == "read" and len(args) in [1, 2]:
                try:
                    fd = convert_fd(args[0])
                    length = int(args[1]) if len(args) == 2 else None
                    print(fs.read(fd, length))
                except ValueError:
                    print("读取长度必须是整数")
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
            elif command == "seek" and len(args) in [2, 3]:
                try:
                    fd = convert_fd(args[0])
                    offset = int(args[1])
                    whence = SeekMode.SEEK_SET
                    if len(args) == 3:
                        if args[2] == "SEEK_SET":
                            whence = SeekMode.SEEK_SET
                        elif args[2] == "SEEK_CUR":
                            whence = SeekMode.SEEK_CUR
                        elif args[2] == "SEEK_END":
                            whence = SeekMode.SEEK_END
                        else:
                            print("无效的 whence 参数；使用 SEEK_SET, SEEK_CUR 或 SEEK_END")
                            continue
                    print(fs.seek(fd, offset, whence))
                except ValueError:
                    print("偏移量必须是整数")
            else:
                print("未知命令或参数错误，输入 'help' 查看帮助")
        except Exception as e:
            print(f"{e}")
        fs.isSudo = False


if __name__ == "__main__":
    main()
