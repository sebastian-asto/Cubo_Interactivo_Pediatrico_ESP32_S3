import 'dart:async';
import 'dart:io';
import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:device_info_plus/device_info_plus.dart';
import 'tft_codec.dart';
import '../models/emotion.dart';

final cubeService = Uuid.parse('72a10000-6c75-426f-9000-6375626f0001');
Uuid cubeCharacteristic(int n) =>
    Uuid.parse('72a1000$n-6c75-426f-9000-6375626f0001');

class CubeBle extends ChangeNotifier {
  late final FlutterReactiveBle _ble = FlutterReactiveBle();
  final Map<String, DiscoveredDevice> devices = {};
  StreamSubscription<DiscoveredDevice>? _scan;
  StreamSubscription<ConnectionStateUpdate>? _connection;
  StreamSubscription<ConnectionStateUpdate>? _connectionEvents;
  Timer? _scanTimer;
  DateTime? _lastDisconnect;
  String? deviceId;
  String? error;
  String message = 'Conecta tu cubo para comenzar';
  bool scanning = false,
      connecting = false,
      busy = false,
      sdReady = false,
      audioReady = false;
  bool _cancel = false;
  int _sequence = 0, _mtu = 23, _generation = 0, _protocolVersion = 0;
  int volume = 65;
  double progress = 0;
  bool get connected => deviceId != null;

  Future<void> scan() async {
    await stopScan();
    error = null;
    devices.clear();
    if (!Platform.isAndroid && !Platform.isIOS) {
      error = 'La conexión BLE está disponible en Android e iOS.';
      notifyListeners();
      return;
    }
    try {
      if (Platform.isAndroid) {
        final sdk = (await DeviceInfoPlugin().androidInfo).version.sdkInt;
        final permissions = sdk >= 31
            ? [Permission.bluetoothScan, Permission.bluetoothConnect]
            : [Permission.locationWhenInUse];
        final result = await permissions.request();
        if (result.values.any((p) => !p.isGranted)) {
          throw StateError(
            'Permite Bluetooth/dispositivos cercanos en Ajustes para buscar el cubo.',
          );
        }
      }
      await _ble.initialize();
      final state = _ble.status == BleStatus.ready
          ? BleStatus.ready
          : await _ble.statusStream
                .firstWhere((s) => s != BleStatus.unknown)
                .timeout(const Duration(seconds: 8));
      if (state != BleStatus.ready) {
        throw StateError(
          'Activa Bluetooth y revisa sus permisos. Estado: ${state.name}. En Android antiguo, activa también Ubicación.',
        );
      }
      scanning = true;
      notifyListeners();
      _scan = _ble
          .scanForDevices(
            withServices: [cubeService],
            scanMode: ScanMode.lowLatency,
          )
          .listen(
            (d) {
              devices[d.id] = d;
              notifyListeners();
            },
            onError: (Object e) {
              error = userError(e);
              unawaited(stopScan());
            },
          );
      _scanTimer = Timer(
        const Duration(seconds: 15),
        () => unawaited(stopScan()),
      );
    } catch (e) {
      error = userError(e);
      scanning = false;
      notifyListeners();
    }
  }

  Future<void> stopScan() async {
    _scanTimer?.cancel();
    await _scan?.cancel();
    _scan = null;
    scanning = false;
    notifyListeners();
  }

