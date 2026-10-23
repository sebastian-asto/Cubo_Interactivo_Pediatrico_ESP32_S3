import 'dart:typed_data';
import 'package:image/image.dart' as img;

int crc32(List<int> bytes) {
  var crc = 0xffffffff;
  for (final byte in bytes) {
    crc ^= byte;
    for (var i = 0; i < 8; i++) {
      crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb88320 : 0);
    }
  }
  return (~crc) & 0xffffffff;
}

Uint8List encodeTft(img.Image image) {
  if (image.width != 240 || image.height != 240) {
    throw ArgumentError('Se requiere una imagen de 240 × 240.');
  }
  final bytes = Uint8List(115208);
  bytes.setAll(0, [84, 70, 84, 49, 0, 240, 0, 240]);
  final data = ByteData.sublistView(bytes);
  var offset = 8;
  for (final pixel in image) {
    final a = pixel.a.toDouble() / 255;
    final r = (pixel.r * a + 16 * (1 - a)).round();
    final g = (pixel.g * a + 27 * (1 - a)).round();
    final b = (pixel.b * a + 42 * (1 - a)).round();
    data.setUint16(
      offset,
      ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3),
      Endian.big,
    );
    offset += 2;
  }
  return bytes;
}

// Runs in an isolate; gallery images are capped at 2048 pixels by the picker.
Uint8List prepareImage(Map<String, Object> args) {
  final decoded = img.decodeImage(args['bytes'] as Uint8List);
  if (decoded == null) {
    throw const FormatException(
      'No se pudo leer esta imagen. Prueba con JPG o PNG.',
    );
  }
  var source = img.bakeOrientation(decoded);
  final cover = args['cover'] as bool;
  final zoom = args['zoom'] as double;
  if (cover) {
    final side =
        ((source.width < source.height ? source.width : source.height) / zoom)
            .round()
            .clamp(1, 100000);
    source = img.copyCrop(
      source,
      x: (source.width - side) ~/ 2,
      y: (source.height - side) ~/ 2,
      width: side,
      height: side,
    );
  }
  final image = img.Image(width: 240, height: 240);
  img.fill(image, color: img.ColorRgb8(16, 27, 42));
  final ratio =
      240 / (source.width > source.height ? source.width : source.height);
  final scaled = img.copyResize(
    source,
    width: (source.width * ratio).round().clamp(1, 240),
    height: (source.height * ratio).round().clamp(1, 240),
    interpolation: img.Interpolation.average,
  );
  img.compositeImage(
    image,
    scaled,
    dstX: (240 - scaled.width) ~/ 2,
    dstY: (240 - scaled.height) ~/ 2,
  );
  // Quantize the preview too: it must match the pixels sent to the TFT.
  for (final p in image) {
    final r = p.r.toInt() >> 3, g = p.g.toInt() >> 2, b = p.b.toInt() >> 3;
    p.setRgb((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
  }
  return Uint8List.fromList(img.encodePng(image));
}

Uint8List pngToTft(Uint8List bytes) {
  final image = img.decodePng(bytes);
  if (image == null) {
    throw const FormatException('Imagen local dañada. Vuelve a editarla.');
  }
  return encodeTft(image);
}

Uint8List tftToPng(Uint8List bytes) {
  if (bytes.length != 115208 ||
      bytes[0] != 84 ||
      bytes[1] != 70 ||
      bytes[2] != 84 ||
      bytes[3] != 49 ||
      bytes[4] != 0 ||
      bytes[5] != 240 ||
      bytes[6] != 0 ||
      bytes[7] != 240) {
    throw const FormatException('La imagen recibida del cubo no es TFT1.');
  }
  final data = ByteData.sublistView(bytes);
  final image = img.Image(width: 240, height: 240);
  var offset = 8;
  for (var y = 0; y < 240; y++) {
    for (var x = 0; x < 240; x++) {
      final value = data.getUint16(offset, Endian.big);
      final r = (value >> 11) & 0x1f;
      final g = (value >> 5) & 0x3f;
      final b = value & 0x1f;
      image.setPixelRgb(
        x,
        y,
        (r << 3) | (r >> 2),
        (g << 2) | (g >> 4),
        (b << 3) | (b >> 2),
      );
      offset += 2;
    }
  }
  return Uint8List.fromList(img.encodePng(image));
}
