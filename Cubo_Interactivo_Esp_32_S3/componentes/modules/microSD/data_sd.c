#include "data_sd.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bus_sdmmc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define SD_PATH_SIZE 128
static const char *TAG = "MICRO_SD";
static sdmmc_card_t *s_card;

static esp_err_t make_path(const char *relative, char path[SD_PATH_SIZE])
{
    if (!relative || !relative[0] || relative[0] == '/' ||
        strchr(relative, '\\') || strchr(relative, ':') || strstr(relative, "..")) {
        return ESP_ERR_INVALID_ARG;
    }
    int n = snprintf(path, SD_PATH_SIZE, "%s/%s", DATA_SD_MOUNT_POINT, relative);
    return n > 0 && n < SD_PATH_SIZE ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t data_sd_mount(void)
{
    if (s_card) {
        return ESP_OK;
    }
    sdmmc_host_t host;
    sdmmc_slot_config_t slot;
    esp_err_t err = bus_sdmmc_configure(&host, &slot);
    if (err != ESP_OK) {
        return err;
    }
    const esp_vfs_fat_sdmmc_mount_config_t config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    err = esp_vfs_fat_sdmmc_mount(DATA_SD_MOUNT_POINT, &host, &slot, &config, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        ESP_LOGE(TAG, "No se pudo montar FAT/SDMMC: %s. No se formatea la tarjeta.",
                 esp_err_to_name(err));
        return err;
    }
    sdmmc_card_print_info(stdout, s_card);
    const uint64_t capacity = (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
    ESP_LOGI(TAG, "SD_LISTA: %llu MiB, SDMMC 4 bits, CLK=16 CMD=15 D0=17 D1=18 D2=13 D3=14",
             (unsigned long long)(capacity / (1024 * 1024)));
    return ESP_OK;
}

esp_err_t data_sd_unmount(void)
{
    if (!s_card) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(DATA_SD_MOUNT_POINT, s_card);
    if (err == ESP_OK) {
        s_card = NULL;
    }
    return err;
}

esp_err_t data_sd_mkdir(const char *relative_dir)
{
    if (!s_card) {
        return ESP_ERR_INVALID_STATE;
    }
    char path[SD_PATH_SIZE];
    esp_err_t err = make_path(relative_dir, path);
    if (err != ESP_OK) {
        return err;
    }
    if (mkdir(path, 0775) == 0) {
        return ESP_OK;
    }
    struct stat st;
    return errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode)
        ? ESP_OK : ESP_FAIL;
}

esp_err_t data_sd_write_if_absent(const char *relative_path, const uint8_t *data,
                                size_t size, bool *created)
{
    if (created) {
        *created = false;
    }
    if (!s_card) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[SD_PATH_SIZE];
    esp_err_t err = make_path(relative_path, path);
    if (err != ESP_OK) {
        return err;
    }
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISREG(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    if (errno != ENOENT) {
        return ESP_FAIL;
    }

    /* Nombres 8.3, compatibles con FATFS_LFN_NONE. Nunca pisa temporales. */
    char temp[SD_PATH_SIZE];
    const char *slash = strrchr(path, '/');
    int directory_len = (int)(slash - path);
    int fd = -1;
    for (unsigned i = 0; i < 100; ++i) {
        int n = snprintf(temp, sizeof(temp), "%.*s/S%07u.TMP", directory_len, path, i);
        if (n < 0 || n >= sizeof(temp)) {
            return ESP_ERR_INVALID_SIZE;
        }
        fd = open(temp, O_WRONLY | O_CREAT | O_EXCL, 0664);
        if (fd >= 0 || errno != EEXIST) {
            break;
        }
    }
    if (fd < 0) {
        ESP_LOGE(TAG, "Crear temporal %s: errno=%d", temp, errno);
        return ESP_FAIL;
    }

    /* El origen puede estar en flash mapeada: nunca darlo directamente al DMA. */
    uint8_t *staging = heap_caps_malloc(4096, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!staging) {
        close(fd);
        unlink(temp);
        return ESP_ERR_NO_MEM;
    }
    size_t written = 0;
    bool ok = true;
    while (written < size) {
        const size_t chunk = size - written > 4096 ? 4096 : size - written;
        memcpy(staging, data + written, chunk);
        ssize_t n = write(fd, staging, chunk);
        if (n <= 0) {
            ESP_LOGE(TAG, "Escribir %s offset=%u: errno=%d", temp, (unsigned)written, errno);
            ok = false;
            break;
        }
        written += (size_t)n;
    }
    if (ok && fsync(fd) != 0) {
        ESP_LOGE(TAG, "Sincronizar %s: errno=%d", temp, errno);
        ok = false;
    }
    if (close(fd) != 0) {
        ok = false;
    }
    /* Verificar todos los bytes leidos de la tarjeta antes de publicar el archivo. */
    if (ok) {
        /* Read directly into the DMA-capable buffer; avoid stdio allocating an
         * intermediate buffer in PSRAM while Bluetooth is active. */
        int verify = open(temp, O_RDONLY);
        if (verify < 0) {
            ok = false;
        } else {
            for (size_t offset = 0; ok && offset < size;) {
                size_t chunk = size - offset > 4096 ? 4096 : size - offset;
                ssize_t n = read(verify, staging, chunk);
                if (n != (ssize_t)chunk || memcmp(staging, data + offset, chunk) != 0) {
                    size_t diff = 0;
                    if (n > 0) while (diff < (size_t)n && staging[diff] == data[offset + diff]) ++diff;
                    ESP_LOGE(TAG, "Verificacion SD fallo: %s offset=%u leidos=%d diferencia=%u errno=%d",
                             temp, (unsigned)offset, (int)n, (unsigned)diff, errno);
                    ok = false;
                }
                offset += chunk;
            }
            if (close(verify) != 0) ok = false;
        }
    }

    free(staging);
    /* Solo esta tarea escribe: conservar cualquier destino existente. */
    if (ok && stat(path, &st) == 0) {
        unlink(temp);
        return S_ISREG(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    if (ok && rename(temp, path) == 0) {
        if (created) {
            *created = true;
        }
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Publicar %s -> %s: verificado=%d errno=%d", temp, path, ok, errno);
    unlink(temp); /* Solo el temporal creado por esta llamada. */
    return ESP_FAIL;
}

esp_err_t data_sd_read_file(const char *relative_path, size_t max_size,
                          uint8_t **data, size_t *size)
{
    if (!data || !size || max_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *data = NULL;
    *size = 0;
    if (!s_card) {
        return ESP_ERR_INVALID_STATE;
    }
    char path[SD_PATH_SIZE];
    esp_err_t err = make_path(relative_path, path);
    if (err != ESP_OK) {
        return err;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    if (!S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > max_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return ESP_FAIL;
    }
    size_t length = (size_t)st.st_size;
    uint8_t *buffer = heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        buffer = heap_caps_malloc(length, MALLOC_CAP_8BIT);
    }
    if (!buffer) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    size_t read_bytes = fread(buffer, 1, length, file);
    int extra = fgetc(file);
    bool ok = read_bytes == length && extra == EOF && !ferror(file);
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        free(buffer);
        return ESP_FAIL;
    }
    *data = buffer;
    *size = length;
    return ESP_OK;
}

static esp_err_t backup_file_with_extension(const char *relative_path, const char *suffix)
{
    if (!s_card) {
        return ESP_ERR_INVALID_STATE;
    }
    char path[SD_PATH_SIZE];
    esp_err_t err = make_path(relative_path, path);
    if (err != ESP_OK) {
        return err;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return errno == ENOENT ? ESP_OK : ESP_FAIL;
    }
    if (!S_ISREG(st.st_mode)) {
        return ESP_ERR_INVALID_ARG;
    }
    char backup[SD_PATH_SIZE];
    memcpy(backup, path, strlen(path) + 1);
    char *extension = strrchr(backup, '.');
    if (!extension || strlen(extension) != 4) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(extension, suffix, strlen(suffix) + 1);
    if (stat(backup, &st) == 0 || errno != ENOENT) {
        return ESP_FAIL;
    }
    if (rename(path, backup) != 0) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Version previa preservada en %s", backup);
    return ESP_OK;
}

esp_err_t data_sd_backup_file(const char *relative_path)
{
    return backup_file_with_extension(relative_path, ".bad");
}

esp_err_t data_sd_replace_factory_file(const char *relative_path,
                                     const uint8_t *data, size_t size,
                                     const char *backup_suffix)
{
    if (!data || size == 0 || !backup_suffix || strlen(backup_suffix) != 3 ||
        backup_suffix[0] != '.' || backup_suffix[1] != 'v' ||
        backup_suffix[2] < '1' || backup_suffix[2] > '9') {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = backup_file_with_extension(relative_path, backup_suffix);
    if (err != ESP_OK) {
        return err;
    }
    bool created = false;
    err = data_sd_write_if_absent(relative_path, data, size, &created);
    if (err == ESP_OK && created) {
        ESP_LOGI(TAG, "SD_ACTUALIZADA: %s", relative_path);
        return ESP_OK;
    }
    /* La publicacion FAT no sobrescribe destinos; restaurar solo si falta. */
    char path[SD_PATH_SIZE];
    if (make_path(relative_path, path) == ESP_OK) {
        struct stat st;
        if (stat(path, &st) != 0 && errno == ENOENT) {
            char backup[SD_PATH_SIZE];
            memcpy(backup, path, strlen(path) + 1);
            char *extension = strrchr(backup, '.');
            if (extension) {
                memcpy(extension, backup_suffix, 4);
                if (rename(backup, path) != 0) {
                    ESP_LOGE(TAG, "La version anterior sigue respaldada en %s", backup);
                }
            }
        }
    }
    return err == ESP_OK ? ESP_FAIL : err;
}

/* FAT rename cannot overwrite. Keep .bak until .new has become the destination.
 * Recovery is per image, not an atomic transaction across all 6 images. */
static esp_err_t transaction_paths(const char *relative, char *path, char *pending, char *backup)
{
    esp_err_t err = make_path(relative, path);
    if (err != ESP_OK) return err;
    strcpy(pending, path);
    strcpy(backup, path);
    char *p = strrchr(pending, '.');
    char *b = strrchr(backup, '.');
    if (!p || strlen(p) != 4) return ESP_ERR_INVALID_ARG;
    strcpy(p, ".new");
    strcpy(b, ".bak");
    return ESP_OK;
}

esp_err_t data_sd_recover(const char *relative)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    char path[SD_PATH_SIZE], pending[SD_PATH_SIZE], backup[SD_PATH_SIZE];
    esp_err_t err = transaction_paths(relative, path, pending, backup);
    if (err != ESP_OK) return err;
    struct stat st;
    if (stat(path, &st) != 0) {
        if (errno != ENOENT) return ESP_FAIL;
        if (stat(backup, &st) == 0 && rename(backup, path) != 0) return ESP_FAIL;
    } else if (unlink(backup) != 0 && errno != ENOENT) return ESP_FAIL;
    if (unlink(pending) != 0 && errno != ENOENT) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t data_sd_replace(const char *relative, const uint8_t *data, size_t size)
{
    esp_err_t err = data_sd_recover(relative);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Recuperar %s: errno=%d", relative, errno); return err; }
    char path[SD_PATH_SIZE], pending[SD_PATH_SIZE], backup[SD_PATH_SIZE];
    err = transaction_paths(relative, path, pending, backup);
    if (err != ESP_OK) return err;
    bool created = false;
    err = data_sd_write_if_absent(pending + strlen(DATA_SD_MOUNT_POINT) + 1, data, size, &created);
    if (err != ESP_OK || !created) { ESP_LOGE(TAG, "Preparar %s: %s created=%d", pending, esp_err_to_name(err), created); return err == ESP_OK ? ESP_FAIL : err; }
    struct stat st;
    if (stat(path, &st) == 0) {
        if (rename(path, backup) != 0) { ESP_LOGE(TAG, "Respaldar %s: errno=%d", path, errno); return ESP_FAIL; }
    } else if (errno != ENOENT) return ESP_FAIL;
    if (rename(pending, path) != 0) {
        ESP_LOGE(TAG, "Publicar %s: errno=%d", pending, errno);
        rename(backup, path);
        return ESP_FAIL;
    }
    if (unlink(backup) != 0 && errno != ENOENT) { ESP_LOGE(TAG, "Retirar respaldo %s: errno=%d", backup, errno); return ESP_FAIL; }
    ESP_LOGI(TAG, "SD_REEMPLAZADA: %s (%u bytes verificados)", relative, (unsigned)size);
    return ESP_OK;
}

bool data_sd_file_exists(const char *relative)
{
    if (!s_card) return false;
    char path[SD_PATH_SIZE];
    if (make_path(relative, path) != ESP_OK) return false;
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

esp_err_t data_sd_delete_file(const char *relative)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    esp_err_t err = data_sd_recover(relative);
    if (err != ESP_OK) return err;
    char path[SD_PATH_SIZE], pending[SD_PATH_SIZE], backup[SD_PATH_SIZE];
    err = transaction_paths(relative, path, pending, backup);
    if (err != ESP_OK) return err;
    if (unlink(path) != 0 && errno != ENOENT) return ESP_FAIL;
    if (unlink(pending) != 0 && errno != ENOENT) return ESP_FAIL;
    if (unlink(backup) != 0 && errno != ENOENT) return ESP_FAIL;
    ESP_LOGI(TAG, "SD_ELIMINADO: %s", relative);
    return ESP_OK;
}
