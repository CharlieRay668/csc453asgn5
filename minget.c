

#include "minutil.h"


int copy_file_info(FILE *fp, 
                superblock *sb, 
                int path_inum, 
                char *path, 
                long partition_offset) {
    
    /* Read the inode */
    inode *in = malloc(sizeof(inode));
    if (in == NULL) {
        perror("malloc");
        if(DEBUG){
            printf("free b/c failed to malloc in\n");
            fflush(NULL);
        }
        free(sb);
        fclose(fp);
        return 1;
    }
    if (read_inode(fp, sb, path_inum, in, partition_offset)) {
        fprintf(stderr, "Failed to read inode %d\n", path_inum);
        if(DEBUG){
            printf("free b/c failed to read_inode\n");
            fflush(NULL);
        }
        free(in);
        if(DEBUG){
            printf("free b/c failed to read_inode\n");
            fflush(NULL);
        }
        free(sb);
        fclose(fp);
        return 1;
    }

    /* Check that the inode does not represent a directory */
    if ((in->mode & FILE_TYPE) == DIRECTORY) {
        fprintf(stderr, "Error: %s is a directory.\n", path);
        if(DEBUG){
            printf("free b/c inode is directory\n");
            fflush(NULL);
        }
        free(in);
        if(DEBUG){
            printf("free b/c inode is directory\n");
            fflush(NULL);
        }
        free(sb);
        fclose(fp);
        return 1;
    }

    /* Get the file size from the inode */
    uint32_t filesize = in->size;

    /* Gather all zones (direct, indirect, 
    and two-indirect) that contain file data */
    uint32_t *zones = NULL;
    int zone_count = 0;
    if (collect_zones(fp, sb, in, partition_offset, &zones, &zone_count)) {
        fprintf(stderr, "Failed to collect file zones\n");
        if(DEBUG){
            printf("free b/c failed to collect zones\n");
            fflush(NULL);
        }
        free(in);
        if(DEBUG){
            printf("free b/c failed to collect zones\n");
            fflush(NULL);
        }
        free(sb);
        fclose(fp);
        return 1;
    }

    /* Compute the size of a zone (blocksize shifted by log_zone_size) */
    long zone_size = (long)sb->blocksize << sb->log_zone_size;

    /* Read and output file data zone by zone */
    uint32_t bytes_left = filesize;
    int i;
    for (i = 0; i < zone_count && bytes_left > 0; i++) {
        uint32_t zone_num = zones[i];
        if (zone_num == 0)
            break;
        /* Seek to the start of the current zone */
        if (seek_to_zone(fp, sb, zone_num, partition_offset) < 0) {
            fprintf(stderr, "Failed to seek to zone %u\n", zone_num);
            break;
        }
        /* Determine how many bytes to read from this zone */
        long to_read = (bytes_left < zone_size) ? bytes_left : zone_size;
        char *buffer = malloc(to_read);
        if (buffer == NULL) {
            perror("malloc");
            break;
        }
        size_t read_bytes = fread(buffer, 1, to_read, fp);
        if (read_bytes != (size_t)to_read) {
            fprintf(stderr, "Failed to read zone data\n");
            if(DEBUG){
                printf("free b/c read wrong number of bytes\n");
                fflush(NULL);
            }
            free(buffer);
            break;
        }
        if (fwrite(buffer, 1, read_bytes, stdout) != read_bytes) {
            perror("fwrite");
            if(DEBUG){
                printf("free b/c wrote wrong number of bytes\n");
                fflush(NULL);
            }
            free(buffer);
            break;
        }
        if(DEBUG){
            printf("free b/c end of loop iter\n");
            fflush(NULL);
        }
        free(buffer);
        bytes_left -= read_bytes;
    }

    if(DEBUG){
        printf("free b/c end of function\n");
        fflush(NULL);
    }
    free(zones);
    if(DEBUG){
        printf("free b/c end of function\n");
        fflush(NULL);
    }
    free(in);
    if(DEBUG){
        printf("free b/c end of function\n");
        fflush(NULL);
    }
    free(sb);
    fclose(fp);
    return 0;
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

    if (optind >= argc) {
        fprintf(stderr, 
        "Usage: %s [ -v ] [ -p num [ -s num ] ] imagefile path\n", 
        argv[0]);
        return 1;
    }

    char *minixdisk = argv[optind++];
    const char *path = argv[optind];

    /* Open the MINIX disk image */
    FILE *fp = fopen(minixdisk, "rb");
    if (!fp) {
        perror("Unable to open image file");
        return 1;
    }

    /* Canonicalize the input path */
    char canonical[1024];
    canonicalize_path(path, canonical, sizeof(canonical));

    long partition_offset = get_partition_offset(partition, fp, subpart);
    

    /* Read the superblock */
    superblock *sb = malloc(sizeof(superblock));
    if (sb == NULL) {
        perror("malloc");
        fclose(fp);
        return 1;
    }
    if (read_superblock(fp, partition_offset, sb)) {
        if(DEBUG){
            printf("free b/c failed to read_super\n");
            fflush(NULL);
        }
        free(sb);
        fclose(fp);
        return 1;
    }
    if (verbose) {
        print_superblock(sb);
    }

    /* Find the inode corresponding to the canonical path */
    int path_inum = find_inode_of_path(fp, sb, canonical, partition_offset);
    if (path_inum < 0) {
        fprintf(stderr, "Failed to find path: %s\n", canonical);
        if(DEBUG){
            printf("free b/c failed to find inode\n");
            fflush(NULL);
        }
        free(sb);
        fclose(fp);
        return 1;
    }

    /* Extract out  into second function */
    if (copy_file_info(fp, sb, path_inum, canonical, partition_offset)) {
        return 1;
    }
    return 0;
}
