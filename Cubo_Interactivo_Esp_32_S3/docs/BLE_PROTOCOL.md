# BLE Cubo-ESP — protocolo V5 (V1.7.1)

Servicio `72a10000-6c75-426f-9000-6375626f0001`; características Comando, Datos y Estado terminadas en `0001`, `0002` y `0003`. Los writes usan respuesta y enlace cifrado Just Works. MTU preferido: 517, con adaptación al valor negociado.

## Catálogo estable

Índices 0–15: `joy, sad, angry, upset, serious, sorry, excited, inlove, nervous, thought, bored, surprise, worried, scared, crying, proud`.

## Comando (12 bytes)

Byte 0 operación; bytes 1–2 secuencia LE16; byte 3 índice; bytes 4–7 tamaño LE32; bytes 8–11 CRC32 IEEE LE32.

| Op | Nombre | Acción |
|---:|---|---|
| 1 | SHOW | Muestra imagen activa y reproduce su audio si existe |
| 2 | BEGIN_IMAGE | Inicia carga TFT1 |
| 3 | COMMIT_IMAGE | Valida, guarda en `user/images`, muestra y reproduce |
| 4 | RESTORE_ALL_IMAGES | Elimina las 16 imágenes de usuario; conserva audios |
| 5 | ABORT | Cancela la carga abierta |
| 6 | RESTORE_SHOW | Elimina una imagen de usuario, muestra la predeterminada y reproduce audio |
| 7 | SHOW_IF_MATCH | Si el CRC de la imagen de usuario coincide, la muestra sin retransmitir |
| 8 | BEGIN_AUDIO | Inicia carga WAV |
| 9 | COMMIT_AUDIO | Valida y guarda WAV en `user/audio` |
| 10 | DELETE_AUDIO | Elimina el WAV del índice |
| 11 | AUDIO_IF_MATCH | Confirma tamaño y CRC del WAV guardado |
| 12 | SET_VOLUME | Ajusta 0–100 % usando el campo tamaño y lo conserva en NVS |
| 13 | GET_VOLUME | Devuelve el volumen actual en el byte 5 del Estado |
| 14 | PREPARE_IMAGE_DOWNLOAD | Compara tamaño/CRC local y prepara la imagen de usuario si difiere |
| 15 | GET_RESOURCE_MAPS | Devuelve mapas de 16 bits para imágenes personalizadas y audios |

Datos contiene offset LE32 más el bloque. Imagen TFT1: 115208 bytes, 240×240 RGB565 big-endian. Audio canónico: RIFF/WAV PCM, mono, 8 bits sin signo, 16000 Hz y máximo 180 s (2880044 bytes). Las publicaciones usan `.new` y `.bak` para recuperarse tras una interrupción.

La app limita el valor de cada escritura a `min(MTU-3, 512)`. Los primeros 4 bytes son el offset, por lo que un MTU 517 transporta hasta 508 bytes de recurso. Con MTU 247 transporta 240 bytes y con el mínimo 23 transporta 16 bytes; el formato lógico permanece igual.

Para descargar, la app ejecuta `PREPARE_IMAGE_DOWNLOAD` y lee repetidamente Datos. Cada lectura devuelve `offset LE32 + bloque TFT1`; el bloque útil se limita a 240 bytes para caber sin fragmentación en el ACL de 255 bytes del controlador. Tamaño cero indica que el CRC local ya coincide. Todas las lecturas de Datos requieren enlace cifrado.

Durante una descarga preparada, escribir exactamente `offset LE32` (4 bytes) en Datos reposiciona el cursor de lectura. La app usa esta operación antes de reintentar una lectura fallida o fuera de secuencia. El firmware conserva el buffer durante 60 segundos y solo lo reemplaza con otra preparación, lo cancela con `ABORT`, lo elimina al desconectarse o lo libera por inactividad.

## Estado (16 bytes)

| Bytes | Significado |
|---|---|
| 0 | Versión = 5 |
| 1 | 0 OK, 1 BUSY, 2 ERROR |
| 2–3 | Secuencia LE16 |
| 4 | Operación |
| 5 | Última emoción mostrada |
| 6–9 | `esp_err_t` LE32 |
| 10–13 | Bytes recibidos LE32 |
| 14 | bit0 microSD/recursos listos; bit1 cifrado; bit2 ES8311 listo |
| 15 | Cantidad = 16 |

En la respuesta de la operación 14, bytes 10–13 contienen el tamaño preparado para descarga. En la operación 15, bytes 10–11 contienen el mapa LE16 de imágenes de usuario y bytes 12–13 el mapa LE16 de audios existentes.

La app consulta primero `SHOW_IF_MATCH` o `AUDIO_IF_MATCH`; solo transfiere cuando falta el recurso o cambia el CRC. Así, tocar nuevamente “Mostrar” reutiliza los archivos persistidos en microSD.

El firmware guarda tamaño y CRC32 en un archivo auxiliar `.crc`. `AUDIO_IF_MATCH` usa ese metadato y evita releer el WAV completo en las selecciones posteriores. Si el metadato todavía no existe, se genera después de una validación completa compatible con tarjetas creadas por versiones anteriores.

Las operaciones que muestran una emoción responden cuando la TFT quedó actualizada y el WAV fue validado y encolado. La respuesta no espera a que finalice la reproducción. `cube_audio` lee el archivo desde la microSD en bloques de 512 muestras; una orden nueva sustituye el audio que se encuentre reproduciéndose o pendiente.
