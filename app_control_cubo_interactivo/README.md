# App Control Cubo Interactivo — V1.5.1+8

Aplicación Flutter para Android e iOS que busca el cubo por Bluetooth Low Energy, administra 16 emociones y sincroniza imágenes y sonidos con el ESP32-S3.

## Funciones disponibles

- Búsqueda y conexión BLE integradas en la pantalla principal.
- Catálogo de 16 emociones con una sola selección activa.
- Imagen predeterminada o personalizada por emoción.
- Audio WAV opcional por emoción.
- Forma de onda y selección visual de un fragmento de hasta 15 segundos.
- Reproducción previa del audio convertido antes de guardarlo.
- Sincronización selectiva por tamaño y CRC32.
- Visualización de progreso y cancelación de transferencias.
- Control del volumen del cubo entre 0 y 100 %.
- Restablecimiento de las 16 imágenes sin borrar audios.
- Eliminación independiente de todos los audios personalizados.
- Persistencia local de las ediciones en el teléfono.

## Pantallas

### Dispositivo

Fusiona el resumen y el antiguo escáner BLE:

- indica si `Cubo-ESP` está conectado;
- muestra disponibilidad de microSD y audio;
- inicia o detiene la búsqueda de 15 segundos;
- lista los cubos encontrados, dirección y RSSI;
- permite conectar o desconectar el dispositivo;
- presenta errores de permisos y acceso directo a ajustes.

La app busca el UUID del servicio y no depende únicamente del nombre Bluetooth.

### Emociones

- Muestra las 16 caras en una cuadrícula desplazable.
- Mantiene una sola emoción seleccionada.
- **Mostrar** sincroniza los cambios y ordena al cubo mostrar y reproducir.
- **Editar** abre la configuración de imagen y sonido.
- Un recurso personalizado que ya coincide con el archivo de microSD no vuelve a enviarse.

### Editor de imagen y sonido

Imagen:

- selección desde la galería;
- modo **Llenar cuadro** o **Imagen completa**;
- control de acercamiento centrado;
- conversión final a 240×240 antes del envío.

Audio:

- selección de archivos `.wav` de hasta 30 MB;
- WAV PCM mono o estéreo, 8 o 16 bits, entre 8 y 96 kHz;
- forma de onda completa;
- resaltado del fragmento seleccionado;
- selector del instante inicial;
- reproducción y detención de la vista previa;
- fragmento máximo de 15 segundos;
- conversión final a mono, 8 bits y 16 kHz.

La vista previa utiliza `audioplayers`; se reproduce el mismo archivo canónico que posteriormente se guardará y enviará.

### Configuración

- Control del volumen de la bocina en pasos de 5 %.
- Lectura del volumen actual al conectar.
- Escritura y persistencia del volumen en el ESP32-S3.
- Restablecimiento de imágenes personalizadas.
- Eliminación de audios personalizados.

Las acciones que necesitan el cubo permanecen deshabilitadas cuando falta conexión, microSD o codec de audio.

## Persistencia local

La app usa el directorio privado de soporte de la plataforma:

```text
emotions/
├── <id>.png       # Imagen personalizada
├── <id>.wav       # Audio convertido
├── <id>.noaudio   # Eliminación pendiente de sincronizar
└── pending.txt    # Recuperación del selector de imágenes en Android
```

Una eliminación pendiente se conserva hasta que el cubo confirma el comando correspondiente.

## Comunicación BLE

Compatibilidad requerida: protocolo V4 y 16 emociones.

- Servicio: `72a10000-6c75-426f-9000-6375626f0001`.
- Características: comando, datos y estado.
- MTU solicitado: 247.
- Escrituras ATT con respuesta.
- Bloques adaptados al MTU negociado.
- Control por secuencia, offset, longitud y CRC32.
- Enlace cifrado Just Works.

La app rechaza firmware de otra versión para impedir que índices o comandos incompatibles modifiquen una emoción equivocada.

## Flujo de Mostrar

1. Si hay audio local, ejecuta `AUDIO_IF_MATCH`.
2. Si falta o cambió, lo transfiere con `BEGIN_AUDIO` y `COMMIT_AUDIO`.
3. Si se solicitó quitar audio, ejecuta `DELETE_AUDIO`.
4. Para una imagen personalizada, ejecuta `SHOW_IF_MATCH`.
5. Si falta o cambió, la transfiere con `BEGIN_IMAGE` y `COMMIT_IMAGE`.
6. Para una imagen original, ejecuta `RESTORE_SHOW`.
7. El firmware muestra la cara y reproduce el audio solo después de validar los recursos.

## Dependencias principales

- `flutter_reactive_ble`: comunicación BLE.
- `image_picker`: selección de imágenes.
- `file_picker`: selección de WAV.
- `audioplayers`: reproducción previa.
- `image`: procesamiento de imágenes.
- `path_provider`: almacenamiento privado.
- `permission_handler`: permisos y ajustes del sistema.
- `device_info_plus`: selección de permisos según versión Android.

## Requisitos

- Flutter compatible con Dart `^3.12.2`.
- Android o iOS para comunicación BLE.
- Firmware del cubo con protocolo V4.
- En Android 12 o superior: permisos de búsqueda y conexión Bluetooth.
- En Android anterior: ubicación durante la búsqueda BLE.

## Desarrollo y pruebas

```powershell
flutter pub get
dart format lib test integration_test
flutter analyze
flutter test
```

La prueba de integración con hardware es optativa y necesita un teléfono y el cubo encendido:

```powershell
flutter test integration_test/cube_ble_test.dart -d <dispositivo>
```

## Generar APK

```powershell
flutter build apk --release
```

Resultado de Flutter:

```text
build/app/outputs/flutter-apk/app-release.apk
```

Copia distribuible actual:

```text
release/cubo-interactivo-1.5.1+8.apk
```

La app V1.5.1+8 debe instalarse junto con el firmware BLE V4. El protocolo completo se encuentra en [BLE_PROTOCOL.md](../Cubo_Interactivo_Esp_32_S3/docs/BLE_PROTOCOL.md).
