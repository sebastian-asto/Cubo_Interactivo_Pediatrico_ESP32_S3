# Firmware Cubo Interactivo ESP32-S3 — V1.7.1

Firmware ESP-IDF 5.5.4 para la placa ESP32-S3 con pantalla ST7789 de 240×240, lector microSD, codec ES8311 y amplificador NS4150B. Expone un periférico BLE llamado `Cubo-ESP` y ejecuta las órdenes de la aplicación móvil.

## Funciones implementadas

- Montaje y validación de la microSD.
- Catálogo estable de 16 emociones.
- Carga de imágenes TFT1 desde la tarjeta.
- Prioridad de recursos personalizados sobre predeterminados.
- Escritura recuperable de archivos del usuario.
- Recepción BLE fragmentada con offset, longitud y CRC32.
- Comparación previa para evitar transferencias repetidas.
- Metadatos persistentes `.crc` para comprobar tamaño y CRC sin releer el WAV.
- Reproducción WAV mediante ES8311 e I2S.
- Reproducción asíncrona y cancelable, sin bloquear la respuesta BLE.
- Activación del NS4150B mediante `PA_CTRL`.
- Volumen regulable de 0 a 100 % y persistente en NVS.
- Restauración de imágenes sin modificar los audios.
- Eliminación independiente de audio por emoción.
- Cifrado BLE Just Works y claves persistidas en NVS.
- Exportación BLE cifrada de imágenes personalizadas por bloques.
- Mapas de 16 bits para consultar imágenes de usuario y audios existentes.
- Encendido enclavado y apagado por botón mediante KEY_PWR/GPIO5 y BAT_EN/GPIO2.

## Estructura relevante

```text
Cubo_Interactivo_Esp_32_S3/
├── components/
│   ├── driver/
│   │   ├── i2c_manager/    # Bus de control del codec
│   │   ├── i2s_manager/    # Flujo PCM hacia el codec
│   │   └── spi_manager/    # TFT y microSD
│   └── modules/
│       ├── st7789/          # Pantalla y framebuffer
│       ├── microSD/         # FAT, lectura y publicación transaccional
│       └── audio/           # ES8311 local, WAV y volumen
├── main/
│   ├── app/
│   │   ├── ble/             # Servicio y protocolo BLE V5
│   │   ├── emotions/        # Catálogo y resolución de recursos
│   │   └── power_button/   # KEY_PWR, BAT_EN y apagado por pulsación
│   ├── assets/              # Solo recursos de respaldo independientes
│   └── main.c
├── docs/BLE_PROTOCOL.md
├── tests/pixel_studio.test.mjs
├── tools/
│   ├── generate_emotions.py
│   ├── idf.ps1
│   └── monitor_boot.py
└── sdkconfig.defaults
```

## Secuencia de arranque

1. Enclava la alimentación poniendo BAT_EN/GPIO2 en HIGH y configura KEY_PWR/GPIO5 por interrupción.
2. Inicializa la TFT y su framebuffer.
3. Monta la microSD.
4. Crea, recupera o valida la estructura `Doc_microSD`.
5. Valida las 16 imágenes predeterminadas.
6. Recupera publicaciones `.new` o `.bak` interrumpidas.
7. Muestra Feliz cuando todos los recursos están disponibles.
8. Configura I2S, I2C, ES8311 y NS4150B.
9. Inicializa NVS y recupera el volumen guardado.
10. Inicia NimBLE y anuncia el servicio `Cubo-ESP`.

Si falta una imagen predeterminada, el firmware no anuncia la microSD como lista. Así la app evita habilitar operaciones que producirían una pantalla incompleta.

## Encendido y apagado: KEY_PWR y BAT_EN

- GPIO5 lee `KEY_PWR`, activo en LOW y con interrupción por flanco de bajada.
- Se utiliza el pull-up externo R14 de 10 kΩ a 3,3 V; el pull-up interno queda deshabilitado.
- Al presionar el botón, D5 baja temporalmente la compuerta de Q2 y permite arrancar desde VBAT.
- Al comenzar el firmware, GPIO2 pone `BAT_EN` en HIGH; T1 mantiene la compuerta de Q2 baja después de soltar el botón.
- La tarea `power_button` valida que una nueva pulsación permanezca LOW durante al menos 50 ms.
- Una pulsación válida lleva `BAT_EN` a LOW. El botón todavía sostiene Q2 mientras está presionado y el devkit se apaga al soltarlo.
- El siguiente encendido no es una alternancia ejecutada por software: el botón vuelve a alimentar la placa, el ESP32-S3 reinicia y GPIO2 se enclava otra vez en HIGH.
- Si existe alimentación USB/VBUS, la placa puede seguir encendida aunque `BAT_EN` esté LOW; esta señal controla la ruta de batería.

