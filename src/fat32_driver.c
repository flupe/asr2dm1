#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "utils.h"
#include "fat32_driver.h"

#define LFN_ENTRY_SIZE 32
#define LFN_ENTRY_CHARACTERS 13
#define ENTRY_SIZE 32

struct fat32_node {
    const struct fat32_driver *driver;
    uint32_t first_cluster;
    uint32_t offset;
    char name[256 * 4];
    uint32_t nb_lfn_entries;
    bool is_root;
};

struct fat32_driver {
    FILE *fd;

    // cf: http://wiki.osdev.org/FAT
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t nb_reserved_sectors;
    uint8_t nb_fats; // generally 2
    uint16_t nb_directories_at_root;
    uint32_t nb_sectors;
    uint32_t first_cluster_root;
    uint32_t sectors_per_fat;
};


struct fat32_driver* fat32_driver_new(const char *image_name) {
    FILE *fd = fopen(image_name, "r");

    struct fat32_driver *driver = malloc(sizeof(struct fat32_driver));

    // ensuring memory has been allocated
    assert(driver);

    driver->fd = fd;

    fseek(fd, 11, SEEK_SET);
    driver->bytes_per_sector = read_uint16_littleendian(fd); // 11
    driver->sectors_per_cluster = read_uint8(fd); // 13
    driver->nb_reserved_sectors = read_uint16_littleendian(fd); // 14
    driver->nb_fats = read_uint8(fd); // 16
    driver->nb_directories_at_root = read_uint16_littleendian(fd); // 17

    fseek(fd, 32, SEEK_SET);
    driver->nb_sectors = read_uint32_littleendian(fd); // 32
    driver->sectors_per_fat = read_uint32_littleendian(fd); // 36

    fseek(fd, 50, SEEK_SET);
    driver->first_cluster_root = read_uint16_littleendian(fd); // 50

#ifdef DEBUG
    fprintf(stderr, "Bytes per sector: %d\n", driver->bytes_per_sector);
    fprintf(stderr, "Sectors per cluster: %d\n", driver->sectors_per_cluster);
    fprintf(stderr, "Number of reserved sectors: %d\n", driver->nb_reserved_sectors);
    fprintf(stderr, "Number of FATs: %d\n", driver->nb_fats);
    fprintf(stderr, "Number of sectors: %d\n", driver->nb_sectors);
    fprintf(stderr, "Index of first cluster of root dir: %d\n", driver->first_cluster_root);
    fprintf(stderr, "Sectors per FAT: %d\n", driver->sectors_per_fat);
#endif

    assert(driver->nb_directories_at_root == 0); /* Always 0 for FAT32. */

    return driver;
}

void fat32_driver_free(struct fat32_driver *driver) {
    // ensuring the pointer is not NULL
    assert(driver);
    fclose(driver->fd);
    free(driver);
    driver = NULL;
}


uint32_t next_cluster_index(const struct fat32_driver *driver, uint32_t cluster_index) {
    assert(0); // TODO: complete
}

uint32_t get_cluster_sector(const struct fat32_driver *driver, uint32_t cluster_index) {
    assert(0); // TODO: complete
}

void read_in_cluster(const struct fat32_driver *driver, uint32_t cluster, uint32_t offset, size_t size, uint8_t *buf) {
    assert(offset+size <= driver->bytes_per_sector*driver->sectors_per_cluster);

    assert(0); // TODO: complete
}

/* Lit une partie de l'entrée correspondant au nœud 'node'.
 * À partir de l'octet indiqué par 'offset', lit 'size' octets,
 * et les écrit dans le tampon 'buf'.
 *
 * Ne fonctionne pas pour le nœud racine. */
void read_node_entry(const struct fat32_node *node, uint32_t offset, size_t size, uint8_t *buf) {
    assert(!node->is_root);

    uint32_t cluster = node->first_cluster;
    uint32_t bytes_per_cluster = (uint32_t) node->driver->sectors_per_cluster*node->driver->bytes_per_sector;
    uint32_t current_offset = node->offset + offset;

    assert(0); // TODO: remplacez-moi (première partie)
    assert(0); // TODO: remplacez-moi (deuxième partie)
}

/* Lit le nom d'un nœud et les éventuelles entrées LFN (Long File Name)
 * et définit les champs 'name' et 'nb_lfn_entries' du nœud.
 *
 * Ne fonctionne pas pour le nœud racine. */
void read_name(struct fat32_node *node) {
    assert(!node->is_root);

    uint8_t attributes;
    read_node_entry(node, 11, 1, &attributes);

    if ((attributes & 0xf) == 0xf) { /* Long File Name */
        bool entries_found[20];
        memset(entries_found, false, 20*sizeof(bool));

        /* Un "long file name" a au plus 256 caractères, chacun codé sur
         * deux octets. */
        char utf16_name[256*2] = {0};

        /* On lit chacune des entrées LFN, qui est au format décrit ici :
         * http://wiki.osdev.org/index.php?title=FAT&oldid=19960#Long_File_Names */
        for (unsigned int i=0; i<64; i++) {
            uint8_t lfn_entry[32];
            read_node_entry(node, i*32, 32, (uint8_t*) &lfn_entry);

            /* Les six bits de poids faible du premier octet est l'indice
             * de cette entrée LFN */
            uint8_t entry_index = (uint8_t) ((lfn_entry[0] & 0x3f) - 1);

            entries_found[entry_index] = true;

            /* Puis, 5 caractères du nom (chaque caractère est sur deux octets). */
            memcpy(&utf16_name[(entry_index*LFN_ENTRY_CHARACTERS+0)*2], &lfn_entry[1], 5*2);

            /* Puis trois octets qu'on ignore */

            /* Puis 6 caractères du nom */
            memcpy(&utf16_name[(entry_index*LFN_ENTRY_CHARACTERS+5)*2], &lfn_entry[14], 6*2);

            /* Puis un octet qu'on ignore */

            /* Puis 2 caractères du nom */
            memcpy(&utf16_name[(entry_index*LFN_ENTRY_CHARACTERS+11)*2], &lfn_entry[28], 2*2);

            if (entry_index == 0) {
                /* C'est la dernière entrée LFN */
                node->nb_lfn_entries = i+1;

                /* Check there is no missing part. */
                for (uint8_t j=0; j<=i; j++) {
                    assert(entries_found[j]);
                }


                char *utf8_name = utf16_to_utf8(utf16_name, 256*2);
                strcpy(node->name, utf8_name);
                free(utf8_name);

                break;
            }
        }
    }
    else { /* Short file name */
        assert(0); // TODO: complete

        node->nb_lfn_entries = 0;
    }
}

