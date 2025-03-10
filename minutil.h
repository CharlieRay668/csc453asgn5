#include <unistd.h> 
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#define PARTITION_OFFSET 0x1BE
#define PARTITION_TYPE 0x81
#define VALID_BIT_ONE 0x55
#define VALID_BIT_TWO 0xAA
#define MINIX_MAGIC 0x4D5A
#define MINIX_MAGIC_REVERSED 0x5A4D
#define INODE_SIZE 64
#define DIR_SIZE 64
#define DIRECT_ZONES 7
#define MAX_NAME_LENGTH 60
#define SECTOR_SIZE 512
#define CHECK_BIT_ADDR 510

#define FILE_TYPE       0170000
#define REGULAR_FILE    0100000
#define DIRECTORY       0040000
#define OWNER_READ      0000400
#define OWNER_WRITE     0000200
#define OWNER_EXEC      0000100
#define GROUP_READ      0000040
#define GROUP_WRITE     0000020
#define GROUP_EXEC      0000010
#define OTHER_READ      0000004
#define OTHER_WRITE     0000002
#define OTHER_EXEC      0000001


typedef struct __attribute__ ((__packed__)) partition_table {
    uint8_t bootind;
    uint8_t startd_head;
    uint8_t start_sec;
    uint8_t start_cyl;
    uint8_t type;
    uint8_t end_head;
    uint8_t end_sec;
    uint8_t end_cyl;
    uint32_t lFirst;
    uint32_t size;
} partition_table;

typedef struct __attribute__ ((__packed__)) superblock {
    uint32_t ninodes;
    uint16_t pad1;
    int16_t  i_blocks;
    int16_t  z_blocks;
    uint16_t firstdata;
    int16_t  log_zone_size;  /* log2(blocks_per_zone) */
    int16_t  pad2;
    uint32_t max_file;
    uint32_t zones;
    int16_t  magic;
    int16_t  pad3;
    uint16_t blocksize;
    uint8_t  subversion;
} superblock;

typedef struct __attribute__ ((__packed__)) inode {
    uint16_t mode; /* mode */
    uint16_t links; /* number or links */
    uint16_t uid;
    uint16_t gid;
    uint32_t size;
    int32_t atime;
    int32_t mtime;
    int32_t ctime;
    uint32_t zone[DIRECT_ZONES];
    uint32_t indirect;
    uint32_t two_indirect;
    uint32_t unused;
} inode;

typedef struct __attribute__ ((__packed__)) dir_entry {
    uint32_t inode;
    unsigned char name[60];
} dir_entry;

void get_mode_string(uint16_t mode, char *out);
void usage(const char *progname);
void print_superblock(superblock *sb);
void print_inode(inode *in);
void print_partition_table(partition_table *pt);
void compute_fs_offsets(superblock *sb, long *inode_bitmap_start, 
long *zone_bitmap_start, long *inode_table_start, long *data_zone_start);
void clean_path(char *in, char *out, size_t outsize);
int read_partition_table(FILE *fp, long base_offset, partition_table *table);

int find_inode(FILE *fp, 
                    superblock *sb, 
                    char *path, 
                    long partition_offset,
                    int verbose);

int collect_zones(FILE *fp,
                superblock *sb,
                const inode *in,
                long partition_offset,
                uint32_t **out_zones,
                int *out_count);

int follow_indirect(FILE* fp, 
                superblock *sb, 
                uint32_t zone_num, 
                long partition_offset,
                size_t *count,
                size_t *capacity,
                uint32_t **zones);

int seek_to_zone(FILE* fp, 
    superblock *sb, 
    long zone_num, 
    long partition_offset);

int ensure_capacity(size_t needed, size_t *capacity, uint32_t **zones);

int read_inode(FILE *fp, 
            superblock *sb, 
            int inode_num, 
            inode *in, 
            long partition_offset);

int read_superblock(FILE *fp, long partition_start, superblock *sb);

long compute_partition_offset(const partition_table *pentry);

long get_partition_offset(int partition,
                            FILE *fp,
                            int subpart,
                            int verbose);

int process_args(int argc, 
                char *argv[], 
                int *verbose, 
                int *partition, 
                int *subpart, 
                int *arg_index);