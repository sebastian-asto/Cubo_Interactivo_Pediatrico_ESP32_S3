import 'dart:math';
import 'dart:typed_data';

const canonicalSampleRate = 16000;
const maxAudioSeconds = 15;

class WavInfo {
  const WavInfo({
    required this.sampleRate,
    required this.channels,
    required this.bitsPerSample,
    required this.frames,
  });
  final int sampleRate, channels, bitsPerSample, frames;
  double get durationSeconds => frames / sampleRate;
}

class _ParsedWav {
  const _ParsedWav(this.info, this.dataOffset, this.dataLength);
  final WavInfo info;
  final int dataOffset, dataLength;
}

_ParsedWav _parse(Uint8List bytes) {
  if (bytes.length < 44 ||
      String.fromCharCodes(bytes.sublist(0, 4)) != 'RIFF' ||
      String.fromCharCodes(bytes.sublist(8, 12)) != 'WAVE') {
    throw const FormatException('Selecciona un archivo WAV PCM válido.');
  }
  final data = ByteData.sublistView(bytes);
  int? rate, channels, bits, dataOffset, dataLength;
  for (var p = 12; p + 8 <= bytes.length;) {
    final id = String.fromCharCodes(bytes.sublist(p, p + 4));
    final size = data.getUint32(p + 4, Endian.little);
    final start = p + 8;
    if (start + size > bytes.length) {
      throw const FormatException('El archivo WAV está incompleto.');
    }
    if (id == 'fmt ' && size >= 16) {
      if (data.getUint16(start, Endian.little) != 1) {
        throw const FormatException('El audio WAV debe usar formato PCM.');
      }
      channels = data.getUint16(start + 2, Endian.little);
      rate = data.getUint32(start + 4, Endian.little);
      bits = data.getUint16(start + 14, Endian.little);
    } else if (id == 'data') {
      dataOffset = start;
      dataLength = size;
    }
    p = start + size + (size.isOdd ? 1 : 0);
  }
  if (rate == null ||
      channels == null ||
      bits == null ||
      dataOffset == null ||
      dataLength == null ||
      dataLength == 0 ||
      (channels != 1 && channels != 2) ||
      (bits != 8 && bits != 16) ||
      rate < 8000 ||
      rate > 96000) {
    throw const FormatException('Usa WAV PCM mono/estéreo, de 8 o 16 bits.');
  }
  final frameBytes = channels * (bits ~/ 8);
  return _ParsedWav(
    WavInfo(
      sampleRate: rate,
      channels: channels,
      bitsPerSample: bits,
      frames: dataLength ~/ frameBytes,
    ),
    dataOffset,
    dataLength,
  );
}

WavInfo inspectWav(Uint8List bytes) => _parse(bytes).info;

List<double> buildWaveform(Uint8List bytes, {int bins = 80}) {
  final parsed = _parse(bytes);
  final info = parsed.info;
  final source = ByteData.sublistView(bytes);
  final bytesPerSample = info.bitsPerSample ~/ 8;
  final frameBytes = info.channels * bytesPerSample;
  return List<double>.generate(bins, (bin) {
    final first = bin * info.frames ~/ bins;
    final last = max(first + 1, (bin + 1) * info.frames ~/ bins);
    final stride = max(1, (last - first) ~/ 32);
    var peak = 0.0;
    for (var frame = first; frame < last; frame += stride) {
      var sum = 0;
      for (var channel = 0; channel < info.channels; channel++) {
        final at =
            parsed.dataOffset + frame * frameBytes + channel * bytesPerSample;
        sum += info.bitsPerSample == 8
            ? (bytes[at] - 128) * 256
            : source.getInt16(at, Endian.little);
      }
      peak = max(peak, (sum / info.channels).abs() / 32768.0);
    }
    return peak.clamp(0.04, 1.0);
  });
}

Uint8List canonicalizeWav(Map<String, Object> request) {
  final input = request['bytes']! as Uint8List;
  final startSeconds = (request['start']! as num).toDouble();
  final parsed = _parse(input);
  final info = parsed.info;
  final sourceStart = min(
    info.frames,
    max(0, (startSeconds * info.sampleRate).round()),
  );
  final outputFrames = min(
    canonicalSampleRate * maxAudioSeconds,
    ((info.frames - sourceStart) * canonicalSampleRate / info.sampleRate)
        .ceil(),
  );
  final output = Uint8List(44 + outputFrames);
  final header = ByteData.sublistView(output);
  void tag(int at, String value) =>
      output.setRange(at, at + 4, value.codeUnits);
  tag(0, 'RIFF');
  header.setUint32(4, 36 + outputFrames, Endian.little);
  tag(8, 'WAVE');
  tag(12, 'fmt ');
  header.setUint32(16, 16, Endian.little);
  header.setUint16(20, 1, Endian.little);
  header.setUint16(22, 1, Endian.little);
  header.setUint32(24, canonicalSampleRate, Endian.little);
  header.setUint32(28, canonicalSampleRate, Endian.little);
  header.setUint16(32, 1, Endian.little);
  header.setUint16(34, 8, Endian.little);
  tag(36, 'data');
  header.setUint32(40, outputFrames, Endian.little);
  final source = ByteData.sublistView(input);
  final bytesPerSample = info.bitsPerSample ~/ 8;
  final frameBytes = info.channels * bytesPerSample;
  for (var i = 0; i < outputFrames; i++) {
    final frame = min(
      info.frames - 1,
      sourceStart + (i * info.sampleRate ~/ canonicalSampleRate),
    );
    var sum = 0;
    for (var channel = 0; channel < info.channels; channel++) {
      final at =
          parsed.dataOffset + frame * frameBytes + channel * bytesPerSample;
      sum += info.bitsPerSample == 8
          ? (input[at] - 128) * 256
          : source.getInt16(at, Endian.little);
    }
    final signed = sum ~/ info.channels;
    output[44 + i] = (signed ~/ 256 + 128).clamp(0, 255);
  }
  return output;
}
