# Cubo Interactivo Pediátrico - ESP32-S3

La visión conjunta de firmware, app, conexiones, almacenamiento, transferencia BLE
y las 16 caras está en el [README general del proyecto](../README.md).
Este documento amplía los detalles del firmware y del editor HTML.

Firmware en C con ESP-IDF para la placa Waveshare ESP32-S3-LCD-1.54.
El proyecto implementa el manejo de la TFT ST7789 desde comandos SPI y un
framebuffer propio. Su objetivo es servir de base para el cubo interactivo y
su integración con la aplicación Flutter por BLE.

Actualmente permite rellenar la pantalla con colores, mostrar dibujos creados
en TFT Pixel Studio y leer 16 caras desde microSD. Las caras iniciales se incluyen
en el firmware para instalarlas en la tarjeta al primer arranque. Una carita
guardada en flash sirve de respaldo si la microSD no está disponible.

## Hardware y entorno

| Elemento | Configuración |
| --- | --- |
| Placa | Waveshare ESP32-S3-LCD-1.54 |
| Microcontrolador | ESP32-S3, target `esp32s3` |
| Framework utilizado | ESP-IDF 5.5.4 |
| Pantalla | ST7789, 240 x 240 píxeles |
| Formato de píxel del firmware | RGB565, 16 bits por píxel |
| Flash | 16 MB |
| PSRAM de la placa | 8 MB, modo octal y detección automática habilitados |
| Bus del LCD | SPI2, modo 0, sin MISO |
| Reloj SPI solicitado | 70 MHz, fuente `SPI_CLK_SRC_DEFAULT` |

La frecuencia SPI real depende de los divisores del periférico; el valor
solicitado no constituye una medición. El driver permite consultarla con
`spi_device_get_actual_freq()`. La frecuencia de CPU, la de la flash y la de
la TFT son configuraciones diferentes.

El tamaño físico de PSRAM detectado y el resultado de su prueba de memoria se
comprueban en el registro serie al arrancar.

### Pines de la TFT

| Señal | GPIO |
| --- | ---: |
| SCLK | 38 |
| MOSI / DIN | 39 |
| CS | 21 |
| RESET | 40 |
| DC, selección de datos/comandos | 45 |
| Backlight | 46 |

## Estructura

```text
Cubo_Interactivo_Pediatrico_ESP32_S3/
├── documents/
│   └── index.html                 # Editor local, fuera de la carpeta del firmware
└── Cubo_Interactivo_Esp_32_S3/
    ├── CMakeLists.txt
    ├── sdkconfig.defaults         # Configuración reproducible para Git
    ├── componentes/
    │   ├── driver/spi_manager/     # bus_spi (TFT) y bus_sdmmc (microSD)
    │   └── modules/
    │       ├── st7789/            # Controlador TFT y framebuffer
    │       └── microSD/           # Montaje FAT, lectura y escritura de archivos
    ├── main/
    │   ├── main.c                 # Arranque, instalación SD y servidor BLE
    │   ├── app/power_button/      # Archivos reservados para el botón
    │   ├── app/emotions/          # Catálogo, instalación, lectura y restauración
    │   ├── app/ble/               # Servidor NimBLE y tarea única de SD/TFT
    │   └── assets/
    │       ├── README.md
    │       └── drawing.tft        # Dibujo opcional incorporado al firmware
    ├── partitions/partitions.csv
    └── tests/pixel_studio.test.mjs
```

## Compilar y grabar

Abre una terminal con el entorno ESP-IDF 5.5.4 activado y sitúate en
`Cubo_Interactivo_Esp_32_S3`.

```sh
idf.py reconfigure build
idf.py -p COM13 flash monitor
```

Sustituye `COM13` por el puerto de tu placa. `build` genera el firmware;
`flash` lo escribe en la placa y `monitor` muestra el registro serie.
Para salir del monitor usa `Ctrl+]`.

### Conservar la configuración

La tabla personalizada, los 16 MB de flash y el modo octal de PSRAM están
guardados en `sdkconfig.defaults`. El archivo local completo `sdkconfig` y su
copia `sdkconfig.old` están excluidos mediante `.gitignore`.

