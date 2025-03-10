#include <stdio.h>
#include "minutil.h"
#define DEBUG 0

void debug(const char *format, ...) {
    if (DEBUG) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        fflush(NULL);
    }
}


void get_mode_string(uint16_t mode, char *out){
    /* first character is a d if file is a directory, - otherwise */
    if((mode & FILE_TYPE) == DIRECTORY){
        out[0] = 'd';
    } else {
        out[0] = '-';
    }

    /* fill in the rest of the buffer based on the mode bits*/
    out[1] = (mode & OWNER_READ) ? 'r' : '-';
    out[2] = (mode & OWNER_WRITE) ? 'w' : '-';
    out[3] = (mode & OWNER_EXEC) ? 'x' : '-';
    out[4] = (mode & GROUP_READ) ? 'r' : '-';
    out[5] = (mode & GROUP_WRITE) ? 'w' : '-';
    out[6] = (mode & GROUP_EXEC) ? 'x' : '-';
    out[7] = (mode & OTHER_READ) ? 'r' : '-';
    out[8] = (mode & OTHER_WRITE) ? 'w' : '-';
    out[9] = (mode & OTHER_EXEC) ? 'x' : '-';
    out[10] = '\0';
}

void usage(const char *progname) {
    fprintf(stderr, 
            "Usage: %s [ -v ] [ -p num [ -s num ] ] imagefile [ path ]\n", 
            progname);
    fprintf(stderr, 
            "Options:\n");
    fprintf(stderr, 
            "  -p part --- select partition for filesystem (default: none)\n");
    fprintf(stderr, 
            "  -s sub  --- select subpartition for \
            filesystem (default: none)\n");
    fprintf(stderr, 
            "  -v      --- verbose; print superblock info, etc.\n");
    fprintf(stderr, 
            "  -h      --- help; print this usage message\n");
}

void print_superblock(superblock *sb) {
    fprintf(stderr, "Superblock Contents:\n");
    fprintf(stderr, "  ninodes        = %u\n",   sb->ninodes);
    fprintf(stderr, "  i_blocks       = %d\n",   sb->i_blocks);
    fprintf(stderr, "  z_blocks       = %d\n",   sb->z_blocks);
    fprintf(stderr, "  firstdata      = %u\n",   sb->firstdata);
    fprintf(stderr, "  log_zone_size  = %d\n",   sb->log_zone_size);
    fprintf(stderr, "  max_file       = %u\n",   sb->max_file);
    fprintf(stderr, "  zones          = %u\n",   sb->zones);
    fprintf(stderr, "  magic          = 0x%04x\n", (unsigned)sb->magic);
    fprintf(stderr, "  blocksize      = %u\n",   sb->blocksize);
    fprintf(stderr, "  subversion     = %u\n\n", sb->subversion);
}

void compute_fs_offsets(superblock *sb, long *inode_bitmap_start, 
    long *zone_bitmap_start, long *inode_table_start, long *data_zone_start){
    long block_size = sb->blocksize;
    
    /* inode bitmao starts at block 2 */
    long inode_bitmap_block = 2;
    /* zone bit map starts at block 2 + # of inodes*/
    long zone_bitmap_block = 2 + sb->i_blocks;
    /* inode table starts at zone_bitmap_block plust # of zone blocks*/
    long inode_table_block = zone_bitmap_block + sb->z_blocks;

    /* convert to multiple of block size*/
    *inode_bitmap_start = inode_bitmap_block * block_size;
    *zone_bitmap_start = zone_bitmap_block * block_size;
    *inode_table_start = inode_table_block * block_size;
    
    /* compute the number of inodes and find the 
    data zone after the last inode*/
    long total_inode_bytes = sb->ninodes * INODE_SIZE;
    long inode_blocks = (total_inode_bytes + block_size - 1) / block_size;
    *data_zone_start = *inode_table_start + (inode_blocks * block_size);
}

