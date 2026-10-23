import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';
import '../models/emotion.dart';
import 'package:image_picker/image_picker.dart';
import 'tft_codec.dart';
import 'wav_codec.dart';

class ArtworkStore extends ChangeNotifier {
  final Map<int, Uint8List> custom = {};
  final Map<int, Uint8List> audio = {};
  final Set<int> audioRemoved = {};
  final Set<int> remoteAudio = {};
  int audioDurationSeconds = defaultAudioSeconds;
  Directory? _directory;
  Future<void> load() async {
    final root = await getApplicationSupportDirectory();
    _directory = await Directory(
      '${root.path}/emotions',
    ).create(recursive: true);
    final durationFile = _audioDurationFile();
    if (await durationFile.exists()) {
      final saved = int.tryParse(await durationFile.readAsString());
      if (saved != null && audioDurationOptions.contains(saved)) {
        audioDurationSeconds = saved;
      }
    }
    final remoteAudioFile = _remoteAudioMaskFile();
    if (await remoteAudioFile.exists()) {
      final mask = int.tryParse(await remoteAudioFile.readAsString()) ?? 0;
      remoteAudio.addAll([
        for (var i = 0; i < emotionIds.length; i++)
          if ((mask & (1 << i)) != 0) i,
      ]);
    }
    for (var i = 0; i < emotionIds.length; i++) {
      final file = _file(i);
      if (await file.exists()) custom[i] = await file.readAsBytes();
      final audioFile = _audioFile(i);
      if (await audioFile.exists()) audio[i] = await audioFile.readAsBytes();
      if (await _audioMarker(i).exists()) audioRemoved.add(i);
    }
    // Android may kill the activity while its gallery is open.
    if (Platform.isAndroid) {
      final lost = await ImagePicker().retrieveLostData();
      final marker = File('${_directory!.path}/pending.txt');
      if (await marker.exists()) {
        final index = int.tryParse(await marker.readAsString());
        try {
          if (index != null &&
              index >= 0 &&
              index < emotionIds.length &&
              lost.files?.isNotEmpty == true) {
            final bytes = await lost.files!.first.readAsBytes();
            final png = await compute(prepareImage, {
              'bytes': bytes,
              'cover': true,
              'zoom': 1.0,
            });
            await save(index, png);
          }
        } finally {
          await marker.delete();
        }
      }
    }
    notifyListeners();
  }

  Future<void> beginPick(int index) async {
    await File(
      '${_directory!.path}/pending.txt',
    ).writeAsString('$index', flush: true);
  }

  Future<void> finishPick() async {
    final marker = File('${_directory!.path}/pending.txt');
    if (await marker.exists()) await marker.delete();
  }

  File _file(int i) => File('${_directory!.path}/${emotionIds[i]}.png');
  File _audioFile(int i) => File('${_directory!.path}/${emotionIds[i]}.wav');
  File _audioMarker(int i) =>
      File('${_directory!.path}/${emotionIds[i]}.noaudio');
  File _audioDurationFile() => File('${_directory!.path}/audio_duration.txt');
  File _remoteAudioMaskFile() =>
      File('${_directory!.path}/remote_audio_mask.txt');

  Future<void> saveAudioDuration(int seconds) async {
    if (!audioDurationOptions.contains(seconds)) {
      throw ArgumentError.value(seconds, 'seconds');
    }
    await _audioDurationFile().writeAsString('$seconds', flush: true);
    audioDurationSeconds = seconds;
    notifyListeners();
  }

  Future<void> save(int i, Uint8List png) async {
    final temp = File('${_file(i).path}.tmp');
    await temp.writeAsBytes(png, flush: true);
    await temp.rename(_file(i).path);
    custom[i] = png;
    notifyListeners();
  }

  Future<void> removeLocalImage(int i) async {
    final file = _file(i);
    if (await file.exists()) await file.delete();
    custom.remove(i);
    notifyListeners();
  }

  Future<void> setRemoteAudioMask(int mask) async {
    remoteAudio
      ..clear()
      ..addAll([
        for (var i = 0; i < emotionIds.length; i++)
          if ((mask & (1 << i)) != 0) i,
      ]);
    await _persistRemoteAudio();
    notifyListeners();
  }

  Future<void> _persistRemoteAudio() async {
    var mask = 0;
    for (final index in remoteAudio) {
      mask |= 1 << index;
    }
    await _remoteAudioMaskFile().writeAsString('$mask', flush: true);
  }

  bool hasAudio(int i) =>
      !audioRemoved.contains(i) &&
      (audio.containsKey(i) || remoteAudio.contains(i));

  Future<void> saveAudio(int i, Uint8List wav) async {
    final destination = _audioFile(i);
    final temp = File('${destination.path}.tmp');
    await temp.writeAsBytes(wav, flush: true);
    if (await destination.exists()) await destination.delete();
    await temp.rename(destination.path);
    final marker = _audioMarker(i);
    if (await marker.exists()) await marker.delete();
    audio[i] = wav;
    audioRemoved.remove(i);
    notifyListeners();
  }

  Future<void> removeAudio(int i) async {
    final file = _audioFile(i);
    if (await file.exists()) await file.delete();
    await _audioMarker(i).writeAsString('remove', flush: true);
    audio.remove(i);
    audioRemoved.add(i);
    notifyListeners();
  }

  Future<void> confirmAudioRemoval(int i) async {
    final marker = _audioMarker(i);
    if (await marker.exists()) await marker.delete();
    audioRemoved.remove(i);
    remoteAudio.remove(i);
    await _persistRemoteAudio();
    notifyListeners();
  }

  Future<void> clearImages() async {
    for (var i = 0; i < emotionIds.length; i++) {
      final f = _file(i);
      if (await f.exists()) await f.delete();
      custom.remove(i);
    }
    notifyListeners();
  }

  Future<void> clearAudio() async {
    for (var i = 0; i < emotionIds.length; i++) {
      final audioFile = _audioFile(i);
      final marker = _audioMarker(i);
      if (await audioFile.exists()) await audioFile.delete();
      if (await marker.exists()) await marker.delete();
    }
    audio.clear();
    audioRemoved.clear();
    remoteAudio.clear();
    await _persistRemoteAudio();
    notifyListeners();
  }

  Future<void> clear() => clearImages();
}