Después de cambiar opciones y guardarlas en `menuconfig`, actualiza los valores
que se conservarán en el repositorio:

```sh
idf.py menuconfig
idf.py save-defconfig
idf.py reconfigure build
```

Incluye `sdkconfig.defaults` en el commit. Los defaults se usan para generar
una configuración nueva; no reemplazan automáticamente las opciones de un
`sdkconfig` existente. La carpeta `build/` contiene archivos generados y no se
versiona.

## MicroSD y las 16 emociones

Se utiliza **SDMMC de 4 bits**, no SDSPI. Los pines se configuran en
`componentes/driver/spi_manager/bus_sdmmc.c`; `bus_spi.c` sigue dedicado a la TFT.
El montaje FAT y la lectura/escritura se implementan en los tres archivos de
`componentes/modules/microSD`.

| Señal microSD | GPIO |
| --- | ---: |
| D2 | 13 |
| D3 | 14 |
| CMD | 15 |
| CLK | 16 |
| D0 | 17 |
| D1 | 18 |

La placa dispone de pull-ups externos de 10 kΩ a 3,3 V. El host solicita hasta
10 MHz y monta la tarjeta en `/sdcard`. No hay detección de extracción en caliente.
En las pruebas de escritura mientras BLE estaba conectado, a 20 MHz se detectaron
diferencias al releer los archivos. A 10 MHz pasó la verificación de los 115208 bytes;
se conserva esta frecuencia y se verifica cada archivo antes de publicarlo.
Apaga la placa antes de retirar la tarjeta.

El firmware **nunca formatea automáticamente**. La tarjeta debe tener un sistema
de archivos FAT compatible; si falla el montaje, consulta el registro antes de
cambiar el formato. No se debe formatear una tarjeta con contenido sin respaldo.

Al arrancar:

1. Inicializa la TFT y monta la microSD.
2. Crea `/sdcard/emotions` si no existe.
3. Copia los 16 archivos iniciales que falten. Conserva cualquier archivo existente.
   Escribe por bloques usando RAM interna apta para DMA en un temporal exclusivo,
   sincroniza, verifica todos los bytes leídos y luego lo renombra. No entrega
   directamente punteros de flash mapeada al DMA de SDMMC.
4. Lee `joy.tft`, valida TFT1 y muestra Feliz. Libera el buffer leído tras copiarlo al framebuffer.
5. Inicia BLE como `Cubo-ESP` y conserva la cara visible hasta recibir una orden desde la app. Una sola tarea administra la TFT y la SD.

Los nombres son compatibles con FAT 8.3, sin requerir soporte de nombres largos:

| Emoción | Archivo en `emotions/` |
| --- | --- |
| Estoy feliz | `joy.tft` |
| Estoy triste | `sad.tft` |
| Estoy enfadado | `angry.tft` |
| Estoy molesto | `upset.tft` |
| Estoy serio | `serious.tft` |
| Estoy avergonzado | `sorry.tft` |
| Estoy emocionado | `excited.tft` |
| Estoy enamorado | `inlove.tft` |
| Estoy nervioso | `nervous.tft` |
| Estoy pensativo | `thought.tft` |
| Estoy aburrido | `bored.tft` |
| Estoy sorprendido | `surprise.tft` |
| Estoy preocupado | `worried.tft` |
| Estoy asustado | `scared.tft` |
| Estoy deprimido | `crying.tft` |
| Estoy orgulloso | `proud.tft` |

Son ilustraciones originales de trazos simples, inspiradas en las expresiones de
referencia. Cada archivo ocupa 115.208 bytes; el conjunto ocupa 1.843.328 bytes
(aproximadamente 1,76 MiB). La copia de fábrica también ocupa ese espacio en la
partición de aplicación. La partición interna `model` no se usa para estos archivos.

Los originales TFT están en `main/assets/emotions/`. En `output/emotions/` se
generan PNG de cada cara, una vista conjunta `emociones.png`, un manifiesto y el
ZIP `emociones_sd.zip`. Para copiar con un lector de tarjetas, extrae el ZIP en
la raíz de la microSD: debe quedar `emotions/joy.tft`, etc.

