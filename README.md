# Cubo Interactivo Pediátrico — V1.7.2

Proyecto compuesto por una aplicación Flutter y un cubo basado en ESP32-S3. Permite seleccionar 16 emociones, mostrar su imagen en una TFT de 240×240 píxeles y reproducir un sonido asociado mediante el codec ES8311 y el amplificador NS4150B.

La V1.7.2 incorpora sincronización recuperable de imágenes desde el cubo para trabajar con varios teléfonos, actualización automática de la caché GATT de Android, indicadores visuales de audio, duración configurable hasta 3 minutos y reproducción WAV por flujo desde microSD.

## Estado alcanzado

- Catálogo estable de 16 emociones amarillas.
- Imágenes predeterminadas almacenadas exclusivamente en microSD.
- Directorios separados para recursos de fábrica y recursos del usuario.
- Personalización independiente de imagen y audio por emoción.
- Duración configurable de 15, 30, 60, 120 o 180 segundos.
- Recorte visual y reproducción previa con el límite seleccionado.
- Conversión automática del WAV elegido al formato reproducible por el ESP32-S3.
- Reproducción conjunta de imagen y audio al pulsar **Mostrar**.
- Comparación de tamaño y CRC32 antes de transferir una imagen o audio.
- Caché persistente de tamaño y CRC en archivos `.crc` de la microSD.
- Reproducción de audio asíncrona y por bloques directamente desde la microSD.
- Una emoción nueva interrumpe el audio anterior y comienza el sonido nuevo.
- Control del volumen del ES8311 entre 0 y 100 %, persistido en NVS.
- Publicación recuperable de archivos mediante temporales `.new` y respaldos `.bak`.
- Aplicación organizada en **Dispositivo / Emociones / Configuración**.
- Protocolo BLE V5 con enlace cifrado Just Works.
- MTU solicitado de 517 con ajuste automático al valor admitido por el teléfono.
- Sincronización manual y de solo lectura de las imágenes personalizadas del cubo.
- Indicador musical en cada emoción que posee un audio local o remoto.
- Detección y actualización automática de la caché GATT antigua en Android.

## Arquitectura

```text
Aplicación Flutter
    │
    │ BLE V5: comandos, estado y bloques bidireccionales
    ▼
ESP32-S3
    ├── TFT ST7789 240×240
    ├── microSD
    │   └── Doc_microSD/
    └── ES8311 + NS4150B
        └── bocina
```

La app conserva localmente las elecciones del usuario. Cuando se presiona **Mostrar**, consulta primero al cubo. Si el CRC coincide, el recurso se reutiliza. Un teléfono nuevo puede usar **Sincronizar imágenes desde el cubo** para descargar solo las caras diferentes y detectar qué emociones tienen audio, sin descargar los WAV.

Para una transferencia nueva, la app solicita ATT MTU 517. Con ese valor utiliza hasta 508 bytes de recurso por escritura; si el teléfono negocia 247, 185, 23 u otro tamaño, el bloque se reduce automáticamente sin cambiar el protocolo ni los archivos.

El firmware conserva junto a cada personalización un metadato `.crc` de 12 bytes con firma, tamaño y CRC32. Esto permite confirmar un WAV existente sin volver a leer hasta 2,88 MB en cada selección. La TFT se actualiza antes de responder y `cube_audio` lee el sonido desde la microSD en bloques de 512 muestras, sin reservar en PSRAM el archivo completo para reproducirlo.

## Uso con varios teléfonos

La microSD del cubo es la fuente oficial de los recursos compartidos. Cada teléfono conserva una copia local para presentar miniaturas y preparar ediciones, pero puede actualizarla desde **Configuración → Sincronizar imágenes desde el cubo**:

1. el cubo informa mediante dos mapas de 16 bits qué emociones poseen imagen personalizada y audio;
2. la app calcula el TFT1 y CRC32 de cada copia local;
3. el firmware prepara para descarga solamente las imágenes diferentes;
4. la app recibe bloques con offset, valida los 115.208 bytes y convierte RGB565 a PNG;
5. la cuadrícula se actualiza y conserva el mapa de audios para mostrar la nota musical.

