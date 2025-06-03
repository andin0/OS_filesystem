#ifndef DATA_STRUCTURES_H
#define DATA_STRUCTURES_H
#include "common_defs.h"

struct ProcessOpenFileEntry
{
    int system_table_idx;     // 指向系统级打开文件表中对应条目的索引
    long long current_offset; // 当前读写指针位置
};

struct SuperBlock
{
    int magic_number;            // 文件系统魔数
    long long total_blocks;      // 虚拟磁盘总块数
    long long free_blocks_count; // 空闲数据块数量
    int total_inodes;            // i-node总数
    int free_inodes_count;       // 空闲i-node数量
    int block_size;              // 每块大小 (例如 1024 bytes)
    int inode_size;              // 每个i-node大小

    // i-node 位图信息
    int inode_bitmap_start_block_idx; // i-node位图的起始块号
    int inode_bitmap_blocks_count;    // i-node位图占用的块数

    // i-node 表信息
    int inode_table_start_block_idx;  // i-node表的起始块号
                                      // (i-node表的块数可以根据total_inodes, inode_size, block_size计算得到)

    int first_data_block_idx;    // 第一个数据块的起始块号
    int root_dir_inode_idx;      // 根目录的inode号

    // 成组链接法相关
    int free_block_stack_top_idx; // 空闲块堆栈顶块的块号 (栈中第一个块)

    int max_filename_length;      // 最大文件名长度
    int max_path_length;          // 最大路径长度
};

struct Inode
{
    int inode_id;                // i-node编号 (在某些设计中可能不需要，因为ID是其在表中的位置)
                                 // 但如果存在，可以用于完整性检查
    FileType file_type;          // 文件类型 (文件/目录)
    short permissions;           // 权限 (rwx, 9位, 例如 0755)
    short owner_uid;             // 文件所有者用户ID
    short link_count;            // 硬链接计数
    long long file_size;         // 文件大小 (字节)
    long long creation_time;     // 创建时间 (时间戳)
    long long modification_time; // 最后修改时间 (时间戳)
    long long access_time;       // 最后访问时间 (时间戳)
    int direct_blocks[NUM_DIRECT_BLOCKS]; // 直接数据块指针 (NUM_DIRECT_BLOCKS 在 common_defs.h 定义)
    int single_indirect_block;   // 一级间接数据块指针
    int double_indirect_block;   // 二级间接数据块指针
    // int triple_indirect_block; // 三级间接数据块指针 (根据需要可选)
};

struct DirectoryEntry
{
    char filename[MAX_FILENAME_LENGTH]; // 文件名, 长度使用 common_defs.h 中定义的常量
    int inode_id;                       // 对应的i-node编号
};

struct FreeBlockGroup
{
    int count;                                                   // 本组空闲块数量 (最多 N_FREE_BLOCKS_PER_GROUP)
    int next_group_block_ids[/*N_FREE_BLOCKS_PER_GROUP_CONST*/]; // 指向下一组空闲块的块号
};

struct User
{
    short uid;                          // 用户ID
    char username[MAX_USERNAME_LENGTH]; // 用户名, 长度使用 common_defs.h 中定义的常量
    char *password;                     // 密码明文
    int home_directory_inode_id;        // 用户家目录的inode编号
};

struct SystemOpenFileEntry
{
    int inode_id;      // 文件的inode编号
    Inode inode_cache; // inode的内存副本，避免频繁读盘
    OpenMode mode;     // 打开模式 (read, write, append)
    int open_count;    // 此文件被打开的次数 (被多少个进程级表项引用)
};

#endif // DATA_STRUCTURES_H