  QualifiedCharacteristic _char(int n) => QualifiedCharacteristic(
    serviceId: cubeService,
    characteristicId: cubeCharacteristic(n),
    deviceId: deviceId!,
  );
  Future<void> connect(DiscoveredDevice device) async {
    await stopScan();
    await disconnect();
    connecting = true;
    error = null;
    message = 'Conectando…';
    notifyListeners();
    // Keep the native connection event channel alive between connections.
    // Closing its final listener disposes Android's connection manager.
    _connectionEvents ??= _ble.connectedDeviceStream.listen((event) {
      if (kDebugMode) {
        debugPrint('BLE event: ${event.connectionState} ${event.failure}');
      }
    });
    // Android may report disconnected before the controller has closed GATT.
    // Let that teardown finish before starting another connection to this cube.
    final lastDisconnect = _lastDisconnect;
    if (lastDisconnect != null) {
      final remaining =
          const Duration(seconds: 3) -
          DateTime.now().difference(lastDisconnect);
      if (remaining > Duration.zero) await Future<void>.delayed(remaining);
    }
    final complete = Completer<void>();
    final generation = ++_generation;
    _connection = _ble
        .connectToDevice(
          id: device.id,
          connectionTimeout: const Duration(seconds: 15),
          servicesWithCharacteristicsToDiscover: {
            cubeService: [
              cubeCharacteristic(1),
              cubeCharacteristic(2),
              cubeCharacteristic(3),
            ],
          },
        )
        .listen(
          (update) {
            if (generation != _generation) return;
            if (update.connectionState == DeviceConnectionState.connected) {
              deviceId = device.id;
              if (!complete.isCompleted) complete.complete();
            } else if (update.connectionState ==
                DeviceConnectionState.disconnected) {
              deviceId = null;
              sdReady = false;
              audioReady = false;
              _protocolVersion = 0;
              message = 'Cubo desconectado';
              if (!complete.isCompleted) {
                complete.completeError(
                  StateError('No se pudo conectar al cubo.'),
                );
              }
              notifyListeners();
            }
          },
          onError: (Object e) {
            if (generation != _generation) return;
            deviceId = null;
            sdReady = false;
            audioReady = false;
            _protocolVersion = 0;
            error = userError(e);
            if (!complete.isCompleted) complete.completeError(e);
            notifyListeners();
          },
        );
    try {
      await complete.future.timeout(const Duration(seconds: 20));
      try {
        _mtu = await _ble.requestMtu(deviceId: device.id, mtu: 247);
      } catch (_) {
        _mtu = 23;
      }
      message = 'Preparando conexión segura…';
      notifyListeners();
      List<int>? status;
      final discoveryDeadline = DateTime.now().add(const Duration(seconds: 30));
      while (status == null) {
        if (!connected) {
          throw StateError(
            'El cubo se desconectó al preparar la conexión. Vuelve a conectar.',
          );
        }
        if (DateTime.now().isAfter(discoveryDeadline)) {
          throw StateError(
            'No terminó el emparejamiento. Acepta la solicitud de Bluetooth y vuelve a conectar.',
          );
        }
        try {
          status = await _ble
              .readCharacteristic(_char(3))
              .timeout(const Duration(seconds: 8));
          if (status.length == 16 && (status[14] & 2) == 0) {
            status = null;
            await Future<void>.delayed(const Duration(milliseconds: 500));
          }
        } on PlatformException catch (e) {
          if (e.code != 'service_discovery_failure' ||
              !connected ||
              DateTime.now().isAfter(discoveryDeadline)) {
            rethrow;
          }
          await Future<void>.delayed(const Duration(milliseconds: 700));
        }
      }
      if (status.length != 16 ||
          status[0] != 4 ||
          status[15] != emotionIds.length) {
        throw StateError('Firmware incompatible con esta app.');
      }
      _protocolVersion = status[0];
      sdReady = (status[14] & 1) != 0;
      audioReady = (status[14] & 4) != 0;
      if (audioReady) {
        final volumeStatus = await _command(13);
        volume = volumeStatus[5].clamp(0, 100);
      }
      message = sdReady
          ? 'Cubo conectado · listo'
          : 'Cubo conectado · microSD no disponible';
    } catch (e) {
      if (kDebugMode) debugPrint('BLE connect failed: $e');
      error = userError(e);
      await disconnect();
    }
    connecting = false;
    notifyListeners();
  }

  Future<void> disconnect() async {
    ++_generation;
    if (_connection != null) {
      await _connection!.cancel();
      _lastDisconnect = DateTime.now();
    }
    _connection = null;
    deviceId = null;
    sdReady = false;
    audioReady = false;
    _protocolVersion = 0;
    connecting = false;
    message = 'Cubo desconectado';
    notifyListeners();
  }

