# Cubo · Emociones

La arquitectura conjunta, los pines, los formatos, el comportamiento de transferencia y la galería de las 16 caras están en el [README general](../README.md).

App Flutter para elegir una de 16 emociones, personalizar su imagen y enviarla por BLE al ESP32-S3. La selección es única. El cubo confirma que la imagen fue guardada y mostrada antes de que la app anuncie éxito.

## Uso

1. Enciende el cubo con la microSD insertada.
2. Pulsa **Conectar → Buscar Cubo-ESP**, concede Bluetooth/dispositivos cercanos y selecciona el cubo.
3. Selecciona una cara y pulsa **Mostrar**.
4. **Editar** permite elegir una foto de la galería, ajustar cuadro completo/recorte central y acercamiento. **Usar esta imagen** guarda la edición en el teléfono; **Mostrar** la transmite al cubo.
5. **Restablecer las 16 imágenes** pide confirmación y restaura las originales en el cubo y luego en este teléfono. Una restauración interrumpida puede quedar parcial: vuelve a ejecutarla.

La paleta usa naranja #E5A05D, verde #87A27E, rojo #BE6053 y fondo #171916. Las ilustraciones proceden del mismo generador que los recursos del firmware.

## Desarrollo

```powershell
flutter pub get
flutter analyze
flutter test
flutter build apk --debug
flutter run -d <id-del-telefono>
```

Android: compileSdk 37, permisos Bluetooth declarados y solicitados según versión. Android 11 y anteriores requieren Ubicación para el escaneo. La galería usa el selector del sistema; no requiere acceso general al almacenamiento. iOS: descripciones Bluetooth/fototeca incluidas; compilar y firmar desde macOS/Xcode. El transporte BLE está implementado para Android/iOS, no web/Windows.

En este entorno Windows, Java 21 puede fallar en los sockets Unix internos de Gradle. Para ese caso se verificó usar una opción solo en el proceso de compilación:

```powershell
$env:JAVA_TOOL_OPTIONS='-Djdk.net.unixdomain.tmpdir=C:/nonexistent-cubo-java-sockets'
flutter build apk --debug
```

`kotlin.incremental=false` evita el fallo de caché Kotlin cuando el proyecto está en D: y los paquetes de Pub en C:.

## Organización

- `lib/screens/emotions_screen.dart`: tarjetas, selección única, mostrar y restaurar.
- `lib/screens/editor_screen.dart`: galería y vista previa de los píxeles enviados.
- `lib/screens/scanner_screen.dart`: permisos, búsqueda, conexión y desconexión.
- `lib/services/cube_ble.dart`: cliente BLE, progreso, cancelación y confirmaciones.
- `lib/services/tft_codec.dart`: normalización 240×240, TFT1/RGB565 big-endian y CRC32.
- `lib/services/artwork_store.dart`: imágenes personalizadas en el almacenamiento privado del teléfono.
- `assets/emotions/`: las 16 imágenes originales.

## Contrato y límites

El protocolo está documentado en `../Cubo_Interactivo_Esp_32_S3/docs/BLE_PROTOCOL.md`. No se transfieren PNG/JPG al microcontrolador: el teléfono genera un TFT1 binario de 115208 bytes. Se envía por bloques con respuesta ATT, offset y CRC32 final. El tamaño del bloque se adapta al MTU. La cancelación/desconexión antes de publicar conserva el archivo anterior; un corte durante la publicación se recupera al arrancar usando el respaldo por archivo.

La app conserva sus propias ediciones, pero no descarga imágenes personalizadas por otro teléfono. Al mostrar una cara original restablece esa emoción en el cubo para que la pantalla corresponda a la vista previa. Las fotos editadas se reenvían al pulsar Mostrar. El cubo conserva la última cara mientras esté encendido; al reiniciar muestra Feliz.

BLE usa cifrado de sesión Just Works. Esta versión de desarrollo no implementa propiedad exclusiva del cubo, PIN ni autenticación del adulto. No depende de Internet ni de un servidor.

Las pruebas de widget verifican selección única, acciones sin conexión y tamaños pequeños. Las pruebas de codec verifican cabecera, RGB565, tamaño, proporción y CRC32. La prueba de integración requiere el cubo de desarrollo y restaura sus imágenes originales; no ejecutarla sobre una tarjeta con personalizaciones que deban conservarse.

## Verificación en hardware — 21 de octubre de 2026

Probado en Moto g86 power 5G y ESP32-S3 con microSD: escaneo, conexión cifrada,
mostrar cara original, cancelar transferencia, enviar imagen modificada de 115208 bytes,
verificar y publicar en SD, restablecer las 16 originales y reconectar para mostrar Feliz.
La prueba de integración completa pasó. También pasaron las cinco pruebas de widgets/codec.

El bus SDMMC está a 10 MHz: a 20 MHz se observaron diferencias en la lectura de
verificación durante escritura con BLE activo. Cada archivo se verifica antes de sustituir
el anterior. Android espera un breve cierre de GATT antes de reconectar.
iOS tiene configuración y código, pero no se compiló ni probó en hardware en este entorno Windows.