void canonicalize_path(char *in, char *out, size_t outsize) {
    if (!in || !*in) {
        // Empty => root "/"
        strncpy(out, "/", outsize);
        out[outsize - 1] = '\0';
        return;
    }
    // We'll build an output in a buffer
    char delim[] = "/";
    char *token;
    char *ptr = out;
    size_t written = 0;

    *ptr++ = '/';
    written++;

    token = strtok(in, delim);
    while(token != NULL){
        size_t len = strlen(token);

        if (written + len + 1 >= outsize) {
            perror("Path longer than 1024 characters\n");
            out[written] = '\0';
            break;
        }

        memcpy(out + written, token, len);
        written += len;
        token = strtok(NULL, delim);
        
        if (token != NULL) {
            if (written + 1 >= outsize) break;
            out[written] = '/';
            written++;
        }
    }
    out[written] = '\0';
}

/* Reads the partition headers into the provided table */
int read_partition_table(FILE *fp, long base_offset, partition_table *table) {
    /* move the pointer in the file to the partition */
    fseek(fp, base_offset + PARTITION_OFFSET, SEEK_SET);
    /* Actually read the partition headers into the table */
    if(fread(table, sizeof(partition_table), 4, fp) != 4){
        perror("Failed to read partition_table\n");
        fflush(NULL);
    }
    /**/
    unsigned char valid_bits[2];
    /* move to the validation bits at 510 & 511 */
    if(fseek(fp, 510, SEEK_SET)){
        perror("Failed to seek to signature bits\n");
        fflush(NULL);
    }
    if(fread(valid_bits, 1, 2, fp) != 2){
        perror("Failed to read signature bits\n");
        fflush(NULL);
    }
    if (valid_bits[0] != 0x55 || valid_bits[1] != 0xAA) {
        perror("Invalid signature bits");
        return 1;
    }
    if (table->type != 0x81){
        perror("Invalid partition table type");
        return 1;
    }
    return 0;
}


long compute_partition_offset(const partition_table *pentry) {
    /*lFirst is in sectors and each sector is 512B*/
    return (long)pentry->lFirst * SECTOR_SIZE;
}

int read_superblock(FILE *fp, long partition_start, superblock *sb) {
    /* "the superblock is always found at offset 1024, regardless
    of the filesystem's block size." */
    long super_offset = partition_start + 1024;
    /* Move pointer to the superblock */
    if(fseek(fp, super_offset, SEEK_SET)){
        perror("Failed to seek in read_superblock\n");
        return 1;
    };
    /* Read the superblock in */
    if(fread(sb, sizeof(superblock), 1, fp) != 1){
        perror("Reading failed in read_superblock\n");
        return 1;
    }
    /* check valid superblock */
    if (sb->magic != MINIX_MAGIC) {
        perror("Invalid super block magic number\n");
        return 1;
    }

    return 0;
}

int read_inode(FILE *fp, 
            superblock *sb, 
            int inode_num, 
            inode *in, 
            long partition_offset){
    long inode_bitmap_offset, 
        zone_bitmap_offset, 
        inode_table_offset, 
        data_offset;
    compute_fs_offsets(sb, 
                    &inode_bitmap_offset,
                    &zone_bitmap_offset, 
                    &inode_table_offset, 
                    &data_offset);

                    
    if(inode_num < 1 || inode_num > (int)sb->ninodes){
        fprintf(stderr, "Invalid number of inodes requested: %d\n", 
            inode_num);
        return 1;
    }


    /* each inode is 64 bytes so this tells us where to look in inode table*/
    long offset_within_table = (long)(inode_num - 1) * INODE_SIZE;

    /* where to look in the partition*/
    long inode_pos = 
            partition_offset 
            + inode_table_offset 
            + offset_within_table;
            
    /* move fp to inode and attempt to read */
    if(fseek(fp, inode_pos, SEEK_SET)) {
        perror("Seek failed\n");
        return 1;
    }
    if(fread(in, sizeof(inode), 1, fp) != 1){
        fprintf(stderr, "Read failed for inode: %d\n", inode_num);
        return 1;
    }

    return 0;    
}

