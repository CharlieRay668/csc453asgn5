

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
        return 1;
    }
    if (read_inode(fp, sb, path_inum, in, partition_offset)) {
        fprintf(stderr, "Failed to read inode %d\n", path_inum);
        return 1;
    }

    /* Check that the inode does represents a regular file*/
    if ((in->mode & FILE_TYPE) != REGULAR_FILE) {
        fprintf(stderr, "Error: %s is not a regular file.\n", path);
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
        return 1;
    }

    /* Compute the size of a zone (blocksize shifted by log_zone_size) */
    long zone_size = (long)sb->blocksize << sb->log_zone_size;

    /* Read and output file data zone by zone */
    uint32_t bytes_left = filesize;
    int i;
    for (i = 0; i < zone_count && bytes_left > 0; i++) {
        uint32_t zone_num = zones[i];
        long to_read = (bytes_left < zone_size) ? bytes_left : zone_size;

        if (zone_num == 0) {
            /* This is a hole: output zeros */
            char *zero_buf = calloc(1, to_read);
            if (zero_buf == NULL) {
                perror("calloc");
                break;
            }
            /* write the zeros */
            if (fwrite(zero_buf, 1, to_read, stdout) != (size_t)to_read) {
                perror("fwrite");
                free(zero_buf);
                break;
            }
            free(zero_buf);
        } else {
            /* go to the zone we're copying */
            if (seek_to_zone(fp, sb, zone_num, partition_offset) < 0) {
                fprintf(stderr, "Failed to seek to zone %u\n", zone_num);
                break;
            }
            /* create a buffer containing how
            many bytes we need to read in */
            char *buffer = malloc(to_read);
            if (buffer == NULL) {
                perror("malloc");
                break;
            }
            /* actually read them in */
            size_t read_bytes = fread(buffer, 1, to_read, fp);
            if (read_bytes != (size_t)to_read) {
                fprintf(stderr, "Failed to read zone data\n");
                free(buffer);
                break;
            }
            /* write them to the file */
            if (fwrite(buffer, 1, read_bytes, stdout) != read_bytes) {
                perror("fwrite");
                free(buffer);
                break;
            }
            free(buffer);
        }
        /* decrement by the amount of bytes we read in */
        bytes_left -= to_read;
    }

    free(zones);
    free(in);
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
    char *path = argv[optind];

    /* Open the MINIX disk image */
    FILE *fp = fopen(minixdisk, "rb");
    if (!fp) {
        perror("Unable to open image file");
        return 1;
    }

    /* Clean the input path */
    char cleaned_path[1024];
    clean_path(path, cleaned_path, sizeof(cleaned_path));

    long partition_offset = get_partition_offset(partition, 
        fp, subpart, verbose);
    if (partition_offset < 0) {
        perror("Unable to get partition offset");
        return 1;
    }
    
    /* Read the superblock */
    superblock *sb = malloc(sizeof(superblock));
    if (sb == NULL) {
        perror("malloc");
        fclose(fp);
        return 1;
    }
    if (read_superblock(fp, partition_offset, sb)) {
        free(sb);
        fclose(fp);
        return 1;
    }
    if (verbose) {
        print_superblock(sb);
    }

    /* Find the inode corresponding to the path */
    int path_inum = find_inode(fp, sb, cleaned_path, partition_offset, verbose);
    if (path_inum < 0) {
        fprintf(stderr, "Failed to find path: %s\n", cleaned_path);
        free(sb);
        fclose(fp);
        return 1;
    }

    /* Go get the file(s) */
    if (copy_file_info(fp, sb, path_inum, cleaned_path, partition_offset)) {
        free(sb);
        fclose(fp);
        return 1;
    }
    free(sb);
    fclose(fp);
    return 0;
}
