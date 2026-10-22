# Recursos embebidos

Desde V1.5.0 las 16 emociones viven exclusivamente en `Doc_microSD/default/images`; no se duplican en la flash del firmware. Los `.tft` antiguos que puedan permanecer en esta carpeta no son referenciados por CMake.

`drawing.tft` continúa siendo un recurso de respaldo independiente cuando está presente.