  Future<Uint8List> _command(
    int op, {
    int index = 0,
    int length = 0,
    int crc = 0,
  }) async {
    if (!connected) throw StateError('Conecta el cubo primero.');
    final seq = (_sequence + 1) & 65535;
    _sequence = seq;
    final bytes = Uint8List(12);
    final data = ByteData.sublistView(bytes);
    bytes[0] = op;
    data.setUint16(1, seq, Endian.little);
    bytes[3] = index;
    data.setUint32(4, length, Endian.little);
    data.setUint32(8, crc, Endian.little);
    await _ble
        .writeCharacteristicWithResponse(_char(1), value: bytes)
        .timeout(const Duration(seconds: 15));
    final deadline = DateTime.now().add(const Duration(seconds: 90));
    while (DateTime.now().isBefore(deadline)) {
      if (!connected) {
        throw StateError('Se perdió la conexión. Vuelve a conectar y enviar.');
      }
      final status = await _ble
          .readCharacteristic(_char(3))
          .timeout(const Duration(seconds: 8));
      if (status.length != 16) throw StateError('Respuesta del cubo inválida.');
      final s = ByteData.sublistView(Uint8List.fromList(status));
      if (s.getUint16(2, Endian.little) == seq &&
          status[4] == op &&
          status[1] != 1) {
        if (status[1] != 0) {
          throw CubeCommandException(s.getUint32(6, Endian.little));
        }
        return Uint8List.fromList(status);
      }
      await Future<void>.delayed(const Duration(milliseconds: 120));
    }
    throw TimeoutException(
      'El cubo no confirmó la operación. Revisa la conexión antes de reintentar.',
    );
  }

  void cancelUpload() {
    _cancel = true;
  }

  Future<void> _uploadResource({
    required int beginOp,
    required int commitOp,
    required int index,
    required Uint8List bytes,
    required String label,
  }) async {
    final checksum = crc32(bytes);
    await _command(beginOp, index: index, length: bytes.length, crc: checksum);
    final chunk = max(1, min(_mtu - 3, 244) - 4);
    for (var offset = 0; offset < bytes.length;) {
      if (_cancel) {
        throw StateError('Envío cancelado; el recurso anterior se conserva.');
      }
      if (!connected) {
        throw StateError('Se desconectó el cubo. Vuelve a enviar.');
      }
      final count = min(chunk, bytes.length - offset);
      final packet = Uint8List(4 + count);
      ByteData.sublistView(packet).setUint32(0, offset, Endian.little);
      packet.setRange(4, packet.length, bytes, offset);
      await _ble
          .writeCharacteristicWithResponse(_char(2), value: packet)
          .timeout(const Duration(seconds: 8));
      offset += count;
      progress = offset / bytes.length;
      message = 'Enviando $label · ${(progress * 100).round()} %';
      notifyListeners();
    }
    await _command(commitOp, index: index);
  }