void ensure_capacity(size_t needed, size_t capacity, uint32_t *zones) {
    if (needed <= capacity) return;
    while (capacity < needed) {
        capacity *= 2;
    }
    uint32_t *tmp = realloc(zones, capacity * sizeof(uint32_t));
    if (tmp == NULL) {
        free(zones);
        zones = NULL;
        capacity = 0;
    } else {
        zones = tmp;
    }
}

/* seek to a zone */
int seek_to_zone(FILE* fp, 
    superblock *sb, 
    long zone_num, 
    long partition_offset) {
    /* Get the size of a zone as stated in spec */
    long zone_size = sb->blocksize << sb->log_zone_size;
    /* add the partition offset, along with which zone we're in */
    long data_offset = partition_offset + zone_num * zone_size;
    if (fseek(fp, data_offset, SEEK_SET) != 0) {
        perror("fseek");
        exit(1);
    }
    return data_offset;
}


int follow_indirect(FILE* fp, 
                superblock *sb, 
                uint32_t zone_num, 
                long partition_offset,
                size_t *count,
                int capacity,
                uint32_t **zones) {
    debug("follow_indirect called\n");
    if (zone_num == 0) {
        return 0;
    }
    seek_to_zone(fp, sb, zone_num, partition_offset);
    /* The number of indirect blocks we have */
    int num_ptrs = sb->blocksize / sizeof(uint32_t);
    /* create a buffer to hold them */
    uint32_t *buf = (uint32_t *)calloc(num_ptrs, sizeof(uint32_t));
    if (buf == NULL) {
        perror("calloc");
        return 1;
    }
    /* read in the indriect zones to the buffer */
    if (fread(buf, sizeof(uint32_t), num_ptrs, fp) != (size_t)num_ptrs) {
        perror("fread");
        free(buf);
        return 1;
    }
    debug("made it past read\n");
    /* Gather non-zero zone references into 'zones'. */
    int i;
    for (i = 0; i < num_ptrs; i++) {
        if (buf[i] != 0) {
            debug("trying to ensure capacity\n");
            ensure_capacity((*count) + 1, capacity, *zones);
            debug("past ensuring capacity\n");
            if (*zones == NULL) {
                free(buf);
                return 1;
            }
            debug("zoninging it mark im zoning it so hard\n");
            debug("value of count is %d\n", *count);
            (*zones)[(*count)++] = buf[i];
            debug("past freakbob\n");
        }
    }

    free(buf);
    if (DEBUG) {
        printf("made it past follow_indirect");
        fflush(NULL);
    }
    return 0;
}   


int collect_zones(FILE *fp,
                superblock *sb,
                const inode *in,
                long partition_offset,
                uint32_t **out_zones,
                int *out_count) {
                    
    size_t capacity = 32;
    size_t count = 0;
    uint32_t *zones = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    if (zones == NULL) {
        perror("malloc\n");
        return 1;
    }

    /* go overr all the direct zones. */
    int i;
    for (i = 0; i < 7; i++) {
        if (in->zone[i] != 0) {
            ensure_capacity(count + 1, capacity, zones);
            if (zones == NULL) { 
                /* Realloc fail check */
                return 1;
            }
            zones[count++] = in->zone[i];
        }
    }
    if (DEBUG) {
        printf("count after just the direct zones %d\n", count);
        fflush(NULL);
    }

    if (in->indirect != 0) {
        if (follow_indirect(fp, 
                            sb, 
                            in->indirect, 
                            partition_offset, 
                            &count,
                            capacity,
                            &zones) != 0) {
            free(zones);
            return 1;
        }
    }

    
    if (in->two_indirect != 0) {
        seek_to_zone(fp, sb, in->two_indirect, partition_offset);

        /* Creat a buffer to hold the indirect blocks*/
        size_t num_ptrs = sb->blocksize / sizeof(uint32_t);
        uint32_t *buf = (uint32_t *)calloc(num_ptrs, sizeof(uint32_t));
        if (!buf) {
            perror("calloc");
            free(zones);
            return 1;
        }

        if (fread(buf, sizeof(uint32_t), num_ptrs, fp) != num_ptrs) {
            perror("fread");
            free(buf);
            free(zones);
            return 1;
        }

        /* For each zone reference in this block, 
        read that single-indirect block. */
        size_t j;
        for (j = 0; j < num_ptrs; j++) {
            if (buf[j] != 0) {
                if (follow_indirect(fp, sb, buf[j], 
                    partition_offset, 
                    &count,
                    capacity,
                    &zones) != 0) {
                    free(buf);
                    free(zones);
                    return 1;
                }
            }
        }
        free(buf);
    }
    *out_zones = zones;
    *out_count = count;
    return 0;
}


