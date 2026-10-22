# BLE Cubo-ESP — protocolo v1

Servidor: ESP-IDF 5.5.4/NimBLE, periférico de una conexión. Cliente: Flutter/`flutter_reactive_ble`. El teléfono busca el UUID de servicio, no depende de un nombre configurable.

| Atributo | UUID | Acceso |
|---|---|---|
| Servicio | `72a10000-6c75-426f-9000-6375626f0001` | Advertising |
| Comando | `72a10001-6c75-426f-9000-6375626f0001` | Write with response, cifrado |
| Datos | `72a10002-6c75-426f-9000-6375626f0001` | Write with response, cifrado |
| Estado | `72a10003-6c75-426f-9000-6375626f0001` | Read |

Nombre en scan response: `Cubo-ESP`. Servicio de 128 bits en advertising. MTU preferido 247; el cliente utiliza como máximo 244 bytes ATT y adapta el tamaño si la negociación falla. Cifrado Just Works con claves persistidas en NVS, sin PIN ni propiedad exclusiva: prototipo local, no control de acceso para producción.

## Índices estables (cero a quince)

`joy, sad, angry, upset, serious, sorry, excited, inlove, nervous, thought, bored, surprise, worried, scared, crying, proud`.

## Comando: exactamente 12 bytes

- byte 0: operación.
- bytes 1–2: número de petición, uint16 little-endian. El cliente incrementa por comando.
- byte 3: índice (0–15; 0 si no aplica).
- bytes 4–7: tamaño total uint32 little-endian, solo BEGIN.
- bytes 8–11: CRC32 uint32 little-endian, solo BEGIN.

| Op | Nombre | Resultado |
|---|---|---|
| 1 | SHOW | Lee el archivo actual de la SD y lo muestra |
| 2 | BEGIN | Descarta transferencia previa y reserva 115208 bytes en PSRAM |
| 3 | COMMIT | Comprueba longitud, CRC y TFT1; publica en SD y muestra |
| 4 | RESTORE_ALL | Restaura las 16 imágenes de flash y muestra Feliz |
| 5 | ABORT | Libera transferencia incompleta sin escribir SD |
| 6 | RESTORE_SHOW | Restaura una cara de fábrica y la muestra |

SHOW/RESTORE se rechazan si hay un upload abierto: COMMIT o ABORT primero. Un comando ocupado devuelve error ATT. La aceptación ATT indica solamente que se encoló el trabajo; hay que leer Estado hasta que coincidan petición/operación y resultado deje de ser BUSY.

## Datos y validación

Cada write de Datos: offset uint32 little-endian seguido de 1–508 bytes. Debe coincidir exactamente con la siguiente posición esperada; el cliente normal usa 240 bytes de datos con MTU 247. Nunca escribir sin respuesta. El buffer se cancela al desconectar o después de 60 segundos sin datos. Las conexiones tienen una generación interna para no aceptar comandos pendientes de una conexión ya cerrada.

CRC32 IEEE: polinomio reflejado 0xEDB88320, inicio 0xFFFFFFFF, complemento final. Vector `123456789` → 0xCBF43926.

El archivo es TFT1: `54 46 54 31 00 F0 00 F0`, seguido de 57600 píxeles RGB565 big-endian por filas (115208 bytes). El little-endian del protocolo de control no cambia el big-endian del formato TFT1.

## Estado: exactamente 16 bytes

| Bytes | Significado |
|---|---|
| 0 | Versión = 1 |
| 1 | 0 OK, 1 BUSY, 2 ERROR |
| 2–3 | Petición confirmada LE16 |
| 4 | Operación |
| 5 | Última emoción mostrada correctamente |
| 6–9 | `esp_err_t` LE32; 0 si éxito |
| 10–13 | Bytes de transferencia recibidos LE32 |
| 14 | Flags: bit 0 microSD montada; bit 1 enlace cifrado |
| 15 | Número de emociones = 16 |

El cliente espera el bit de enlace cifrado antes de habilitar Mostrar. Estado se consulta cada ~120 ms mientras un comando trabaja. Tiempo máximo cliente: 90 s. No hay notificaciones ni reanudación a mitad de transferencia: al reconectar se empieza otra BEGIN. La UI informa éxito solamente después de COMMIT/RESTORE_SHOW confirmado.

## Escritura y concurrencia

Una tarea `cube_commands` es la única dueña de SD/pantalla tras la inicialización. El host BLE solo valida, copia datos en PSRAM y encola comandos; no realiza IO de FAT. Se verifica el contenido completo de SD tras escribirlo usando el buffer DMA interno del módulo existente.

Sustitución por archivo: escribir/verificar `.new`; renombrar destino a `.bak`; publicar `.new` como `.tft`; retirar `.bak`. Al arrancar se restaura `.bak` si falta `.tft` y se retira `.new` incompleto. No se modifica la tabla de particiones ni se formatea la SD. FAT no garantiza atomicidad ante cualquier fallo eléctrico del medio; RESTORE_ALL no es una transacción atómica de 16 archivos. Si falla, se informa error y puede repetirse.

Mostrar un original que ya coincide no reescribe la SD. Reiniciar el cubo muestra Feliz (incluida su personalización si existe); no persiste el índice seleccionado. Si falta SD se ve el respaldo embebido y BLE informa `sd_ready=0`.

## Verificación

- `tools/idf.ps1 build`: firmware.
- `tools/idf.ps1 -p COM13 flash`: grabación autorizada.
- `tools/monitor_boot.py --port COM13 --seconds 12 --output output/ble_boot.log`: registro sin reiniciar.
- App: `flutter analyze`, `flutter test`.
- Prueba de hardware optativa: `flutter test integration_test/cube_ble_test.dart -d <telefono>`; restaura originales. Usar solo el cubo de desarrollo indicado en el test.