## Catálogo e imágenes

Orden interno:

```text
joy, sad, angry, upset, serious, sorry, excited, inlove,
nervous, thought, bored, surprise, worried, scared, crying, proud
```

Formato TFT1:

- cabecera de 8 bytes: `54 46 54 31 00 F0 00 F0`;
- 57.600 píxeles RGB565 big-endian;
- tamaño total: 115.208 bytes.

Resolución del archivo mostrado:

1. `Doc_microSD/user/images/<id>.tft` si existe y es válido;
2. `Doc_microSD/default/images/<id>.tft` en caso contrario.

Las imágenes de fábrica ya no se embeben en el firmware ni se copian a `user/images`.

## Escritura recuperable en microSD

Una personalización se publica así:

1. escribe y verifica `<archivo>.new`;
2. renombra el destino existente como `<archivo>.bak`;
3. publica `.new` con el nombre definitivo;
4. elimina `.bak` al confirmar el resultado.

Durante el arranque se recupera un respaldo si falta el archivo final y se retiran temporales incompletos. FAT no ofrece atomicidad absoluta frente a todos los cortes eléctricos, pero este procedimiento conserva el archivo anterior durante la publicación.

El soporte de nombres largos FAT está habilitado porque `Doc_microSD` supera el formato 8.3.

## Audio ES8311 y NS4150B

| Señal | GPIO |
|---|---:|
| I2S_MCLK | 8 |
| I2S_SCLK/BCLK | 9 |
| I2S_LRCK | 10 |
| I2S_DSDIN | 12 |
| CODEC_SDA | 42 |
| CODEC_SCL | 41 |
| PA_CTRL | 7 |

El audio no depende de un componente administrado externo. `i2c_manager` controla el bus de registros, `i2s_manager` controla el flujo PCM y `modules/audio/es8311.c` contiene la inicialización local del ES8311 para 16 kHz. Por ello el proyecto ya no genera la carpeta `managed_components` ni necesita `espressif/esp_codec_dev`.

La salida I2S se configura como estéreo de 16 bits; cada muestra del WAV mono de 8 bits se convierte a signed 16-bit y se duplica en ambos canales antes de enviarla al codec.

Formato aceptado por el firmware:

- RIFF/WAVE PCM;
- un canal;
- 8 bits sin signo;
- 16.000 Hz;
- máximo 180 segundos;
- máximo 2.880.044 bytes incluyendo cabecera.

Los audios viven en `Doc_microSD/user/audio/<id>.wav`. No hay audio predeterminado obligatorio: si el archivo no existe, la emoción se muestra en silencio.

Cada imagen o audio personalizado posee un archivo auxiliar `<id>.crc` de 12 bytes: firma `CRC1`, tamaño LE32 y CRC32 LE32. El metadato se publica transaccionalmente después del recurso. Para instalaciones anteriores, el firmware calcula el CRC una sola vez y crea el archivo auxiliar cuando la app consulta el recurso.

La reproducción se ejecuta en la tarea `cube_audio`, que abre el WAV y lo lee desde la microSD en bloques de 512 muestras. No carga el archivo completo en PSRAM para reproducirlo. Si llega una emoción nueva, la reproducción anterior se cancela como máximo al terminar el bloque I2S actual y cualquier sonido pendiente es reemplazado.

## Volumen

- Rango lógico: 0–100 %.
- Valor inicial cuando no existe configuración: 65 %.
- Aplicación inmediata mediante el driver local `es8311_set_volume`.
- Clave persistente: namespace `cube_cfg`, valor `volume`.
- `GET_VOLUME` permite que la app presente el valor real al conectar.

## BLE V5

- Servicio: `72a10000-6c75-426f-9000-6375626f0001`.
- Nombre anunciado: `Cubo-ESP`.
- Características de comando, datos y estado.
- Comando fijo de 12 bytes.
- Estado fijo de 16 bytes.
- MTU preferido: 517, con compatibilidad automática con centrales que negocien menos.
- Escrituras de hasta 512 bytes de atributo: 4 de offset y hasta 508 de recurso.
- Una conexión simultánea.
- Escrituras de datos con respuesta.
- Timeout de 60 segundos para cargas incompletas.
- Buffer de transferencia asignado en PSRAM.

Operaciones principales:

