#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define DATA_SD_MOUNT_POINT "/sdcard"

/* Una sola tarea administra el montaje y las escrituras. Nunca autoformatea. */
esp_err_t data_sd_mount(void);
esp_err_t data_sd_unmount(void);
esp_err_t data_sd_mkdir(const char *relative_dir);
/* Unico escritor. Recupera una sustitucion interrumpida y publica con respaldo. */
esp_err_t data_sd_recover(const char *relative_path);
esp_err_t data_sd_replace(const char *relative_path, const uint8_t *data, size_t size);
/* Elimina un archivo de usuario y sus temporales transaccionales. Ausente = OK. */
esp_err_t data_sd_delete_file(const char *relative_path);
bool data_sd_file_exists(const char *relative_path);

/* Instala solo archivos ausentes; usa un temporal exclusivo y fsync. */
esp_err_t data_sd_write_if_absent(const char *relative_path, const uint8_t *data,
                                size_t size, bool *created);

/* Reserva un buffer (PSRAM preferida). El llamador debe liberarlo con free(). */
esp_err_t data_sd_read_file(const char *relative_path, size_t max_size,
                          uint8_t **data, size_t *size);

/* Renombra a .bad sin sobrescribir respaldos. El llamador valida previamente
 * que el archivo necesita recuperacion. No elimina su contenido. */
esp_err_t data_sd_backup_file(const char *relative_path);

/* Actualizacion autorizada por el llamador: preserva la version previa,
 * escribe y verifica la nueva; restaura la previa si la escritura falla. */
esp_err_t data_sd_replace_factory_file(const char *relative_path,
                                     const uint8_t *data, size_t size,
                                     const char *backup_suffix);