Para editar una cara: importa su `.tft` en `../documents/index.html`, dibuja y
exporta ESP32. Renombra `drawing.tft` al nombre de la emoción y reemplaza ese
archivo de la tarjeta desde un lector de PC. No se requiere recompilar por un
cambio de contenido en la SD. Cambiar solo la copia de fábrica del firmware no
reemplaza una imagen que ya existe en la tarjeta.

El generador reproducible es `tools/generate_emotions.py` (Python y Pillow).
Ejecutarlo vuelve a generar los archivos locales; no escribe en la microSD.
Las operaciones de montaje y escritura están pensadas para una sola tarea.

Antes de conservar un archivo se valida su formato TFT1. Si la lectura fue
correcta pero el contenido es inválido, se conserva con extensión `.bad` antes de
reinstalarlo. No se modifican dibujos TFT1 válidos ni se pisan respaldos `.bad`.
Los errores de lectura o tamaños fuera del límite se registran sin reemplazar
automáticamente esos archivos.

La corrección de dientes de Amor y Miedo incluye una migración específica:
solo actualiza los archivos originales reconocidos por su SHA-256. Conserva
la ilustración previa como `inlove.v1` o `scared.v1`, escribe y verifica la
nueva versión, y restaura la previa si falla la escritura. Las caras editadas
por el usuario no coinciden con esas firmas y se conservan. Los respaldos
existentes tampoco se sobrescriben. El ZIP descargable contiene las caras
actualizadas.

Las etiquetas actuales se presentan en primera persona: `ESTOY` y el estado en
dos líneas. Esta actualización conserva los píxeles de los rostros y reconoce
por SHA-256 las 16 versiones de fábrica previas (incluidas las dos anteriores
a la corrección dental). Guarda la versión sustituida como `.v2` sin sobrescribir
respaldos ni dibujos personalizados. Las firmas autorizadas están en
`main/app/emotions/factory_history.inc`.

## Crear y mostrar un dibujo de respaldo en flash

1. Abre `../documents/index.html` en un navegador. El editor funciona sin conexión.
2. Dibuja en un lienzo de hasta **240 x 240** píxeles.
3. Pulsa **Exportar ESP32** para descargar `drawing.tft`.
4. Copia el archivo a `main/assets/drawing.tft`.
5. Ejecuta `idf.py reconfigure build` y después `idf.py -p COM13 flash monitor`.

Si no hay imágenes SD válidas, la aplicación muestra el dibujo y lo deja fijo. Las imágenes
menores que la pantalla se centran sobre un fondo negro. El dibujo persiste
después de apagar porque sus bytes forman parte de la aplicación en flash.
Para cambiarlo hay que sustituir el archivo, volver a compilar y grabar.

Si falta también el respaldo, la pantalla conserva su estado inicial y BLE sigue disponible. Después de agregar o quitar el recurso de flash, ejecuta `reconfigure` para actualizar su inclusión.

Conserva una copia de tus dibujos en el PC. Puedes importar TFT o TXT para
seguir editándolos, y guardar un PNG como vista de la imagen. El editor no tiene
guardado automático del lienzo. Las opciones RAW_HEX y RLE corresponden al TXT;
el firmware actual carga el archivo binario TFT1 exportado con **Exportar ESP32**.

## Formato de las imágenes: TFT1 y RGB565

**RGB565 es el formato de los píxeles. TFT1 es el contenedor binario propio del
proyecto, identificado con la extensión `.tft`.** La extensión no representa un
estándar universal ni un formato que ESP-IDF interprete por sí mismo.

### Píxeles RGB565

Cada píxel ocupa 16 bits: 5 para rojo, 6 para verde y 5 para azul. Permite
representar 65.536 colores, sin canal de transparencia.

```text
Bits: 15        11 10           5 4          0
      R R R R R    G G G G G G    B B B B B

RGB565 = ((R8 >> 3) << 11) | ((G8 >> 2) << 5) | (B8 >> 3)
```

`R8`, `G8` y `B8` son los canales de entrada de 8 bits. Al convertirlos se reduce
la precisión del color para adaptarse a RGB565.