La sincronización es de solo lectura para la microSD. No descarga los WAV ni cambia la imagen que se encuentra visible en la TFT. Como la microSD prevalece, la app solicita confirmación antes de reemplazar copias locales diferentes.

Desde V1.7.1, cada bloque descargado incluye su offset. El firmware usa bloques conservadores de 240 bytes, mantiene la imagen preparada durante 60 segundos y la app puede reposicionar el cursor y reintentar hasta tres veces si Android pierde una respuesta BLE. Esto evita reiniciar toda la descarga por una lectura aislada, especialmente con teléfonos que negocian MTU 517.

### Compatibilidad GATT en Android

Las primeras versiones del firmware publicaban la característica Datos únicamente para escritura. Android puede conservar esa definición en su caché Bluetooth incluso después de desinstalar la app o actualizar el firmware. En V1.7.2 la aplicación inspecciona las propiedades descubiertas al conectar; si Datos todavía aparece como no legible, limpia la caché GATT y reconecta una vez antes de habilitar la sincronización.

Si el fabricante del teléfono bloquea la operación de limpieza, la app muestra una instrucción explícita para desvincular `Cubo-ESP`, apagar y encender Bluetooth y volver a conectar. Esta limpieza no modifica la microSD ni elimina imágenes o audios.

Leyenda de la cuadrícula:

- icono de fotografía: existe una imagen personalizada;
- nota musical: existe un sonido asociado localmente o detectado en el cubo;
- ambos iconos: la emoción tiene imagen y sonido personalizados.

Desde V1.7.0, **Mostrar** sin una copia local utiliza el recurso activo del cubo. Ya no elimina una personalización creada desde otro teléfono. El borrado o restablecimiento requiere una acción explícita en Configuración.

## Emociones

| Índice | ID interno | Etiqueta |
|---:|---|---|
| 0 | `joy` | Feliz |
| 1 | `sad` | Triste |
| 2 | `angry` | Enfadado |
| 3 | `upset` | Molesto |
| 4 | `serious` | Serio |
| 5 | `sorry` | Avergonzado |
| 6 | `excited` | Emocionado |
| 7 | `inlove` | Enamorado |
| 8 | `nervous` | Nervioso |
| 9 | `thought` | Pensativo |
| 10 | `bored` | Aburrido |
| 11 | `surprise` | Sorprendido |
| 12 | `worried` | Preocupado |
| 13 | `scared` | Asustado |
| 14 | `crying` | Deprimido |
| 15 | `proud` | Orgulloso |

El orden es compartido por la app, el firmware, el manifiesto de microSD y el protocolo BLE. Cambiarlo sin actualizar todos los componentes produciría asociaciones incorrectas.

## Organización del repositorio

```text
62.Cubo_Interactivo_Pediatrico_ESP32_S3/
├── app_control_cubo_interactivo/   # Aplicación Flutter
│   ├── lib/
│   ├── test/
│   ├── assets/emotions/
│   └── release/                    # APK generados
├── Cubo_Interactivo_Esp_32_S3/    # Firmware ESP-IDF
│   ├── componentes/
│   ├── main/
│   ├── docs/
│   ├── tests/
│   └── tools/
├── Doc_microSD/                    # Copia lista para la tarjeta
├── documents/
│   ├── images/emotions_v1.1/       # Fuentes de las 16 caras
│   ├── microsd/                    # ZIP distribuible
│   └── ESP32-S3-LCD-1.54-Schematic.pdf
└── README.md
```

## Contenido de la microSD

En la raíz de la tarjeta debe copiarse una sola carpeta llamada `Doc_microSD`:

```text
Doc_microSD/
├── default/
│   └── images/             # 16 imágenes TFT1 originales
├── user/
│   ├── images/             # Personalizaciones del usuario
│   └── audio/              # WAV asociado a cada emoción
├── manifest.json
└── README.txt
```

Los archivos de `default/images` son inmutables. El firmware prioriza `user/images/<id>.tft`; si no existe o se restablece, utiliza `default/images/<id>.tft`. Los sonidos son opcionales y se guardan como `user/audio/<id>.wav`.

