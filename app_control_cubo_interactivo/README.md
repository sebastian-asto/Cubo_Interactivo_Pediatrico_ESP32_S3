# App Control Cubo Interactivo — V1.7.2+12

Aplicación Flutter para Android e iOS que busca el cubo por Bluetooth Low Energy, administra 16 emociones y sincroniza imágenes y sonidos con el ESP32-S3.

## Funciones disponibles

- Búsqueda y conexión BLE integradas en la pantalla principal.
- Catálogo de 16 emociones con una sola selección activa.
- Imagen predeterminada o personalizada por emoción.
- Audio WAV opcional por emoción.
- Forma de onda y selección visual de fragmentos configurables hasta 3 minutos.
- Reproducción previa del audio convertido antes de guardarlo.
- Sincronización selectiva por tamaño y CRC32.
- Visualización de progreso y cancelación de transferencias.
- Control del volumen del cubo entre 0 y 100 %.
- Restablecimiento de las 16 imágenes sin borrar audios.
- Eliminación independiente de todos los audios personalizados.
- Persistencia local de las ediciones en el teléfono.
- Descarga selectiva de imágenes personalizadas desde el cubo.
- Icono musical para emociones con audio local o detectado en la microSD.

## Pantallas

### Dispositivo

Fusiona el resumen y el antiguo escáner BLE:

- indica si `Cubo-ESP` está conectado;
- muestra disponibilidad de microSD y audio;
- inicia o detiene la búsqueda de 15 segundos;
- lista los cubos encontrados, dirección y RSSI;
- permite conectar o desconectar el dispositivo;
- presenta errores de permisos y acceso directo a ajustes.
- comprueba que la característica Datos sea legible y actualiza una caché GATT antigua en Android antes de continuar.

La app busca el UUID del servicio y no depende únicamente del nombre Bluetooth.

### Emociones

- Muestra las 16 caras en una cuadrícula desplazable.
- Mantiene una sola emoción seleccionada.
- **Mostrar** sincroniza los cambios y ordena al cubo mostrar y reproducir.
- **Editar** abre la configuración de imagen y sonido.
- Un recurso personalizado que ya coincide con el archivo de microSD no vuelve a enviarse.
- Una nota musical identifica visualmente las emociones que tienen sonido.

Leyenda de las tarjetas:

| Indicador | Significado |
|---|---|
| Fotografía | La emoción usa una imagen personalizada |
| Nota musical | La emoción posee audio local o detectado en el cubo |
| Fotografía + nota | Imagen y sonido personalizados |

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
- fragmento configurable de 15, 30, 60, 120 o 180 segundos;
- conversión final a mono, 8 bits y 16 kHz.

La vista previa utiliza `audioplayers`; se reproduce el mismo archivo canónico que posteriormente se guardará y enviará.

### Configuración