| Color | Valor RGB565 | Bytes guardados y enviados |
| --- | --- | --- |
| Negro | `0x0000` | `00 00` |
| Rojo | `0xF800` | `F8 00` |
| Verde | `0x07E0` | `07 E0` |
| Azul | `0x001F` | `00 1F` |
| Blanco | `0xFFFF` | `FF FF` |

### Estructura binaria TFT1

| Offset | Longitud | Contenido |
| --- | ---: | --- |
| 0 | 4 bytes | Firma ASCII `TFT1`: `54 46 54 31` |
| 4 | 2 bytes | Ancho, `uint16` big-endian |
| 6 | 2 bytes | Alto, `uint16` big-endian |
| 8 | `ancho * alto * 2` bytes | Píxeles RGB565 big-endian |

Big-endian significa que se guarda primero el byte más significativo. Los
píxeles recorren la imagen **de izquierda a derecha y de arriba hacia abajo**,
sin relleno entre filas y sin terminador al final.

Por ejemplo, un archivo de 2 x 1 píxeles, rojo seguido de azul, contiene:

```text
54 46 54 31  00 02  00 01  F8 00  00 1F
   TFT1     ancho  alto   rojo   azul
```

El cargador exige ancho y alto entre 1 y 240, firma válida y longitud exacta.
La imagen se valida antes de modificar el framebuffer.

### Tamaño de archivo

El formato no tiene compresión:

```text
Tamaño en bytes = 8 + ancho * alto * 2
```

| Dimensiones | Píxeles | Archivo TFT1 |
| --- | ---: | ---: |
| 240 x 240 | 57.600 | 115.208 bytes |
| 128 x 128 | 16.384 | 32.776 bytes |
| 64 x 64 | 4.096 | 8.200 bytes |

A igual tamaño de lienzo, todos los dibujos ocupan lo mismo, independientemente
de sus colores o detalle. El costo es mayor almacenamiento que algunos formatos
comprimidos; a cambio, el ESP32 no necesita un decodificador PNG/JPEG ni convertir
los colores a RGB565 al mostrar la imagen.

## Del editor a la pantalla

```text
Editor HTML
  └─ convierte los colores a RGB565 y escribe drawing.tft (binario TFT1)
       └─ CMake incorpora sus bytes en la aplicación
            └─ flash graba el firmware en la memoria de la placa
                 └─ tft_draw_image() valida y copia al framebuffer
                      └─ tft_Show() transmite los píxeles por SPI al ST7789
```

La incorporación se declara en `main/CMakeLists.txt`:

```cmake
target_add_binary_data(${COMPONENT_LIB} "assets/drawing.tft" BINARY)
```

El archivo ya es binario antes de compilar. ESP-IDF incorpora sus bytes sin
reinterpretar la imagen; el significado de TFT1 lo define nuestro cargador.
El dibujo ocupa espacio dentro de la partición de aplicación, no en `model`.

El framebuffer de 240 x 240 ocupa **115.200 bytes**. Se intenta reservar en PSRAM;
si esa reserva falla, el controlador intenta usar RAM interna. Para transmitir
se usa un buffer DMA interno de **9.600 bytes**, equivalente a 20 filas. Cada
pantalla completa se envía en 12 bloques. La cabecera TFT1 no se transmite al LCD.
El framebuffer mantiene los bytes en el orden esperado por el ST7789.

Una imagen fija se envía al cargarla; el panel conserva sus píxeles en su memoria
mientras está encendido. La aplicación no necesita reenviarla continuamente.

### API de pantalla

| Función | Acción |
| --- | --- |
| `tft_init()` | Inicializa GPIO, SPI, controlador y buffers |
| `tft_fullScreen(color)` | Rellena el framebuffer con un color RGB565 |
| `tft_draw_image(image, size)` | Carga TFT1, centra el dibujo y rellena el exterior de negro |
| `tft_validate_image(image, size)` | Valida TFT1 sin modificar el framebuffer |
| `tft_Show()` | Envía el framebuffer al LCD |
| `tft_backlight(enabled)` | Enciende o apaga la iluminación |