/* Récupère le nom d'un nœud.
 *
 * Ne fonctionne pas pour le nœud racine. */
const char* fat32_node_get_name(const struct fat32_node *node) {
    if (node->is_root) {
        return "<root>";
    }
    else {
        return node->name;
    }
}

/* Récupère l'octet d'attributs d'un nœud.
 *
 * Ne fonctionne pas pour le nœud racine. */
uint8_t fat32_node_get_attributes(const struct fat32_node *node) {
    uint8_t attributes;

    read_node_entry(node, 11+(node->nb_lfn_entries*LFN_ENTRY_SIZE), 1, &attributes);

    return attributes;
}

/* Renvoie true si et seulement si le nœud est un dossier. */
bool fat32_node_is_dir(const struct fat32_node *node) {
    if (node->is_root) {
        return true;
    }
    else {
        assert(0); // TODO: remplacez-moi
    }
}

struct fat32_node_list* read_dir_list(const struct fat32_driver *driver, uint32_t first_cluster) {
    uint32_t size_read = 0;
    struct fat32_node_list *list = NULL, *list_tmp;

    /* TODO: currently, this constructs the list in reverse order.
     * Make it in the right order? */
    while (true) {
        struct fat32_node *subdir = malloc(sizeof(struct fat32_node));
        assert(subdir); /* On s'assure que la mémoire a bien été allouée. */

        subdir->first_cluster = first_cluster;
        subdir->offset = size_read;
        subdir->driver = driver;
        subdir->is_root = false;

        uint8_t first_byte;
        read_node_entry(subdir, 0, 1, &first_byte);
        if (first_byte == 0xE5) {
            /* This entry is unused eg. deleted file. Skip to next entry. */
            memset(&subdir->name, 0, 12);
            read_node_entry(subdir, 1, 11, (uint8_t*) &subdir->name);
            fat32_node_free(subdir);
            size_read += ENTRY_SIZE;
            continue;
        }

        read_name(subdir);
        if (subdir->name[0] == '\0' && subdir->name[1] == '\0') {
            /* There is no more directory in this list. */
            fat32_node_free(subdir);
            return list;
        }

        list_tmp = list;
        list = malloc(sizeof(struct fat32_node_list));
        assert(list);
        list->node = subdir;
        list->next = list_tmp;

        size_read += ENTRY_SIZE + subdir->nb_lfn_entries*LFN_ENTRY_SIZE;
    }
}

struct fat32_node* fat32_driver_get_root_dir(const struct fat32_driver *driver) {
    struct fat32_node *root = malloc(sizeof(struct fat32_node));
    assert(root);
    root->driver = driver;
    root->is_root = true;
    return root;
}

uint32_t get_content_cluster(const struct fat32_node *node) {
    assert(0); // TODO: remplacez-moi
}

struct fat32_node_list* fat32_node_get_children(const struct fat32_node *node) {
    if (node->is_root) {
        return read_dir_list(node->driver, node->driver->first_cluster_root);
    }
    else {
        assert(fat32_node_is_dir(node));
        uint32_t content_cluster = get_content_cluster(node);
        return read_dir_list(node->driver, content_cluster);
    }
}

struct fat32_node* fat32_node_get_path(const struct fat32_node *node, const char *path) {
    assert(0); // TODO: remplacez-moi
}

void fat32_node_read_to_fd(const struct fat32_node *node, FILE *fd) {
    uint32_t first_content_cluster = get_content_cluster(node);

    uint32_t content_size; assert(0); // TODO: remplacez-moi

    uint32_t buffer_size = (uint32_t) (node->driver->bytes_per_sector*node->driver->sectors_per_cluster);
    char buffer[buffer_size];

    uint32_t current_cluster = first_content_cluster;
    while (content_size) {
        assert(current_cluster != 0); /* Check we do not run out of the chain before we finish reading the file. */

        /* Compute the address of the first sector of the current cluster. */
        uint32_t sector = get_cluster_sector(node->driver, current_cluster);
        uint32_t sector_address = sector*node->driver->bytes_per_sector;

        uint32_t read_size = (content_size<buffer_size) ? content_size : buffer_size;

        fseek(node->driver->fd, sector_address, SEEK_SET);
        fread(buffer, read_size, 1, node->driver->fd);

        fwrite(buffer, read_size, 1, fd);

        current_cluster = next_cluster_index(node->driver, current_cluster);
        content_size = (uint32_t) (content_size - read_size);
    }
}

void fat32_node_free(struct fat32_node *node) {
    free(node);
}


void fat32_node_list_free(struct fat32_node_list *list) {
    struct fat32_node_list *list_tmp;
    while (list) {
        fat32_node_free(list->node);
        list_tmp = list->next;
        free(list);
        list = list_tmp;
    }
}