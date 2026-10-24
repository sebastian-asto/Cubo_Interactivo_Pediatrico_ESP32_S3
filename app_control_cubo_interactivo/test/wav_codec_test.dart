import 'dart:typed_data';

import 'package:app_control_cubo_interactivo/services/wav_codec.dart';
import 'package:flutter_test/flutter_test.dart';

Uint8List wav16Stereo({int rate = 8000, int frames = 16000}) {
  final out = Uint8List(44 + frames * 4);
  final d = ByteData.sublistView(out);
  void tag(int at, String s) => out.setRange(at, at + 4, s.codeUnits);
  tag(0, 'RIFF');
  d.setUint32(4, out.length - 8, Endian.little);
  tag(8, 'WAVE');
  tag(12, 'fmt ');
  d.setUint32(16, 16, Endian.little);
  d.setUint16(20, 1, Endian.little);
  d.setUint16(22, 2, Endian.little);
  d.setUint32(24, rate, Endian.little);
  d.setUint32(28, rate * 4, Endian.little);
  d.setUint16(32, 4, Endian.little);
  d.setUint16(34, 16, Endian.little);
  tag(36, 'data');
  d.setUint32(40, frames * 4, Endian.little);
  for (var i = 0; i < frames; i++) {
    d.setInt16(44 + i * 4, i.isEven ? 12000 : -12000, Endian.little);
    d.setInt16(46 + i * 4, i.isEven ? 12000 : -12000, Endian.little);
  }
  return out;
}

void main() {
  test('inspecciona y convierte WAV a formato canónico', () {
    final input = wav16Stereo();
    final info = inspectWav(input);
    expect(info.durationSeconds, 2);
    expect(info.channels, 2);
    final output = canonicalizeWav({
      'bytes': input,
      'start': 0.5,
      'duration': 15,
    });
    final canonical = inspectWav(output);
    expect(canonical.sampleRate, canonicalSampleRate);
    expect(canonical.channels, 1);
    expect(canonical.bitsPerSample, 8);
    expect(canonical.durationSeconds, closeTo(1.5, .001));
    final waveform = buildWaveform(input);
    expect(waveform, hasLength(80));
    expect(waveform.every((value) => value >= .04 && value <= 1), isTrue);
  });

  test('respeta una duración configurable de hasta tres minutos', () {
    final input = wav16Stereo(rate: 8000, frames: 8000 * 181);
    final output = canonicalizeWav({
      'bytes': input,
      'start': 0,
      'duration': 180,
    });
    expect(inspectWav(output).durationSeconds, 180);
  });

  test('la conversión cooperativa produce el mismo WAV sin bloquear', () async {
    final input = wav16Stereo(rate: 44100, frames: 44100 * 4);
    final request = <String, Object>{
      'bytes': input,
      'start': 1.25,
      'duration': 30,
    };
    final expected = canonicalizeWav(request);
    final actual = await canonicalizeWavAsync(request, batchFrames: 257);
    expect(actual, orderedEquals(expected));
  });

  test('rechaza un archivo que no es WAV', () {
    expect(() => inspectWav(Uint8List(44)), throwsFormatException);
  });
}
