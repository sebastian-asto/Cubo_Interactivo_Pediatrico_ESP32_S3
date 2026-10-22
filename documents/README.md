# Documentación y recursos

- `index.html`: editor TFT Pixel Studio sin conexión.
- `ESP32-S3-LCD-1.54-Schematic.pdf`: esquema de la placa.
- `images/emotions_v1.1/`: fuentes originales de las 16 emociones amarillas.
- `images/emotions/`: copias 240×240 generadas y mosaico de revisión.
- `images/mobile/`: capturas de la aplicación.
- `microsd/`: manifiesto y ZIP para preparar manualmente una microSD.
- `logs/`: registros locales de validación; los archivos `.log` no se versionan.

El script `../Cubo_Interactivo_Esp_32_S3/tools/generate_emotions.py` valida las 16 fuentes 240×240, actualiza los PNG de la app, genera los TFT1 predeterminados dentro de `Doc_microSD/default/images` y crea `microsd/Doc_microSD_v1.5.0.zip`.

En la tarjeta debe existir una sola carpeta raíz llamada `Doc_microSD`. `default/images` contiene los 16 originales; `user/images` y `user/audio` quedan reservados para lo que sincronice la app.