Modificar el framebuffer no actualiza la pantalla hasta llamar `tft_Show()`.
Estas funciones se utilizan desde `app_main` durante el arranque y después
desde la tarea `cube_commands`. El driver no protege llamadas simultáneas:
la aplicación mantiene una sola tarea dueña de la pantalla y de la SD.

## Particiones de flash

Tabla seleccionada: `partitions/partitions.csv`.

| Nombre | Tipo/subtipo | Offset | Tamaño |
| --- | --- | --- | --- |
| `nvs` | data / nvs | `0x9000` | 24 KiB |
| `otadata` | data / ota | `0xF000` | 8 KiB |
| `ota_0` | app / ota_0 | `0x20000` | 5 MiB |
| `ota_1` | app / ota_1 | `0x520000` | 5 MiB |
| `model` | data / spiffs | `0xA20000` | 6016 KiB |

La última partición termina en `0x1000000`, correspondiente a 16 MiB de flash.
La presencia de dos slots OTA y una partición SPIFFS prepara el espacio, pero
el firmware actual no implementa actualizaciones OTA ni monta `model`.

## Verificaciones

Con Node.js instalado, desde la carpeta del firmware:

```sh
node tests/pixel_studio.test.mjs
```

La prueba usa las funciones reales del editor y verifica sintaxis JavaScript,
orden de bytes RGB565, exportación/importación TFT1, importación TXT RAW/RLE,
rechazo de archivos TFT1 inválidos y una imagen completa de 240 x 240.
Requiere que `../documents/index.html` esté presente.

La compilación se comprueba con `idf.py reconfigure build`. Las pruebas de formato
y el build no sustituyen la validación visual y el registro de arranque en la placa.

### Verificación realizada en la placa

En una verificación anterior se compiló y grabó el firmware por COM13. La tarjeta
se identificó como SDSC de 116 MiB y la PSRAM detectada fue de 8 MB. Aquella prueba
de lectura usó SDMMC de 4 bits a 20 MHz; la configuración actual es 10 MHz por las
diferencias detectadas posteriormente al verificar escrituras con BLE activo.
Se comprobó un ciclo de lectura y envío correcto de las 16 imágenes a la TFT.
Después de reiniciar, el registro confirmó:

```text
SD_IMAGENES: 0 nuevas, 16 existentes, 16 esperadas
```

Las capturas están en `output/sd_verified.log` (ciclo completo) y
`output/sd_persistence.log` (persistencia). Los `.log` se excluyen de Git.
Esta comprobación confirma lectura y transmisión sin errores; la inspección
visual final se realiza observando la TFT física.

## Alcance actual

La app Flutter selecciona una de 16 emociones y envía personalizaciones por BLE.
El cubo las guarda en microSD, verifica el contenido y muestra la cara elegida.
La instalación inicial y la restauración de originales se realizan desde flash.
Las personalizaciones se reenvían completas en cada pulsación de Mostrar; todavía
no hay comparación de versiones para evitar envíos repetidos.
La reproducción de audio, Wi-Fi, OTA y la integración de IMU y botones quedan
para siguientes etapas. No se cargan las 16 imágenes a la vez en RAM.

## Control desde el aplicativo Flutter por BLE

La aplicación hermana `../app_control_cubo_interactivo` permite seleccionar una sola emoción, editarla con una foto y mostrarla en el cubo. Al arrancar se muestra Feliz; se retiró el carrusel automático para conservar la cara elegida por el usuario. BLE se anuncia como **Cubo-ESP**.

El teléfono convierte la foto a TFT1/RGB565 de 240×240 y transmite por bloques confirmados. El cubo valida CRC32, verifica el archivo escrito y responde después de mostrarlo. Las caras originales pueden restablecerse desde la app. La publicación tiene recuperación por archivo usando `.new`/`.bak`; no formatea la microSD. Las ediciones originales del teléfono quedan en su almacenamiento privado.

Ver [protocolo, UUID y límites BLE](docs/BLE_PROTOCOL.md) y [README de la app](../app_control_cubo_interactivo/README.md). La seguridad actual es cifrado Just Works sin propiedad exclusiva del cubo.
