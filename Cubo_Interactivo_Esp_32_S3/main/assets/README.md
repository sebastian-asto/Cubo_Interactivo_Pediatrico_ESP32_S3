# Dibujo de respaldo para la TFT

Las 16 emociones se leen primero desde la microSD. Sus archivos iniciales estan
en `emotions/` dentro de esta carpeta y se instalan automaticamente si faltan.
El archivo `drawing.tft` descrito aqui es el respaldo cuando no hay dibujos SD
validos. Ver el README principal para editar una emocion de la tarjeta.

1. Abre `documents/index.html` (en la carpeta superior al proyecto).
2. Dibuja con un lienzo de hasta **240 x 240** pixeles.
3. Pulsa **Exportar ESP32**. Se descarga `drawing.tft`.
4. Copia ese archivo a esta carpeta: `main/assets/drawing.tft`.
5. Desde una terminal ESP-IDF en la raiz del proyecto ejecuta:

   ```sh
   idf.py reconfigure build
   idf.py -p COM13 flash monitor
   ```

El firmware muestra este dibujo si no puede mostrar las emociones SD. Las imagenes menores de 240 x 240
se centran sobre negro. El dibujo queda en la flash y sobrevive a los apagados.
Para reemplazarlo, exporta otro archivo, copialo y vuelve a compilar y grabar.
Si no hay `drawing.tft`, se mantiene la prueba de colores.

Conserva una copia exportada del dibujo en tu PC. El editor permite importar
tanto TXT como TFT para seguir editando. No tiene guardado automatico del lienzo.
Exportar o compilar no graba la placa: el paso `flash` es el que la actualiza.

## Formato TFT1

| Bytes | Contenido |
| --- | --- |
| 0..3 | ASCII `TFT1` |
| 4..5 | Ancho, entero de 16 bits big-endian |
| 6..7 | Alto, entero de 16 bits big-endian |
| 8..fin | RGB565 big-endian, de izquierda a derecha y de arriba hacia abajo |

Un dibujo de 240 x 240 ocupa **115208 bytes**, incluida la cabecera.
La exportacion TFT no usa RLE; la seleccion RAW/RLE del editor corresponde al TXT.
El firmware valida la firma, dimensiones y longitud antes de modificar la imagen.
Esta primera version incorpora el recurso en la aplicacion; no escribe la particion
`model` ni implementa todavia transferencias desde el movil.
