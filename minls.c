#include "minutil.h"

void print_file_info(FILE *fp, 
                superblock *sb, 
                int inode_num,
                char *path, 
                long partition_offset){
    inode *in = malloc(sizeof(inode));
    
    /* read the inode_num into in */
    if(read_inode(fp, sb, inode_num, in, partition_offset)){
        fprintf(stderr, "failed to read_inode: %d\n", inode_num);
        free(in);
        return;
    }

    /* get the permissions in a printable form */
    char permissions[11];
    get_mode_string(in->mode, permissions);

    if((in->mode & FILE_TYPE) == DIRECTORY){
        printf("%s:\n", path);

        uint32_t *zones = NULL;
        int zone_count = 0;
        /* gather all direct, indirect and double 
            indirect zones into zones list */
        if (collect_zones(fp, sb, in, partition_offset, &zones, &zone_count)) {
            perror("collect_zones\n");
            free(in);
            return;
        }
        int i;
        /* loop through the zones and print the proper 
        information for each link in the directory*/
        for (i = 0; i < zone_count; i++){
            uint32_t zone_num = zones[i];
            if(zone_num == 0){
                continue;
            }
            /* calculate the zone size from
            given from the spec */
            long zone_size = (long)sb->blocksize << sb->log_zone_size;
            /* go to where the data is actually stored */
            long data_offset = seek_to_zone(fp, sb, zone_num, partition_offset);
            
            if(fseek(fp, data_offset, SEEK_SET)){
                perror("Failed to seek to data\n");
                return;
            }
            int entries_in_zone = (int)(zone_size / DIR_SIZE);

            int j;
            /* loop through all the entreis in the zone */
            for(j = 0; j < entries_in_zone; j++){
                long cur_pos = ftell(fp);
                /*if ftell fails*/
                if (cur_pos == -1L) {
                    perror("ftell");
                    return;
                }

                /* if we made it to the end of the directory */
                if(cur_pos - data_offset >= in->size){
                    break;
                }

                dir_entry *entry = malloc(sizeof(dir_entry));

                /* read in directory entry */
                if(fread(entry, sizeof(dir_entry), 1, fp) != 1){
                    perror("Couldn't read directory entry\n");
                    free(entry);
                    return;
                }

                /* if the entry was deleted */
                if(entry->inode == 0) {
                    continue;
                }

                /* read in inode from directory entry -- saving fp location*/
                inode *temp = malloc(sizeof(inode));
                long pointer_copy = ftell(fp);
                if(read_inode(fp, sb, entry->inode, temp, partition_offset)){
                    free(temp);
                    continue;
                }
                /* move fp back to original position */
                if(fseek(fp, pointer_copy, SEEK_SET)){
                    perror("failed to seek back to original spot\n");
                    return;
                }
                

                char perm2[11];
                /* get the permission string */
                get_mode_string(temp->mode, perm2);
                /* copy over the name */
                char namebuf[MAX_NAME_LENGTH + 1];
                memcpy(namebuf, entry->name, MAX_NAME_LENGTH);
                namebuf[MAX_NAME_LENGTH] = '\0';
                /* print out the file info */
                printf("%s %9u %s\n",
                    perm2,
                    temp->size,
                    namebuf
                );
            }
        }
    } else {
        /*increment path to ignore first / created by clean_path*/
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
    char *path = argv[optind];

    char cleaned_path[1024];
    clean_path(path, cleaned_path, sizeof(cleaned_path));

    /* Open disk image*/
    FILE *fp = fopen(minixdisk, "rb");
    if (!fp) {
        perror("Unable to open minis file\n");
        return 1;
    }
    /* calculate partition offset */
    long partition_offset = get_partition_offset(partition, 
                                        fp, subpart, verbose);
    if (partition_offset < 0) {
        perror("Unable to get partition offset");
        return 1;
    }

    superblock *sb = malloc(sizeof(superblock));
    /* read in the superblock */
    if (read_superblock(fp, partition_offset, sb)) {
        free(sb);
        fclose(fp);
        return 1;
    }
    if (verbose) {
        print_superblock(sb);
    }
    /* get the inode corresponding to where we're looking */
    int path_inum = find_inode(fp, sb, cleaned_path, partition_offset, verbose);
    if (path_inum < 0) {
        fprintf(stderr, "Failed to find path: %s\n", cleaned_path);
        free(sb);
        fclose(fp);
        return 1;
    }
    /* print out the info. */
    print_file_info(fp, sb, path_inum, cleaned_path, partition_offset);

    free(sb);
    fclose(fp);
    return 0;
}