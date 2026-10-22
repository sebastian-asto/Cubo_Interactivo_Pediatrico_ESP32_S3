import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';
import '../models/emotion.dart';
import 'package:image_picker/image_picker.dart';
import 'tft_codec.dart';

class ArtworkStore extends ChangeNotifier {
  final Map<int, Uint8List> custom = {};
  Directory? _directory;
  Future<void> load() async {
    final root = await getApplicationSupportDirectory();
    _directory = await Directory(
      '${root.path}/emotions',
    ).create(recursive: true);
    for (var i = 0; i < emotionIds.length; i++) {
      final file = _file(i);
      if (await file.exists()) custom[i] = await file.readAsBytes();
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
              index < 16 &&
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
  Future<void> save(int i, Uint8List png) async {
    final temp = File('${_file(i).path}.tmp');
    await temp.writeAsBytes(png, flush: true);
    await temp.rename(_file(i).path);
    custom[i] = png;
    notifyListeners();
  }

  Future<void> clear() async {
    for (var i = 0; i < emotionIds.length; i++) {
      final f = _file(i);
      if (await f.exists()) await f.delete();
      custom.remove(i);
    }
    notifyListeners();
  }
}