int find_inode_of_path(FILE *fp, 
                    superblock *sb, 
                    char *path, 
                    long partition_offset) {
    if (DEBUG) {
        printf("Find inode of path called\n");
        printf("Path in fiop: %s\n", path);
        fflush(NULL);
    }
    int curr_inode = 1; /* Root is inode = 1*/
    if (strcmp(path, "/") == 0) {
        /* if we are just / then we are root*/
        return curr_inode;
    }
    /* remove leading /*/
    char *p = path;
    if (*p == '/') {
        p++;
    }
    
    /* Create a buffer to hold file names */
    char name[MAX_NAME_LENGTH+1];
    /* start processing the path */
    while (*p) {
        /* while we have a path... */
        size_t index = 0;
        /* while path is not empty, 
        we haven't reached a /, 
        and our name isn't too long, copy into name */
        while (*p && *p != '/' && index < MAX_NAME_LENGTH) {
            name[index] = *p;
            p++;
            index++;
        }
        /* null terminate our name */
        name[index] = '\0';
        inode *in = malloc(sizeof(inode));
        /* read the current inode info */
        read_inode(fp, sb, curr_inode, in, partition_offset);

        if ((in->mode & FILE_TYPE) != DIRECTORY) {
            /* Only deal with directories for now */
            fprintf(stderr, "Name %s is not a directory\n", name);
            return -1;
        }
        
        /* clear following slashes */
        while(*p == '/') {
            p++;
        }

        int found = 0;
        uint32_t *zones = NULL;
        int zone_count = 0;
        if (collect_zones(fp, sb, in, partition_offset, &zones, &zone_count)) {
            perror("collect_zones\n");
            free(in);
            return -1;
        }
        if (DEBUG) {
            printf("Zone count %d\n", zone_count);
            fflush(NULL);
        }
        int i;
        for (i = 0; i < zone_count; i++){
            uint32_t zone_num = zones[i];
            
            if (zone_num == 0) {
                /* we have no zones to read */
                break;
            }
            /* get the zone size as shown in the spec */
            long zone_size = (long)sb->blocksize << sb->log_zone_size;
            /* get where our "head" should be
            that is where we are reading bytes */
            /*int zone_index = (int)(zone_num - sb->firstdata);*/
            if (zone_num < sb->firstdata) {
                /* if our "head" is before where the data is
                something went wrong */
                continue;
            }
            long data_offset = seek_to_zone(fp, sb, zone_num, partition_offset);
            /* the total number of entries we could have */
            int max_entries = (int)(zone_size / DIR_SIZE);
            int j;
            for (j = 0; j < max_entries; j++) {
                dir_entry *entry = malloc(sizeof(dir_entry));
                fread(entry, sizeof(dir_entry), 1, fp);
                if (entry->inode == 0) {
                    /* deleted or invalid */
                    continue;
                }
                /* compare the name of this entry 
                to what we are looking for */
                char entry_name[MAX_NAME_LENGTH+1];
                memcpy(entry_name, entry->name, MAX_NAME_LENGTH);
                entry_name[MAX_NAME_LENGTH] = '\0';
                /* only compare up to 60 chaaracters, maybe it already
                was terminated */
                if (strncmp(entry_name, name, MAX_NAME_LENGTH) == 0) {
                    /* yay we found it*/
                    curr_inode = entry->inode;
                    found = 1;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "Path entry %s not found\n", name);
            return -1;
        }
    }
    return curr_inode;
}


