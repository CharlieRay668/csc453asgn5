#include <stdio.h>
#include "minutil.h"

/* Get the premission string of a file */
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

/* Helper to output minget and minls usage */
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

/* Prints out the superblock info for verbosity */
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

void print_inode(inode *in) {
    fprintf(stderr, "Inode Contents:\n");
    fprintf(stderr, "  mode           = 0x%04x\n", in->mode);
    fprintf(stderr, "  links          = %u\n", in->links);
    fprintf(stderr, "  uid            = %u\n", in->uid);
    fprintf(stderr, "  gid            = %u\n", in->gid);
    fprintf(stderr, "  size           = %u\n", in->size);
    fprintf(stderr, "  atime          = %d\n", in->atime);
    fprintf(stderr, "  mtime          = %d\n", in->mtime);
    fprintf(stderr, "  ctime          = %d\n", in->ctime);

    fprintf(stderr, "  zone:\n");
    int i;
    for (i = 0; i < DIRECT_ZONES; i++) {
        fprintf(stderr, "    zone[%d]       = %u\n", i, in->zone[i]);
    }

    fprintf(stderr, "  indirect       = %u\n", in->indirect);
    fprintf(stderr, "  two_indirect   = %u\n", in->two_indirect);
}

void print_partition_table(partition_table *pt) {
    fprintf(stderr, "Partition Table Contents:\n");
    fprintf(stderr, "  bootind        = 0x%02x\n", pt->bootind);
    fprintf(stderr, "  startd_head    = %u\n", pt->startd_head);
    fprintf(stderr, "  start_sec      = %u\n", pt->start_sec);
    fprintf(stderr, "  start_cyl      = %u\n", pt->start_cyl);
    fprintf(stderr, "  type           = 0x%02x\n", pt->type);
    fprintf(stderr, "  end_head       = %u\n", pt->end_head);
    fprintf(stderr, "  end_sec        = %u\n", pt->end_sec);
    fprintf(stderr, "  end_cyl        = %u\n", pt->end_cyl);
    fprintf(stderr, "  lFirst         = %u\n", pt->lFirst);
    fprintf(stderr, "  size           = %u\n\n", pt->size);
}


/* Computes the offsets into the partition/subpartions
of the filesystem. Finds where the different tables and bitmap
and data starts. */
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