  Future<void> show(
    int index,
    Uint8List? png,
    Uint8List? wav, {
    bool removeAudio = false,
  }) async {
    if (busy) return;
    if (!connected || !sdReady) {
      throw StateError('Conecta un cubo con microSD disponible.');
    }
    busy = true;
    _cancel = false;
    progress = 0;
    message = 'Preparando imagen…';
    notifyListeners();
    try {
      if (wav != null) {
        if (!audioReady) {
          throw StateError('El audio del cubo no está disponible.');
        }
        final checksum = crc32(wav);
        message = 'Comprobando audio en la microSD…';
        notifyListeners();
        try {
          await _command(11, index: index, length: wav.length, crc: checksum);
        } on CubeCommandException catch (e) {
          if (e.code != 0x109 && e.code != 0x105) rethrow;
          await _uploadResource(
            beginOp: 8,
            commitOp: 9,
            index: index,
            bytes: wav,
            label: 'audio',
          );
        }
      } else if (removeAudio) {
        await _command(10, index: index);
      }
      progress = 0;
      if (png == null) {
        await _command(6, index: index);
      } else {
        final bytes = await compute(pngToTft, png);
        final checksum = crc32(bytes);
        if (_protocolVersion >= 2) {
          message = 'Comprobando imagen en la microSD…';
          notifyListeners();
          try {
            await _command(
              7,
              index: index,
              length: bytes.length,
              crc: checksum,
            );
            message = 'Imagen recuperada de la microSD';
            progress = 1;
            return;
          } on CubeCommandException catch (e) {
            // ESP_ERR_INVALID_CRC means that this slot contains another image.
            // Any other error is a real SD/display failure and must be reported.
            if (e.code != 0x109 && e.code != 0x105) rethrow;
          }
        }
        if (Platform.isAndroid && connected) {
          try {
            await _ble.requestConnectionPriority(
              deviceId: deviceId!,
              priority: ConnectionPriority.highPerformance,
            );
          } catch (_) {}
        }
        await _uploadResource(
          beginOp: 2,
          commitOp: 3,
          index: index,
          bytes: bytes,
          label: 'imagen',
        );
      }
      message = 'Imagen confirmada en el cubo';
      progress = 1;
    } catch (e) {
      if (connected) {
        try {
          await _command(5);
        } catch (_) {}
      }
      message = 'No se completó el envío';
      rethrow;
    } finally {
      if (Platform.isAndroid && connected) {
        try {
          await _ble.requestConnectionPriority(
            deviceId: deviceId!,
            priority: ConnectionPriority.balanced,
          );
        } catch (_) {}
      }
      busy = false;
      notifyListeners();
    }
  }

  Future<void> restore() async {
    if (busy || !connected || !sdReady) {
      throw StateError('Conecta el cubo con su microSD antes de restaurar.');
    }
    busy = true;
    message = 'Restableciendo las 16 imágenes…';
    notifyListeners();
    try {
      await _command(4);
      message = 'Las 16 imágenes originales están listas';
    } finally {
      busy = false;
      notifyListeners();
    }
  }

  Future<void> removeAllAudio() async {
    if (busy || !connected || !sdReady) {
      throw StateError('Conecta el cubo con su microSD antes de continuar.');
    }
    busy = true;
    try {
      for (var i = 0; i < emotionIds.length; i++) {
        message = 'Quitando audios · ${i + 1}/${emotionIds.length}';
        notifyListeners();
        await _command(10, index: i);
      }
      message = 'Los audios personalizados fueron eliminados';
    } finally {
      busy = false;
      notifyListeners();
    }
  }

  Future<void> setVolume(int value) async {
    if (busy || !connected || !audioReady) {
      throw StateError('Conecta un cubo con audio disponible.');
    }
    final next = value.clamp(0, 100);
    busy = true;
    message = 'Ajustando volumen…';
    notifyListeners();
    try {
      await _command(12, length: next);
      volume = next;
      message = 'Volumen del cubo: $next %';
    } finally {
      busy = false;
      notifyListeners();
    }
  }

  @override
  void dispose() {
    _scanTimer?.cancel();
    _scan?.cancel();
    _connection?.cancel();
    _connectionEvents?.cancel();
    super.dispose();
  }
}

class CubeCommandException implements Exception {
  const CubeCommandException(this.code);

  final int code;

  @override
  String toString() => 'CubeCommandException(0x${code.toRadixString(16)})';
}

String userError(Object error) {
  if (error is StateError) return error.message.toString();
  if (error is TimeoutException) {
    return 'El cubo tardó demasiado en responder. Acércalo y vuelve a conectar.';
  }
  if (error is PlatformException) {
    if (error.code == 'service_discovery_failure') {
      return 'No terminó el emparejamiento. Acepta la solicitud de Bluetooth y vuelve a conectar.';
    }
    return 'No se pudo completar la conexión Bluetooth. Comprueba que el cubo siga encendido y vuelve a intentar.';
  }
  if (error is FormatException) return error.message;
  if (error is CubeCommandException) {
    return 'El cubo no completó la operación (0x${error.code.toRadixString(16)}). Revisa la microSD y vuelve a intentar.';
  }
  return 'No se pudo completar la operación. Vuelve a intentarlo.';
}
