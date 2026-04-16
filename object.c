#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}


void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}


int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    // 1. Convert type to string
    const char *type_str;
    if (type == OBJ_BLOB) type_str = "blob";
    else if (type == OBJ_TREE) type_str = "tree";
    else type_str = "commit";

    // 2. Create header: "type size\0"
    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len) + 1;

    // 3. Allocate full object buffer
    size_t total_len = header_len + len;
    char *buffer = malloc(total_len);
    if (!buffer) return -1;

    memcpy(buffer, header, header_len);
    memcpy(buffer + header_len, data, len);

        // 4. Compute hash of full object
    compute_hash(buffer, total_len, id_out);

        // 5. Deduplication check
    if (object_exists(id_out)) {
        free(buffer);
        return 0;
    }

    // 6. Get object path
    char path[512];
    object_path(id_out, path, sizeof(path));

    // 7. Create shard directory
    char dir[512];
    strncpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }

        // 8. Create temp file path
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);

    // 9. Write to temp file
    int fd = open(temp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        free(buffer);
        return -1;
    }

    if (write(fd, buffer, total_len) != (ssize_t)total_len) {
        close(fd);
        free(buffer);
        return -1;
    }

    fsync(fd);
    close(fd);

    // 10. Atomic rename
    if (rename(temp_path, path) != 0) {
        free(buffer);
        return -1;
    }

    free(buffer);
    return 0;

    // TEMP return 
    free(buffer);
    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    rewind(fp);

    char *buffer = malloc(size);
    if (!buffer) {
        fclose(fp);
        return -1;
    }

    fread(buffer, 1, size, fp);
    fclose(fp);

    // Verify hash
    ObjectID computed;
    compute_hash(buffer, size, &computed);
    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        free(buffer);
        return -1;
    }

    // Parse header
    char *null_pos = memchr(buffer, '\0', size);
    if (!null_pos) {
        free(buffer);
        return -1;
    }

    // Extract type
    if (strncmp(buffer, "blob", 4) == 0) *type_out = OBJ_BLOB;
    else if (strncmp(buffer, "tree", 4) == 0) *type_out = OBJ_TREE;
    else *type_out = OBJ_COMMIT;

    // Extract size
    size_t data_len;
    sscanf(buffer, "%*s %zu", &data_len);

    // Extract data
    char *data_start = null_pos + 1;
    void *data = malloc(data_len);
    memcpy(data, data_start, data_len);

    *data_out = data;
    *len_out = data_len;

    free(buffer);
    return 0;
}
