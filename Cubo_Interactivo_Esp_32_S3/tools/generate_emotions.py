"""Build the V1.5.0 app assets and the copy-ready Doc_microSD folder.

The 16 supplied 240x240 PNG files are the source of truth. Factory faces live
on the microSD, not in firmware flash. User images and WAV files use separate
directories so restoring a face never overwrites the factory resource.
"""

from pathlib import Path
import json
import struct
import zipfile

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parent
SOURCE = REPOSITORY / "documents/images/emotions_v1.1"
APP_ASSETS = REPOSITORY / "app_control_cubo_interactivo/assets/emotions"
PREVIEWS = REPOSITORY / "documents/images/emotions"
MICROSD_ROOT = REPOSITORY / "Doc_microSD"
DEFAULT_IMAGES = MICROSD_ROOT / "default/images"
USER_IMAGES = MICROSD_ROOT / "user/images"
USER_AUDIO = MICROSD_ROOT / "user/audio"
MICROSD_RELEASE = REPOSITORY / "documents/microsd"

EMOTIONS = [
    ("joy", "FELIZ"),
    ("sad", "TRISTE"),
    ("angry", "ENFADADO"),
    ("upset", "MOLESTO"),
    ("serious", "SERIO"),
    ("sorry", "AVERGONZADO"),
    ("excited", "EMOCIONADO"),
    ("inlove", "ENAMORADO"),
    ("nervous", "NERVIOSO"),
    ("thought", "PENSATIVO"),
    ("bored", "ABURRIDO"),
    ("surprise", "SORPRENDIDO"),
    ("worried", "PREOCUPADO"),
    ("scared", "ASUSTADO"),
    ("crying", "DEPRIMIDO"),
    ("proud", "ORGULLOSO"),
]


def encode_tft(image):
    output = bytearray(struct.pack(">4sHH", b"TFT1", *image.size))
    for red, green, blue in image.get_flattened_data():
        rgb565 = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        output.extend(struct.pack(">H", rgb565))
    return output


def main():
    for directory in (
        APP_ASSETS,
        PREVIEWS,
        DEFAULT_IMAGES,
        USER_IMAGES,
        USER_AUDIO,
        MICROSD_RELEASE,
    ):
        directory.mkdir(parents=True, exist_ok=True)

    sheet = Image.new("RGB", (960, 960), "black")
    manifest = {
        "version": "1.5.0",
        "screen": {"width": 240, "height": 240, "pixel_format": "RGB565_BE"},
        "audio": {
            "format": "WAV PCM",
            "sample_rate": 16000,
            "channels": 1,
            "bits_per_sample": 8,
            "max_seconds": 15,
        },
        "emotions": [],
    }

    for index, (name, label) in enumerate(EMOTIONS):
        source = SOURCE / f"{name}.png"
        if not source.is_file():
            raise FileNotFoundError(f"Falta la fuente: {source}")
        image = Image.open(source).convert("RGB")
        if image.size != (240, 240):
            raise ValueError(f"{source} debe medir exactamente 240x240")
        binary = encode_tft(image)
        assert len(binary) == 115208

        image.save(APP_ASSETS / f"{name}.png", optimize=True)
        image.save(PREVIEWS / f"{name}.png", optimize=True)
        (DEFAULT_IMAGES / f"{name}.tft").write_bytes(binary)
        sheet.paste(image, ((index % 4) * 240, (index // 4) * 240))
        manifest["emotions"].append(
            {
                "index": index,
                "id": name,
                "label": label,
                "default_image": f"default/images/{name}.tft",
                "user_image": f"user/images/{name}.tft",
                "user_audio": f"user/audio/{name}.wav",
                "image_bytes": len(binary),
            }
        )

    sheet.save(PREVIEWS / "emociones.png", optimize=True)
    (MICROSD_ROOT / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    (MICROSD_ROOT / "README.txt").write_text(
        "Cubo Interactivo V1.5.0\n"
        "Copie esta carpeta Doc_microSD completa en la raiz de la microSD.\n"
        "default/images: 16 caras originales (no editar).\n"
        "user/images: caras elegidas por el usuario.\n"
        "user/audio: audio WAV asignado por emocion.\n",
        encoding="utf-8",
    )
    # Preserve empty user directories when the ZIP is extracted.
    (USER_IMAGES / ".gitkeep").touch()
    (USER_AUDIO / ".gitkeep").touch()

    archive_path = MICROSD_RELEASE / "Doc_microSD_v1.5.0.zip"
    with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(MICROSD_ROOT.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(REPOSITORY))

    print(f"{len(EMOTIONS)} caras amarillas generadas a 240x240")
    print(f"PNG app: {APP_ASSETS}")
    print(f"Carpeta lista para microSD: {MICROSD_ROOT}")
    print(f"ZIP: {archive_path}")


if __name__ == "__main__":
    main()
