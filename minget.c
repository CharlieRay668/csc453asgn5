

#include "minutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[]) {
    int opt;
    int verbose = 0;
    int partition = -1;
    int subpart = -1;

    /* Process command-line options */
    while ((opt = getopt(argc, argv, "hvp:s:")) != -1) {
        switch (opt) {
            case 'h':
                usage(argv[0]);
                return 0;
            case 'v':
                verbose = 1;
                break;
            case 'p':
                partition = atoi(optarg);
                break;
            case 's':
                subpart = atoi(optarg);
                break;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (optind + 1 >= argc) {
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

    long partition_offset = 0;
    if (partition >= 0) {
        partition_table head[4];
        if (read_partition_table(fp, 0, head) != 0) {
            fclose(fp);
            return 1;
        }
        if (partition < 0 || partition > 3) {
            fprintf(stderr, "Invalid partition index\n");
            fclose(fp);
            return 1;
        }
        partition_offset = compute_partition_offset(&head[partition]);
        if (subpart >= 0) {
            partition_table subs[4];
            if (read_partition_table(fp, partition_offset, subs) != 0) {
                fclose(fp);
                return 1;
            }
            if (subpart < 0 || subpart > 3) {
                fprintf(stderr, "Invalid subpartition index\n");
                fclose(fp);
                return 1;
            }
            partition_offset = compute_partition_offset(&subs[subpart]);
        }
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

    /* Find the inode corresponding to the canonical path */
    int inum = find_inode_of_path(fp, sb, canonical, partition_offset);
    if (inum < 0) {
        fprintf(stderr, "Failed to find path: %s\n", canonical);
        free(sb);
        fclose(fp);
        return 1;
    }

    /* Read the inode */
    inode *in = malloc(sizeof(inode));
    if (in == NULL) {
        perror("malloc");
        free(sb);
        fclose(fp);
        return 1;
    }
    if (read_inode(fp, sb, inum, in, partition_offset)) {
        fprintf(stderr, "Failed to read inode %d\n", inum);
        free(in);
        free(sb);
        fclose(fp);
        return 1;
    }

    /* Check that the inode does not represent a directory */
    if ((in->mode & FILE_TYPE) == DIRECTORY) {
        fprintf(stderr, "Error: %s is a directory.\n", canonical);
        free(in);
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
        free(in);
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
            free(buffer);
            break;
        }
        if (fwrite(buffer, 1, read_bytes, stdout) != read_bytes) {
            perror("fwrite");
            free(buffer);
            break;
        }
        free(buffer);
        bytes_left -= read_bytes;
    }

    free(zones);
    free(in);
    free(sb);
    fclose(fp);
    return 0;
}