- Control del volumen de la bocina en pasos de 5 %.
- Lectura del volumen actual al conectar.
- Escritura y persistencia del volumen en el ESP32-S3.
- Duración máxima de los nuevos audios, persistida localmente en la app.
- Botón **Sincronizar imágenes desde el cubo**, con confirmación previa.
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
├── audio_duration.txt # Duración elegida para nuevos recortes
├── remote_audio_mask.txt # Emociones con audio detectado en el cubo
└── pending.txt    # Recuperación del selector de imágenes en Android
```

Una eliminación pendiente se conserva hasta que el cubo confirma el comando correspondiente.

## Comunicación BLE

Compatibilidad requerida: protocolo V5 y 16 emociones.

- Servicio: `72a10000-6c75-426f-9000-6375626f0001`.
- Características: comando, datos y estado.
- MTU solicitado: 517, con adaptación automática al valor negociado.
- MTU efectivo visible en la tarjeta de estado del dispositivo.
- Escrituras ATT con respuesta.
- Bloques adaptados al MTU negociado.
- Hasta 508 bytes útiles de recurso por escritura cuando el MTU es 517.
- Bloques de descarga de hasta 240 bytes para evitar fragmentación en el controlador del ESP32-S3.
- Control por secuencia, offset, longitud y CRC32.
- Enlace cifrado Just Works.

La app rechaza firmware de otra versión para impedir que índices o comandos incompatibles modifiquen una emoción equivocada.

## Sincronización desde el cubo

La configuración permite consultar mapas de imágenes y audios para las 16 emociones. La app compara el CRC de sus copias locales, descarga por BLE solamente las imágenes distintas, convierte TFT1/RGB565 a PNG y actualiza la cuadrícula. Los audios no se descargan: su presencia se representa con un icono musical.

Si una instalación nueva pulsa **Mostrar** sin copia local, usa la imagen activa guardada en el cubo y no elimina la personalización remota. El restablecimiento solo ocurre desde la acción explícita de Configuración.

Flujo del botón:

1. pide confirmación porque la microSD prevalece sobre copias locales diferentes;
2. ejecuta `GET_RESOURCE_MAPS` para conocer las 16 imágenes y audios existentes;
3. compara cada imagen local mediante tamaño y CRC32;
4. usa `PREPARE_IMAGE_DOWNLOAD` solo cuando la imagen falta o difiere;
5. lee TFT1 por bloques cifrados de hasta 240 bytes y valida cada offset;
6. ante una lectura interrumpida, solicita nuevamente el offset pendiente y reintenta hasta tres veces;
7. convierte RGB565 a PNG e informa cuántas imágenes personalizadas y emociones con sonido encontró.

Los WAV no se descargan. `remote_audio_mask.txt` conserva el mapa recibido para que las notas musicales continúen visibles al volver a abrir la app.

### Caché GATT de Android

Android puede mantener en el sistema las propiedades de un servicio BLE aunque la aplicación se desinstale. Esto es relevante porque versiones antiguas del firmware declaraban Datos solo para escritura, mientras BLE V5 también permite leerla para recuperar imágenes.

Durante la conexión, V1.7.2+12 descubre todos los servicios y comprueba `isReadable`. Si detecta la definición antigua, llama a la actualización de caché proporcionada por `flutter_reactive_ble`, desconecta y conecta una sola vez. Si el sistema no permite actualizarla, solicita desvincular `Cubo-ESP` manualmente desde Ajustes de Bluetooth. Los recursos de la microSD no se modifican durante este procedimiento.

## Flujo de Mostrar

1. Si hay audio local, ejecuta `AUDIO_IF_MATCH`.
2. Si falta o cambió, lo transfiere con `BEGIN_AUDIO` y `COMMIT_AUDIO`.
3. Si se solicitó quitar audio, ejecuta `DELETE_AUDIO`.
4. Para una imagen personalizada, ejecuta `SHOW_IF_MATCH`.
5. Si falta o cambió, la transfiere con `BEGIN_IMAGE` y `COMMIT_IMAGE`.
6. Si el teléfono no posee copia local, ejecuta `SHOW` y reutiliza la imagen activa del cubo sin borrarla.
7. El firmware muestra la cara y reproduce el audio solo después de validar los recursos.

La confirmación de **Mostrar** se recibe cuando la TFT fue actualizada y el audio quedó encolado. La reproducción continúa de forma asíncrona; al seleccionar otra emoción, el firmware interrumpe el sonido anterior y reproduce el nuevo. Los CRC confirmados se conservan en la microSD para acelerar selecciones posteriores, incluso después de reiniciar el cubo.

## Cambios de V1.7.0+10

- Compatibilidad exclusiva con firmware BLE V5.
- Sincronización de imágenes entre teléfonos usando el cubo como fuente.
- Conversión TFT1/RGB565 a PNG dentro de un isolate.
- Indicadores separados para imagen y sonido.
- Persistencia local del mapa de audios remotos.
- Protección contra el restablecimiento accidental al mostrar desde una instalación nueva.

## Corrección de V1.7.1+11

- La descarga de imágenes reintenta hasta tres veces una lectura BLE interrumpida.
- Antes de reintentar, la app envía al cubo el offset exacto que todavía necesita.
- Si la transferencia no puede recuperarse, el aviso indica el byte en el que se interrumpió en lugar de mostrar solamente un error genérico.

## Corrección de V1.7.2+12

- Al conectar en Android, la app verifica que la característica BLE de datos figure como legible.
- Si Android conserva la definición antigua de solo escritura, limpia la caché GATT y reconecta una sola vez para descubrir los servicios actuales.
- Si la lectura todavía falla, el aviso incluye el tipo de error BLE para facilitar el diagnóstico físico.

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
- Firmware del cubo con protocolo V5.
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
release/cubo-interactivo-1.7.2+12.apk
```

La app V1.7.2+12 debe instalarse junto con el firmware V1.7.1/BLE V5. El protocolo completo se encuentra en [BLE_PROTOCOL.md](../Cubo_Interactivo_Esp_32_S3/docs/BLE_PROTOCOL.md).
