# Cubo Interactivo Pediátrico — V1.5.1

Proyecto compuesto por una aplicación Flutter y un cubo basado en ESP32-S3. Permite seleccionar 16 emociones, mostrar su imagen en una TFT de 240×240 píxeles y reproducir un sonido asociado mediante el codec ES8311 y el amplificador NS4150B.

La V1.5.1 incorpora imágenes y audios persistentes en microSD, sincronización BLE que evita transferencias repetidas, recorte y vista previa de audio, y control remoto del volumen de la bocina.

## Estado alcanzado

- Catálogo estable de 16 emociones amarillas.
- Imágenes predeterminadas almacenadas exclusivamente en microSD.
- Directorios separados para recursos de fábrica y recursos del usuario.
- Personalización independiente de imagen y audio por emoción.
- Recorte visual y reproducción previa de fragmentos de hasta 15 segundos.
- Conversión automática del WAV elegido al formato reproducible por el ESP32-S3.
- Reproducción conjunta de imagen y audio al pulsar **Mostrar**.
- Comparación de tamaño y CRC32 antes de transferir una imagen o audio.
- Control del volumen del ES8311 entre 0 y 100 %, persistido en NVS.
- Publicación recuperable de archivos mediante temporales `.new` y respaldos `.bak`.
- Aplicación organizada en **Dispositivo / Emociones / Configuración**.
- Protocolo BLE V4 con enlace cifrado Just Works.

## Arquitectura

```text
Aplicación Flutter
    │
    │ BLE V4: comandos, estado y bloques de datos
    ▼
ESP32-S3
    ├── TFT ST7789 240×240
    ├── microSD
    │   └── Doc_microSD/
    └── ES8311 + NS4150B
        └── bocina
```

La app conserva localmente las elecciones del usuario. Cuando se presiona **Mostrar**, consulta primero al cubo. Si el CRC del recurso coincide con el archivo existente en la microSD, se reutiliza sin transmitirlo nuevamente. Si cambió, se envía, valida, publica y posteriormente se muestra o reproduce.

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

Puede copiarse directamente [Doc_microSD](Doc_microSD/) o descomprimir `documents/microsd/Doc_microSD_v1.5.0.zip`. El contenido visual continúa siendo V1.5.0 porque V1.5.1 modifica la aplicación, el firmware y el control del dispositivo, no las 16 imágenes originales.

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
4. limita el resultado a 15 segundos;
5. convierte a WAV PCM mono, 8 bits sin signo y 16.000 Hz.

El archivo canónico ocupa como máximo 240.044 bytes.

## Flujo del usuario

1. Copiar `Doc_microSD` a la tarjeta e insertarla en el cubo.
2. Grabar el firmware V1.5.1.
3. Instalar el APK V1.5.1+8.
4. Abrir **Dispositivo**, buscar y conectar `Cubo-ESP`.
5. Abrir **Emociones** y seleccionar una cara.
6. Opcionalmente editar su imagen o audio.
7. Pulsar **Mostrar** para sincronizar y ejecutar la emoción.
8. Usar **Configuración** para regular volumen, restablecer imágenes o eliminar audios.

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

APK actual: `app_control_cubo_interactivo/release/cubo-interactivo-1.5.1+8.apk`.

## Validación realizada

- Firmware compilado con ESP-IDF 5.5.4.
- Aplicación analizada sin advertencias ni errores.
- Siete pruebas Flutter aprobadas.
- Conversión TFT1, RGB565 y CRC32 verificados.
- Conversión, duración y forma de onda WAV verificadas.
- Las 16 imágenes predeterminadas se validaron a 240×240.
- APK Android release generado correctamente.

La verificación automatizada no reemplaza la prueba física final de pantalla, volumen, bocina, microSD y estabilidad BLE.

## Documentación relacionada

- [Aplicación Flutter](app_control_cubo_interactivo/README.md)
- [Firmware ESP32-S3](Cubo_Interactivo_Esp_32_S3/README.md)
- [Protocolo BLE V4](Cubo_Interactivo_Esp_32_S3/docs/BLE_PROTOCOL.md)
- [Documentación y recursos](documents/README.md)