/* Cleans a path by removing excess slashes, etc. */
void clean_path(char *in, char *out, size_t outsize) {
    /* If we don't have a path, 
    use / as default path */
    if (!in || !*in) {
        strncpy(out, "/", outsize);
        out[outsize - 1] = '\0';
        return;
    }
    /* Deliminate on slashes */
    char delim[] = "/";
    char *token;
    char *ptr = out;
    size_t written = 0;

    *ptr++ = '/';
    written++;
    /* use strtok to handle most of it */
    token = strtok(in, delim);
    while(token != NULL){
        size_t len = strlen(token);
        /* set a limit on the size of the path */
        if (written + len + 1 >= outsize) {
            perror("Path longer than 1024 characters\n");
            out[written] = '\0';
            break;
        }
        /* save the path info. */
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
    unsigned char valid_bits[2];
    /* move to the validation bits at 510 & 511 */
    if(fseek(fp, CHECK_BIT_ADDR, SEEK_SET)){
        perror("Failed to seek to signature bits\n");
        fflush(NULL);
    }
    /* Read the two validation bits in*/
    if(fread(valid_bits, 1, 2, fp) != 2){
        perror("Failed to read signature bits\n");
        fflush(NULL);
    }
    /* check if they're valid */
    if (valid_bits[0] != VALID_BIT_ONE || valid_bits[1] != VALID_BIT_TWO) {
        perror("Invalid signature bits");
        return 1;
    }
    /* Make sure we're a MINIX partiton... */
    if (table->type != PARTITION_TYPE){
        perror("Invalid partition table type");
        return 1;
    }
    return 0;
}


long compute_partition_offset(const partition_table *pentry) {
    /*lFirst is in sectors and each sector is 512B*/
    return (long)pentry->lFirst * SECTOR_SIZE;
}

/* reads in the superblock at a given partition start */
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

/* Reads the inode of a block */
int read_inode(FILE *fp, 
            superblock *sb, 
            int inode_num, 
            inode *in, 
            long partition_offset){
    long inode_bitmap_offset, 
        zone_bitmap_offset, 
        inode_table_offset, 
        data_offset;
    /* get the offsets needed for this file */
    compute_fs_offsets(sb, 
                    &inode_bitmap_offset,
                    &zone_bitmap_offset, 
                    &inode_table_offset, 
                    &data_offset);

    /* if we are asking for node 0
    or more than what the superblock says we hav 
    fail */          
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

/* Helper function to make sure our list of zones
can actually hold all of the information */
int ensure_capacity(size_t needed, size_t *capacity, uint32_t **zones) {
    /* If we don't need to expand, dont*/
    if (needed <= *capacity) {
        return 0;
    }
    /* otherwise double */
    while (*capacity < needed) {
        *capacity *= 2;
    }
    /* realloc to copy over the information*/
    uint32_t *tmp = realloc(*zones, *capacity * sizeof(uint32_t));
    if (tmp == NULL) {
        /* if realloc failed, free it and fail*/
        free(*zones);
        *zones = NULL;
        return 1;
    }
    /* swap */
    *zones = tmp;
    return 0;
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

/* follow a indirect link to get more zones */
int follow_indirect(FILE* fp, 
                superblock *sb, 
                uint32_t zone_num, 
                long partition_offset,
                size_t *count,
                size_t *capacity,
                uint32_t **zones) {
    /* The number of indirect blocks we have */
    int num_ptrs = sb->blocksize / sizeof(uint32_t);
    /* if we get a 0, its probably a hole*/
    if (zone_num == 0) {
        int i;
        /* loop over all the number of blocks */
        for (i = 0; i < num_ptrs; i++) {
            if (ensure_capacity((*count) + 1, capacity, zones)) {
                return 1;
            }
            /* add a zero*/
            (*zones)[(*count)++] = 0;
        }
        return 0;
    }
    /* go to the next zone */
    seek_to_zone(fp, sb, zone_num, partition_offset);
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
    /* Gather non-zero zone references into 'zones'. */
    int i;
    for (i = 0; i < num_ptrs; i++) {
        if (ensure_capacity((*count) + 1, capacity, zones)) {
            free(buf);
            return 1;
        }
        (*zones)[(*count)++] = buf[i];
    }

    free(buf);
    return 0;
}   

/* Gather all the zones together, so we can accurelty
copy/list them */
int collect_zones(FILE *fp,
                  superblock *sb,
                  const inode *in,
                  long partition_offset,
                  uint32_t **out_zones,
                  int *out_count) {
    /* We start with enough room to hold 32 zones */
    size_t capacity = 32;
    size_t count = 0;
    uint32_t *zones = malloc(capacity * sizeof(uint32_t));
    if (zones == NULL) {
        perror("malloc");
        return 1;
    }

    /* Compute zone size and number of zones needed from file size */
    long zone_size = (long)sb->blocksize << sb->log_zone_size;
    uint32_t total_zones_needed = (in->size + zone_size - 1) / zone_size;

    /* First 7 are the direct zones */
    uint32_t direct_zones = 7;
    unsigned int i;
    for (i = 0; i < direct_zones && count < total_zones_needed; i++) {
        if (ensure_capacity(count + 1, &capacity, &zones)) {
            free(zones);
            return 1;
        }
        zones[count++] = in->zone[i];
    }

    /* If the file requires more than 7 zones, process the indirect block.
       Even if in->indirect is 0 call follow_indirect so that it
       adds a full indirect block’s worth of zeros. */
    uint32_t num_indirect = sb->blocksize / sizeof(uint32_t);
    if (total_zones_needed > direct_zones) {
        if (follow_indirect(fp, 
                            sb, in->indirect, 
                            partition_offset, 
                            &count, &capacity, &zones)) {
            free(zones);
            return 1;
        }
    }

    /* Process double-indirect block if needed */
    if (total_zones_needed > (direct_zones + num_indirect)) {
        /* Read the double indirect block */
        seek_to_zone(fp, sb, in->two_indirect, partition_offset);
        uint32_t *buf = calloc(num_indirect, sizeof(uint32_t));
        if (buf == NULL) {
            perror("calloc");
            free(zones);
            return 1;
        }
        if (fread(buf, sizeof(uint32_t), num_indirect, fp) 
                != (size_t)num_indirect) {
            perror("fread");
            free(buf);
            free(zones);
            return 1;
        }
        /* For each pointer in the double-indirect block, 
            always call follow_indirect */
        unsigned int j;
        for (j = 0; j < num_indirect && count < total_zones_needed; j++) {
            if (follow_indirect(fp, sb, buf[j], 
                    partition_offset, &count, &capacity, &zones)) {
                free(buf);
                free(zones);
                return 1;
            }
        }
        free(buf);
    }

    *out_zones = zones;
    *out_count = count;
    return 0;
}


/* find the inode of a file */
int find_inode(FILE *fp, 
                    superblock *sb, 
                    char *path, 
                    long partition_offset,
                    int verbose) {
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

        if(verbose){
            print_inode(in);
        }

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
        /* get all the zones corresponding to the file */
        if (collect_zones(fp, sb, in, partition_offset, &zones, &zone_count)) {
            perror("collect_zones\n");
            free(in);
            return -1;
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
            if (data_offset < 0) {
                continue;
            }
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

/* Handle the command line arguments
for minls and minget */
int process_args(int argc, 
                char *argv[], 
                int *verbose, 
                int *partition, 
                int *subpart, 
                int *arg_index) {
    /* Handle user command line arguments */
    int opt;
    while ((opt = getopt(argc, argv, "hvp:s:")) != -1) {
        switch (opt) {
            case 'h':
                usage(argv[0]);
                return 1;
            case 'v':
                printf("Option -v\n");
                *verbose = 1;
                break;
            case 'p':
                *partition = atoi(optarg);
                if (*verbose) {
                    printf("Option -p with value '%d'\n", *partition);
                }   
                break;
            case 's':
                *subpart = atoi(optarg);
                if(*verbose){
                    printf("Option -s with value '%d'\n", *subpart);
                }
                break;
            case '?':
                usage(argv[0]);
                return 1;
        }
    }
    *arg_index = optind;
    return 0;
}

/* calculate the partition offset for
a given partition and subpartition index */
long get_partition_offset(int partition,
                            FILE *fp,
                            int subpart,
                            int verbose) {
    /* if we have a valid partition to grab (non-negative) */
    long partition_offset = 0;
    if (partition >= 0) {
        partition_table head[4];
        /* read the tables in */
        if(read_partition_table(fp, 0, head) != 0) {
            perror("unable to read partitoin tables");
            return -1;
        }
        if(verbose){
            print_partition_table(head);
        }
        /* We only support 4 partitions, */
        if (partition < 0 || partition > 3) {
            perror("Invalid partition index");
            return -1;
        }
        partition_offset = compute_partition_offset(&head[partition]);
        /* handle sub partitions */
        if (subpart >= 0) {
            partition_table subs[4];
            if (read_partition_table(fp, partition_offset, subs) != 0) {
                return -1;
            }
            /* again we support only 4 subpartitoins*/
            if (subpart < 0 || subpart > 3) {
                fprintf(stderr, "Invalid subpartition index %d\n", subpart);
                return -1;
            }
            partition_offset = compute_partition_offset(&subs[subpart]);
        }
    }
    return partition_offset;
}

