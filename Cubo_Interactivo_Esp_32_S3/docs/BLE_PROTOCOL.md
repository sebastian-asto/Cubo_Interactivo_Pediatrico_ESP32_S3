# BLE Cubo-ESP — protocolo V4 (V1.5.1)

Servicio `72a10000-6c75-426f-9000-6375626f0001`; características Comando, Datos y Estado terminadas en `0001`, `0002` y `0003`. Los writes usan respuesta y enlace cifrado Just Works. MTU preferido: 247.

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

Datos contiene offset LE32 más el bloque. Imagen TFT1: 115208 bytes, 240×240 RGB565 big-endian. Audio canónico: RIFF/WAV PCM, mono, 8 bits sin signo, 16000 Hz y máximo 15 s (240044 bytes). Las publicaciones usan `.new` y `.bak` para recuperarse tras una interrupción.

## Estado (16 bytes)

| Bytes | Significado |
|---|---|
| 0 | Versión = 4 |
| 1 | 0 OK, 1 BUSY, 2 ERROR |
| 2–3 | Secuencia LE16 |
| 4 | Operación |
| 5 | Última emoción mostrada |
| 6–9 | `esp_err_t` LE32 |
| 10–13 | Bytes recibidos LE32 |
| 14 | bit0 microSD/recursos listos; bit1 cifrado; bit2 ES8311 listo |
| 15 | Cantidad = 16 |

La app consulta primero `SHOW_IF_MATCH` o `AUDIO_IF_MATCH`; solo transfiere cuando falta el recurso o cambia el CRC. Así, tocar nuevamente “Mostrar” reutiliza los archivos persistidos en microSD.