Puede copiarse directamente [Doc_microSD](Doc_microSD/) o descomprimir `documents/microsd/Doc_microSD_v1.5.0.zip`. El contenido visual continúa siendo V1.5.0 porque V1.7.0 modifica la aplicación, el firmware y la sincronización, no las 16 imágenes originales.

## Formatos

### Imagen TFT1

- Firma: `TFT1`.
- Resolución: 240×240.
- Píxeles: RGB565 big-endian.
- Orden: filas consecutivas.
- Tamaño exacto: 115.208 bytes.

### Audio

La app acepta WAV PCM mono o estéreo de 8 o 16 bits. Antes de guardarlo:

1. muestra la forma de onda completa;
2. permite elegir el inicio del fragmento;
3. reproduce una vista previa;
4. limita el resultado a la duración elegida en Configuración, entre 15 y 180 segundos;
5. convierte a WAV PCM mono, 8 bits sin signo y 16.000 Hz.

El archivo canónico ocupa como máximo 2.880.044 bytes. Un audio de 3 minutos utiliza aproximadamente 2,88 MB; 16 audios completos utilizan cerca de 46 MB.

## Flujo del usuario

1. Copiar `Doc_microSD` a la tarjeta e insertarla en el cubo.
2. Grabar el firmware V1.7.1.
3. Instalar el APK V1.7.2+12.
4. Abrir **Dispositivo**, buscar y conectar `Cubo-ESP`.
5. Abrir **Emociones** y seleccionar una cara.
6. Opcionalmente editar su imagen o audio.
7. Pulsar **Mostrar** para sincronizar y ejecutar la emoción.
8. Usar **Configuración** para regular volumen, elegir la duración máxima, restablecer imágenes o eliminar audios.
9. En un segundo teléfono, ejecutar **Sincronizar imágenes desde el cubo** para recuperar las miniaturas y los indicadores de sonido.

## Compatibilidad de versiones

| Componente | Versión actual | Compatibilidad |
|---|---|---|
| Aplicación | V1.7.2+12 | Requiere protocolo BLE V5 |
| Firmware | V1.7.1 | Implementa BLE V5 y descarga recuperable |
| Contenido microSD | V1.5.0 | Conserva las 16 imágenes predeterminadas |

## Audio del hardware

| Señal | GPIO |
|---|---:|
| I2S_MCLK | 8 |
| I2S_SCLK/BCLK | 9 |
| I2S_LRCK | 10 |
| I2S_DSDIN | 12 |
| CODEC_SDA | 42 |
| CODEC_SCL | 41 |
| PA_CTRL | 7 |

El volumen se aplica al ES8311 y se guarda en NVS bajo `cube_cfg/volume`, por lo que se recupera después de reiniciar el cubo.

## Compilación

### Firmware

```powershell
cd Cubo_Interactivo_Esp_32_S3
.\tools\idf.ps1 build
.\tools\idf.ps1 -p COM13 flash
```

### Aplicación

```powershell
cd app_control_cubo_interactivo
flutter pub get
flutter analyze
flutter test
flutter build apk --release
```

APK actual: `app_control_cubo_interactivo/release/cubo-interactivo-1.7.2+12.apk`.

## Validación realizada

- Firmware compilado con ESP-IDF 5.5.4.
- Aplicación analizada sin advertencias ni errores.
- Ocho pruebas Flutter aprobadas.
- Conversión TFT1, RGB565 y CRC32 verificados.
- Conversión inversa TFT1/RGB565 a PNG verificada.
- Indicador musical y diseño móvil verificados mediante pruebas de widgets.
- Conversión, duración y forma de onda WAV verificadas.
- Las 16 imágenes predeterminadas se validaron a 240×240.
- APK Android release generado correctamente.

La verificación automatizada no reemplaza la prueba física final de pantalla, volumen, bocina, microSD y estabilidad BLE.

## Documentación relacionada

- [Aplicación Flutter](app_control_cubo_interactivo/README.md)
- [Firmware ESP32-S3](Cubo_Interactivo_Esp_32_S3/README.md)
- [Protocolo BLE V5](Cubo_Interactivo_Esp_32_S3/docs/BLE_PROTOCOL.md)
- [Documentación y recursos](documents/README.md)
