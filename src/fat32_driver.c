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

struct fat32_node* copy_node(const struct fat32_node *node) {
  struct fat32_node *clone = malloc(sizeof(struct fat32_node));
  clone->driver = node->driver;
  clone->first_cluster = node->first_cluster;
  clone->offset = node->offset;
  strcpy(clone->name, node->name);
  clone->nb_lfn_entries = node->nb_lfn_entries;
  clone->is_root = node->is_root;
  return clone;
}

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
    driver->nb_sectors = read_uint16_littleendian(fd);

    if (driver->nb_sectors == 0) {
      fseek(fd, 32, SEEK_SET);
      driver->nb_sectors = read_uint32_littleendian(fd); // 32
    }

    fseek(fd, 36, SEEK_SET);
    driver->sectors_per_fat = read_uint32_littleendian(fd); // 36

    fseek(fd, 44, SEEK_SET);
    driver->first_cluster_root = read_uint32_littleendian(fd); // 44

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
    assert(driver);
    fclose(driver->fd);
    free(driver);
    driver = NULL;
}


uint32_t next_cluster_index(const struct fat32_driver *driver, uint32_t cluster_index) {
    uint32_t pos = (uint32_t) (driver->nb_reserved_sectors * (uint32_t) driver->bytes_per_sector + cluster_index * 4);
    fseek(driver->fd, pos, SEEK_SET);
    return read_uint32_littleendian(driver->fd);
}

uint32_t get_cluster_sector(const struct fat32_driver *driver, uint32_t cluster_index) {
    return driver->nb_reserved_sectors + driver->sectors_per_fat * driver->nb_fats + (uint32_t) driver->sectors_per_cluster * (cluster_index - 2);
}

void read_in_cluster(const struct fat32_driver *driver, uint32_t cluster, uint32_t offset, size_t size, uint8_t *buf) {
    assert(offset+size <= driver->bytes_per_sector * driver->sectors_per_cluster);

    uint32_t pos = get_cluster_sector(driver, cluster) * driver->bytes_per_sector + offset;
    fseek(driver->fd, pos, SEEK_SET);
    fread(buf, 1, size, driver->fd);
}

/* Lit une partie de l'entrée correspondant au nœud 'node'.
 * À partir de l'octet indiqué par 'offset', lit 'size' octets,
 * et les écrit dans le tampon 'buf'.
 *
 * Ne fonctionne pas pour le nœud racine. */
void read_node_entry(const struct fat32_node *node, uint32_t offset, size_t size, uint8_t *buf) {
    assert(!node->is_root);

    uint32_t cluster = node->first_cluster;
    uint32_t bytes_per_cluster = (uint32_t) node->driver->sectors_per_cluster * node->driver->bytes_per_sector;
    uint32_t current_offset = node->offset + offset;

    while (current_offset >= bytes_per_cluster) {
      current_offset -= bytes_per_cluster;
      cluster = next_cluster_index(node->driver, cluster);
    }

    if (current_offset + size > bytes_per_cluster) {
      read_in_cluster(node->driver, cluster, current_offset, bytes_per_cluster - current_offset, buf);
      buf += bytes_per_cluster - current_offset;
      size += current_offset - bytes_per_cluster;
      cluster = next_cluster_index(node->driver, cluster);
      current_offset = 0;

      while (size > bytes_per_cluster) {
        read_in_cluster(node->driver, cluster, 0, bytes_per_cluster, buf);
        buf += bytes_per_cluster;
        size -= bytes_per_cluster;
        cluster  = next_cluster_index(node->driver, cluster);
      }
    }

    read_in_cluster(node->driver, cluster, current_offset, size, buf);
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
    // short file name
    else {
        uint8_t dump[11];

        read_node_entry(node, 0, 11, dump);

        size_t last_char = 7, next;
        while (last_char && dump[last_char] == ' ') last_char--;
        next = last_char + 1;
        memcpy(node->name, dump, next); 

        // find if there is an extension
        if (dump[8] != ' ') {
          node->name[next++] = '.';
          memcpy(node->name + next, dump + 8, 3);
          next += 3;
        }

        node->name[next] = '\0';
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
       uint8_t attributes = fat32_node_get_attributes(node);
       return attributes == 0x10;
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
    uint8_t bytes[4];
    read_node_entry(node, 20 + node->nb_lfn_entries*LFN_ENTRY_SIZE, 2, bytes + 2);
    read_node_entry(node, 26 + node->nb_lfn_entries * LFN_ENTRY_SIZE, 2, bytes);
    // not very pretty (we just assume the 2-bytes sequences are little-endian)
    return (uint32_t) (bytes[0] | bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24);
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
    assert(node);

    // get rid of leading /
    while (*path == '/') path++;
    if (*path == 0) return copy_node(node);
    const char *next = path + 1;
    while (*next != 0 && *next != '/') next++;

    struct fat32_node_list *list = fat32_node_get_children(node);
    struct fat32_node_list *tmp = list;

    const char *name;
    struct fat32_node *ret = NULL;
    size_t len;

    while (tmp) {
      name = fat32_node_get_name(tmp->node);
      len = (size_t) (next - path);

      if (strlen(name) == len && strncmp(name, path, len) == 0) {
        ret = fat32_node_get_path(tmp->node, next);
        break;
      }

      tmp = tmp->next;
    }

    fat32_node_list_free(list);

    return ret;
}

void fat32_node_read_to_fd(const struct fat32_node *node, FILE *fd) {
    uint32_t first_content_cluster = get_content_cluster(node);

    
    uint8_t buf[4];
    read_node_entry(node, 28 + node->nb_lfn_entries * LFN_ENTRY_SIZE, 4, buf);
    uint32_t content_size = (uint32_t) (buf[0] | buf[1] << 8 | buf[2] << 16 | buf[3] << 24);

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
