#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "fat32_driver.h"
#include "utils.h"

/* Affiche comment se servir du programme. */
void print_help() {
    printf("Syntaxe : fat_cli <disque FAT> <commande> <arguments>\n");
    printf("Commandes acceptées :\n");
    printf("* tree: affiche un arbre des dossiers et fichiers\n");
    printf("* ls <chemin>: liste les fichiers dans un dossier\n");
    printf("* cat <chemin>: affiche le contenu d'un fichier\n");
    printf("\n");
    printf("Exemple :\n");
    printf("fat_cli chemin/vers/le/disque.img ls /chemin/vers/dossier/\n");
}

/* Vous pouvez éventuellement utiliser une fonction auxiliaire pour 'tree' */

void log_tree(struct fat32_node *root, int depth) {
    const char *name = fat32_node_get_name(root);

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;

    if (fat32_node_is_dir(root)) {
        printf("%*c> %s\n", 3 * depth, ' ', name);

        struct fat32_node_list* children = fat32_node_get_children(root);
        struct fat32_node_list* tmp = children;

        while(tmp) {
          log_tree(tmp->node, depth + 1);
          tmp = tmp->next;
        }

        fat32_node_list_free(children);
    }
    else {
      printf("%*c- %s\n", 3 * depth, ' ', name);
    }
    return;
}

/* Gestion de la commande "tree". */
int tree(char* fat_disk) {
    struct fat32_driver *driver = fat32_driver_new(fat_disk);
    struct fat32_node *root = fat32_driver_get_root_dir(driver);

    log_tree(root, 0);

    fat32_node_free(root);
    fat32_driver_free(driver);
    return EXIT_SUCCESS;
}

/* Gestion de la commande "ls". */
int ls(char* fat_disk, char* path) {
    struct fat32_driver *driver = fat32_driver_new(fat_disk);
    struct fat32_node *root = fat32_driver_get_root_dir(driver);

    struct fat32_node *node = fat32_node_get_path(root, path);

    if (node == NULL || !fat32_node_is_dir(node)) {
      fprintf(stderr, "is not a directory\n");
      return EXIT_FAILURE;
    }

    struct fat32_node_list* children = fat32_node_get_children(node);
    struct fat32_node_list* tmp = children;

    while (tmp) {
      printf("%s\n", fat32_node_get_name(tmp->node));
      tmp = tmp->next;
    }

    fat32_node_list_free(children);
    fat32_node_free(node);
    fat32_node_free(root);
    fat32_driver_free(driver);

    return EXIT_SUCCESS;
}

/* Gestion de la commande "cat". */
int cat(char* fat_disk, char* path) {
    struct fat32_driver *driver = fat32_driver_new(fat_disk);
    struct fat32_node *root = fat32_driver_get_root_dir(driver);
    struct fat32_node *node = fat32_node_get_path(root, path);

    if (node == NULL || fat32_node_is_dir(node)) {
      fprintf(stderr, "does not exist or is a directory\n");
      return EXIT_FAILURE;
    }
    
    fat32_node_read_to_fd(node, stdout);

    fat32_node_free(node);
    fat32_node_free(root);
    fat32_driver_free(driver);

    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    if (argc == 3 && strcmp(argv[2], "tree") == 0) {
        /* Si la commande est "tree" */
        return tree(argv[1]);
    }
    else if (argc == 4 && strcmp(argv[2], "ls") == 0) {
        /* Si la commande est "ls" */
        return ls(argv[1], argv[3]);
    }
    else if (argc == 4 && strcmp(argv[2], "cat") == 0) {
        /* Si la commande est "cat" */
        return cat(argv[1], argv[3]);
    }
    else {
        /* Pas une commande connue, on affiche comment se servir du
         * programme. */
        print_help();
        return EXIT_FAILURE;
    }

}
