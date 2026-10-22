import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;
import 'package:app_control_cubo_interactivo/services/tft_codec.dart';

void main() {
  test('CRC32 interoperable con firmware', () {
    expect(crc32(ascii.encode('123456789')), 0xcbf43926);
  });
  test('TFT1 tamaño, RGB565 y orden big endian', () {
    final image = img.Image(width: 240, height: 240);
    image.setPixelRgb(0, 0, 255, 0, 0);
    image.setPixelRgb(1, 0, 0, 255, 0);
    image.setPixelRgb(2, 0, 0, 0, 255);
    final bytes = encodeTft(image);
    expect(bytes.length, 115208);
    expect(bytes.sublist(0, 8), [84, 70, 84, 49, 0, 240, 0, 240]);
    expect(bytes.sublist(8, 14), [0xf8, 0, 0x07, 0xe0, 0, 0x1f]);
    expect(
      () => encodeTft(img.Image(width: 1, height: 1)),
      throwsArgumentError,
    );
  });
  test('Ajustar foto conserva proporción y genera archivo válido', () {
    final wide = img.Image(width: 480, height: 120);
    img.fill(wide, color: img.ColorRgb8(255, 0, 0));
    final png = prepareImage({
      'bytes': Uint8List.fromList(img.encodePng(wide)),
      'cover': false,
      'zoom': 1.0,
    });
    final result = img.decodePng(png)!;
    expect(result.width, 240);
    expect(result.height, 240);
    expect(result.getPixel(120, 120).r, 255);
    expect(result.getPixel(120, 0).r, lessThan(30));
    expect(pngToTft(png).length, 115208);
  });
}