| Op | Operación |
|---:|---|
| 1 | Mostrar imagen activa y reproducir audio |
| 2–3 | Iniciar y confirmar imagen |
| 4 | Restablecer las 16 imágenes |
| 5 | Cancelar transferencia |
| 6–7 | Mostrar original o personalizada si coincide el CRC |
| 8–9 | Iniciar y confirmar audio |
| 10–11 | Eliminar o comparar audio |
| 12 | Ajustar y guardar volumen |
| 13 | Consultar volumen |
| 14 | Preparar descarga de imagen personalizada si difiere del CRC del teléfono |
| 15 | Consultar mapas de imágenes personalizadas y audios existentes |

La característica de datos es bidireccional desde V5: las escrituras suben recursos y las lecturas descargan bloques de imagen con offset LE32. Los detalles byte por byte se encuentran en [BLE_PROTOCOL.md](docs/BLE_PROTOCOL.md).

## Sincronización hacia otros teléfonos

`GET_RESOURCE_MAPS` recorre `user/images` y `user/audio` y devuelve dos mapas LE16, uno por tipo de recurso. Para cada bit de imagen activo, la app puede llamar a `PREPARE_IMAGE_DOWNLOAD` incluyendo el tamaño y CRC32 de su copia local.

El firmware lee y valida exclusivamente `Doc_microSD/user/images/<id>.tft`:

- si tamaño y CRC coinciden, responde con tamaño de descarga cero;
- si difieren, mantiene temporalmente los 115.208 bytes en PSRAM;
- cada lectura de Datos devuelve `offset LE32` y hasta 240 bytes de imagen, evitando fragmentar la respuesta sobre el ACL de 255 bytes del controlador;
- una escritura de exactamente 4 bytes en Datos durante la descarga reposiciona el cursor al offset LE32 solicitado;
- el recurso preparado se conserva hasta 60 segundos para permitir reintentos, incluso si se perdió la respuesta del último bloque;
- una cancelación o desconexión también libera el recurso preparado.

La característica Datos exige cifrado tanto al escribir como al leer. La descarga no modifica archivos, no cambia la TFT y no transfiere audio. El comando `SHOW` conserva la prioridad normal `user/images` → `default/images`, por lo que un segundo teléfono puede reproducir una emoción sin borrar la personalización existente.

### Caché de servicios en Android

En firmware anterior a BLE V5, Datos se anunciaba solo para escritura. Algunos teléfonos Android conservan esa tabla GATT aunque se desinstale la aplicación. La app V1.7.2+12 verifica al conectar que Datos tenga la propiedad de lectura; si encuentra la definición antigua, actualiza la caché del sistema y reconecta una vez. Esta es una corrección del cliente: el firmware compatible continúa siendo V1.7.1 y el protocolo permanece en V5.

La actualización de caché ocurre antes de preparar una descarga y no accede a la microSD. Si Android impide la actualización programática, se debe desvincular `Cubo-ESP` manualmente y alternar Bluetooth antes de reconectar.

## Concurrencia

Después del arranque, el callback GATT solo valida y copia bloques; no realiza operaciones FAT ni dibuja directamente. `cube_commands` administra comandos, publicaciones y pantalla, mientras `cube_audio` lee bloques pequeños del WAV y los envía a I2S/ES8311. FatFs serializa el acceso al volumen y el acceso al codec está protegido para permitir cambios de volumen durante la reproducción.

## Preparar la microSD

Desde la raíz general del repositorio puede copiarse `Doc_microSD` directamente. Para regenerar las imágenes, el manifiesto y el ZIP:

```powershell
python tools/generate_emotions.py
```

El script usa como fuente `../documents/images/emotions_v1.1` y actualiza:

- `../Doc_microSD/default/images`;
- `../app_control_cubo_interactivo/assets/emotions`;
- `../documents/images/emotions`;
- `../documents/microsd/Doc_microSD_v1.5.0.zip`.

## Compilar y grabar

```powershell
.\tools\idf.ps1 build
.\tools\idf.ps1 -p COM13 flash
```

Binario principal:

```text
build/Cubo_Interactivo_Esp_32_S3.bin
```

Para observar el arranque sin modificar la placa:

```powershell
python tools/monitor_boot.py --port COM13 --seconds 12 --output ../documents/logs/boot.log
```

## Verificación

```powershell
node tests/pixel_studio.test.mjs
.\tools\idf.ps1 build
```

La prueba Node valida sintaxis del editor, RGB565, formato TFT1, round-trip exacto, las 16 imágenes de microSD y la coincidencia del orden entre firmware y Flutter.

La compilación actual ocupa aproximadamente 801 KB y deja cerca del 85 % de la partición de aplicación disponible. Falta validar siempre en hardware la orientación de la TFT, la descarga BLE con distintos MTU, el nivel acústico, la calidad de la bocina y la estabilidad de la tarjeta utilizada.
