#include "minutil.h"

void print_file_info(FILE *fp, 
                superblock *sb, 
                int inode_num,
                char *path, 
                long partition_offset){
    if (DEBUG) {
        printf("Print file info called\n");
        printf("Path in pfi: %s\n", path);
        fflush(NULL);
    }
    inode *in = malloc(sizeof(inode));
    
    /* read the inode_num into in */
    if(read_inode(fp, sb, inode_num, in, partition_offset)){
        fprintf(stderr, "failed to read_inode: %d\n", inode_num);
        free(in);
        return;
    }

    /*get the permissions in a printable form*/
    char permissions[11];
    get_mode_string(in->mode, permissions);

    if((in->mode & FILE_TYPE) == DIRECTORY){
        printf("%s:\n", path);

        uint32_t *zones = NULL;
        int zone_count = 0;
        if (collect_zones(fp, sb, in, partition_offset, &zones, &zone_count)) {
            perror("collect_zones\n");
            free(in);
            return 1;
        }
        if (DEBUG) {
            printf("Zone count %d\n", zone_count);
            fflush(NULL);
        }
        int i;
        for (i = 0; i < zone_count; i++){
            uint32_t zone_num = zones[i];


            if(zone_num == 0){
                break;
            }
            /* Maybe flip these arrows, i forget...*/
            long zone_size = (long)sb->blocksize << sb->log_zone_size;
            debug("Zone size is %ld\n", zone_size);
            long data_offset = seek_to_zone(fp, sb, zone_num, partition_offset);
            
            if(fseek(fp, data_offset, SEEK_SET)){
                perror("Failed to seek to data\n");
                return;
            }
            
            int entries_in_zone = (int)(zone_size / DIR_SIZE);
            if (DEBUG) {
                printf("entreis in zone %d\n", entries_in_zone);
                fflush(NULL);
            }

            int j;
            for(j = 0; j < entries_in_zone; j++){
                long cur_pos = ftell(fp);
                /*if ftell fails*/
                if (cur_pos == -1L) {
                    perror("ftell");
                    return;
                }

                if(DEBUG){
                    printf("Cur_pos: %d, data_offset: %d\n", 
                        
                        cur_pos, 
                        data_offset);
                    fflush(NULL);
                }

                /* if we made it to the end of the directory */
                if(cur_pos - data_offset >= in->size){
                    debug("made it to end of directory while printing\n");
                    debug("in->size: %u\n", in->size);
                    break;
                }

                dir_entry *entry = malloc(sizeof(dir_entry));

                if(fread(entry, sizeof(dir_entry), 1, fp) != 1){
                    perror("Couldn't read directory entry\n");
                    free(entry);
                    return;
                }

                /* if the entry was deleted */
                if(entry->inode == 0) {
                    debug("Inode was 0\n");
                    continue;
                }

                inode *temp = malloc(sizeof(inode));
                long pointer_copy = ftell(fp);
                if(read_inode(fp, sb, entry->inode, temp, partition_offset)){
                    debug("print inode failed\n");
                    free(temp);
                    continue;
                }
                /* move fp back to original position */
                if(fseek(fp, pointer_copy, SEEK_SET)){
                    perror("failed to seek back to original spot\n");
                    return;
                }
                

                char perm2[11];
                get_mode_string(temp->mode, perm2);


                char namebuf[MAX_NAME_LENGTH + 1];
                memcpy(namebuf, entry->name, MAX_NAME_LENGTH);
                namebuf[MAX_NAME_LENGTH] = '\0';

                printf("%s %9u %s\n",
                    perm2,
                    temp->size,
                    namebuf
                );
            }
        }
    } else {
        if (DEBUG) {
            printf("Permissions: %d\n", permissions);
            fflush(NULL);
        }
        path++;
        printf("%s %9u %s\n", permissions, in->size, path);
    }
}

int main(int argc, char *argv[]) {
    int verbose = 0;
    int partition = -1;
    int subpart = -1;
    int optind = -1;

    /* Handle flags */
    if (process_args(argc, argv, &verbose, &partition, &subpart, &optind)) {
        return 1;
    }

    /* We only go options, we need arguments to run */
    if (optind >= argc) {
        fprintf(stderr, 
        "Usage: %s [ -v ] [ -p num [ -s num ] ] imagefile path\n", 
        argv[0]);
        return 1;
    }

    /* Grab the MINIX disk file */
    char *minixdisk = argv[optind];
    optind++;
    const char *path = argv[optind];

    char canonical[1024];
    canonicalize_path(path, canonical, sizeof(canonical));

    /* Open disk image*/
    FILE *fp = fopen(minixdisk, "rb");
    if (!fp) {
        perror("Unable to open minis file\n");
        return 1;
    }

    long partition_offset = get_partition_offset(partition, fp, subpart);

    superblock *sb = malloc(sizeof(superblock));

    if (read_superblock(fp, partition_offset, sb)) {
        free(sb);
        close(fp);
        return 1;
    }
    if (verbose) {
        print_superblock(sb);
    }

    int path_inum = find_inode_of_path(fp, sb, canonical, partition_offset);
    if (path_inum < 0) {
        fprintf(stderr, "Failed to find path: %s\n", canonical);
        free(sb);
        fclose(fp);
        return 1;
    }

    print_file_info(fp, sb, path_inum, canonical, partition_offset);

    free(sb);
    fclose(fp);
    return 0;
}