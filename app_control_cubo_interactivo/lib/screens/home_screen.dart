import 'package:flutter/material.dart';
import 'package:permission_handler/permission_handler.dart';

import '../main.dart';
import '../models/emotion.dart';
import '../services/artwork_store.dart';
import '../services/cube_ble.dart';
import '../services/wav_codec.dart';
import 'emotions_screen.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({
    super.key,
    required this.store,
    required this.ble,
    this.startupError,
  });
  final ArtworkStore store;
  final CubeBle ble;
  final String? startupError;
  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  int page = 0;

  void _notice(String text) => ScaffoldMessenger.of(context).showSnackBar(
    SnackBar(content: Text(text), behavior: SnackBarBehavior.floating),
  );

  Future<void> _restoreImages() async {
    final yes = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('¿Restablecer las 16 imágenes?'),
        content: const Text(
          'Se quitarán las imágenes personalizadas. Los audios se conservarán.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(c, false),
            child: const Text('Cancelar'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(c, true),
            child: const Text('Restablecer'),
          ),
        ],
      ),
    );
    if (yes != true) return;
    try {
      await widget.ble.restore();
      await widget.store.clearImages();
      if (mounted) _notice('Las 16 imágenes predeterminadas están listas.');
    } catch (e) {
      if (mounted) _notice(userError(e));
    }
  }

  Future<void> _removeAudio() async {
    try {
      await widget.ble.removeAllAudio();
      await widget.store.clearAudio();
      if (mounted) _notice('Se eliminaron los audios personalizados.');
    } catch (e) {
      if (mounted) _notice(userError(e));
    }
  }

  Future<void> _syncImages() async {
    final yes = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('¿Traer imágenes del cubo?'),
        content: const Text(
          'La microSD será la fuente principal. Las imágenes locales diferentes se reemplazarán por las guardadas en el cubo.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(c, false),
            child: const Text('Cancelar'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(c, true),
            child: const Text('Sincronizar'),
          ),
        ],
      ),
    );
    if (yes != true) return;
    try {
      final result = await widget.ble.syncImages(Map.of(widget.store.custom));
      for (var i = 0; i < emotionIds.length; i++) {
        if ((result.imageMask & (1 << i)) == 0 &&
            widget.store.custom.containsKey(i)) {
          await widget.store.removeLocalImage(i);
        }
      }
      for (final entry in result.images.entries) {
        await widget.store.save(entry.key, entry.value);
      }
      await widget.store.setRemoteAudioMask(result.audioMask);
      final customCount = _bitCount(result.imageMask);
      final audioCount = _bitCount(result.audioMask);
      if (mounted) {
        _notice(
          '$customCount imágenes personalizadas · $audioCount emociones con sonido.',
        );
      }
    } catch (e) {
      if (mounted) _notice(userError(e));
    }
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: Listenable.merge([widget.store, widget.ble]),
    builder: (context, _) => Scaffold(
      body: IndexedStack(
        index: page,
        children: [
          _DeviceScan(ble: widget.ble),
          EmotionsScreen(
            store: widget.store,
            ble: widget.ble,
            startupError: widget.startupError,
            onDevice: () => setState(() => page = 0),
          ),
          _DeviceSettings(
            ble: widget.ble,
            store: widget.store,
            onSync: _syncImages,
            onRestore: _restoreImages,
            onRemoveAudio: _removeAudio,
          ),
        ],
      ),
      bottomNavigationBar: NavigationBar(
        selectedIndex: page,
        onDestinationSelected: (value) => setState(() => page = value),
        destinations: const [
          NavigationDestination(
            icon: Icon(Icons.bluetooth_searching_rounded),
            label: 'Dispositivo',
          ),
          NavigationDestination(
            icon: Icon(Icons.grid_view_rounded),
            label: 'Emociones',
          ),
          NavigationDestination(
            icon: Icon(Icons.settings_rounded),
            label: 'Configuración',
          ),
        ],
      ),
    ),
  );
}

class _DeviceScan extends StatelessWidget {
  const _DeviceScan({required this.ble});
  final CubeBle ble;
  @override
  Widget build(BuildContext context) => SafeArea(
    child: Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 560),
        child: ListView(
          padding: const EdgeInsets.all(24),
          children: [
            const Icon(Icons.view_in_ar_rounded, color: orange, size: 44),
            const SizedBox(height: 14),
            const Text(
              'Encuentra.\nConecta. Expresa.',
              style: TextStyle(
                fontSize: 34,
                height: 1.05,
                fontWeight: FontWeight.w900,
              ),
            ),
            const SizedBox(height: 10),
            const Text(
              'Busca y conecta el cubo para configurar sus 16 emociones, imágenes y sonidos.',
            ),
            const SizedBox(height: 24),
            Card(
              child: ListTile(
                leading: Icon(
                  ble.connected ? Icons.bluetooth_connected : Icons.bluetooth,
                  color: ble.connected ? sage : orange,
                ),
                title: Text(
                  ble.connected ? 'Cubo-ESP conectado' : 'Cubo desconectado',
                ),
                subtitle: Text(
                  ble.connected
                      ? (ble.sdReady
                            ? 'microSD lista${ble.audioReady ? ' · audio listo' : ' · audio no disponible'} · MTU ${ble.negotiatedMtu}'
                            : 'Revisa la microSD')
                      : 'Enciende el cubo y mantenlo cerca.',
                ),
                trailing: ble.connected
                    ? IconButton(
                        tooltip: 'Desconectar',
                        onPressed: ble.busy ? null : ble.disconnect,
                        icon: const Icon(Icons.link_off_rounded),
                      )
                    : null,
              ),
            ),
            const SizedBox(height: 14),
            FilledButton.icon(
              onPressed: ble.connecting || ble.connected
                  ? null
                  : (ble.scanning ? ble.stopScan : ble.scan),
              icon: Icon(
                ble.scanning ? Icons.stop_rounded : Icons.radar_rounded,
              ),
              label: Text(
                ble.scanning ? 'Detener búsqueda' : 'Buscar Cubo-ESP',
              ),
            ),
            if (ble.scanning) ...[
              const SizedBox(height: 10),
              const LinearProgressIndicator(),
              const SizedBox(height: 8),
              const Text('Buscando cubos cercanos durante 15 segundos…'),
            ],
            if (ble.connecting)
              const Padding(
                padding: EdgeInsets.all(20),
                child: Center(child: CircularProgressIndicator()),
              ),
            if (ble.error != null)
              Card(
                color: coral.withValues(alpha: .2),
                child: Padding(
                  padding: const EdgeInsets.all(14),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(ble.error!),
                      TextButton(
                        onPressed: openAppSettings,
                        child: const Text('Abrir ajustes de la app'),
                      ),
                    ],
                  ),
                ),
              ),
            if (!ble.connected)
              ...ble.devices.values.map(
                (device) => Card(
                  child: ListTile(
                    contentPadding: const EdgeInsets.all(14),
                    leading: const Icon(
                      Icons.view_in_ar_rounded,
                      color: orange,
                    ),
                    title: Text(device.name.isEmpty ? 'Cubo-ESP' : device.name),
                    subtitle: Text('${device.id}\nSeñal: ${device.rssi} dBm'),
                    isThreeLine: true,
                    trailing: const Icon(Icons.arrow_forward_rounded),
                    onTap: ble.connecting ? null : () => ble.connect(device),
                  ),
                ),
              ),
            if (!ble.scanning && !ble.connected && ble.devices.isEmpty)
              const Padding(
                padding: EdgeInsets.symmetric(vertical: 22),
                child: Text(
                  'Aquí aparecerán los cubos disponibles. Comprueba que el tuyo esté encendido y libre de otra conexión.',
                ),
              ),
          ],
        ),
      ),
    ),
  );
}

class _DeviceSettings extends StatefulWidget {
  const _DeviceSettings({
    required this.ble,
    required this.store,
    required this.onSync,
    required this.onRestore,
    required this.onRemoveAudio,
  });
  final CubeBle ble;
  final ArtworkStore store;
  final VoidCallback onSync, onRestore, onRemoveAudio;
  @override
  State<_DeviceSettings> createState() => _DeviceSettingsState();
}

class _DeviceSettingsState extends State<_DeviceSettings> {
  double? draftVolume;

  Future<void> _commitVolume(double value) async {
    try {
      await widget.ble.setVolume(value.round());
      if (mounted) setState(() => draftVolume = null);
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(userError(e)),
            behavior: SnackBarBehavior.floating,
          ),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final ble = widget.ble;
    final volume = draftVolume ?? ble.volume.toDouble();
    final canConfigure = ble.connected && ble.audioReady && !ble.busy;
    return SafeArea(
      child: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 560),
          child: ListView(
            padding: const EdgeInsets.all(24),
            children: [
              const Text(
                'Configuración del cubo',
                style: TextStyle(fontSize: 30, fontWeight: FontWeight.w900),
              ),
              const SizedBox(height: 8),
              const Text(
                'Ajusta el sonido y administra los recursos guardados en el dispositivo.',
              ),
              const SizedBox(height: 24),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(18),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Icon(
                            volume == 0
                                ? Icons.volume_off_rounded
                                : Icons.volume_up_rounded,
                            color: sage,
                          ),
                          const SizedBox(width: 12),
                          const Expanded(
                            child: Text(
                              'Volumen de la bocina',
                              style: TextStyle(
                                fontSize: 17,
                                fontWeight: FontWeight.w800,
                              ),
                            ),
                          ),
                          Text(
                            '${volume.round()} %',
                            style: const TextStyle(
                              color: orange,
                              fontWeight: FontWeight.w900,
                            ),
                          ),
                        ],
                      ),
                      Slider(
                        value: volume,
                        min: 0,
                        max: 100,
                        divisions: 20,
                        onChanged: canConfigure
                            ? (v) => setState(() => draftVolume = v)
                            : null,
                        onChangeEnd: canConfigure ? _commitVolume : null,
                      ),
                      Text(
                        canConfigure
                            ? 'El valor se guarda en el ESP32-S3.'
                            : 'Conecta el cubo y verifica que el audio esté disponible.',
                        style: TextStyle(
                          fontSize: 12,
                          color: paper.withValues(alpha: .7),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(18),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Row(
                        children: [
                          Icon(Icons.timer_outlined, color: orange),
                          SizedBox(width: 12),
                          Expanded(
                            child: Text(
                              'Duración máxima del audio',
                              style: TextStyle(
                                fontSize: 17,
                                fontWeight: FontWeight.w800,
                              ),
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 12),
                      DropdownButtonFormField<int>(
                        initialValue: widget.store.audioDurationSeconds,
                        decoration: const InputDecoration(
                          border: OutlineInputBorder(),
                          labelText: 'Duración de cada fragmento',
                        ),
                        items: audioDurationOptions
                            .map(
                              (seconds) => DropdownMenuItem(
                                value: seconds,
                                child: Text(_audioDurationLabel(seconds)),
                              ),
                            )
                            .toList(),
                        onChanged: (seconds) async {
                          if (seconds != null) {
                            await widget.store.saveAudioDuration(seconds);
                          }
                        },
                      ),
                      const SizedBox(height: 10),
                      Text(
                        'Se aplica al recortar nuevos audios. Los sonidos ya guardados no se modifican.',
                        style: TextStyle(
                          fontSize: 12,
                          color: paper.withValues(alpha: .7),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              Card(
                child: ListTile(
                  leading: const Icon(Icons.sync_rounded, color: sage),
                  title: const Text('Sincronizar imágenes desde el cubo'),
                  subtitle: const Text(
                    'Trae las caras modificadas y detecta qué emociones tienen sonido.',
                  ),
                  trailing: const Icon(Icons.chevron_right),
                  onTap: !ble.connected || !ble.sdReady || ble.busy
                      ? null
                      : widget.onSync,
                ),
              ),
              Card(
                child: ListTile(
                  leading: const Icon(Icons.restart_alt, color: orange),
                  title: const Text('Restablecer imágenes'),
                  subtitle: const Text(
                    'Vuelve a las 16 caras de Doc_microSD/default.',
                  ),
                  trailing: const Icon(Icons.chevron_right),
                  onTap: !ble.connected || !ble.sdReady || ble.busy
                      ? null
                      : widget.onRestore,
                ),
              ),
              Card(
                child: ListTile(
                  leading: const Icon(Icons.volume_off_outlined, color: coral),
                  title: const Text('Eliminar audios personalizados'),
                  subtitle: const Text(
                    'Borra los WAV guardados en Doc_microSD/user/audio.',
                  ),
                  trailing: const Icon(Icons.chevron_right),
                  onTap: !ble.connected || !ble.sdReady || ble.busy
                      ? null
                      : widget.onRemoveAudio,
                ),
              ),
              const SizedBox(height: 18),
              Text(ble.message, textAlign: TextAlign.center),
              if (ble.busy)
                const Padding(
                  padding: EdgeInsets.only(top: 12),
                  child: LinearProgressIndicator(),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

String _audioDurationLabel(int seconds) {
  if (seconds < 60) return '$seconds segundos';
  final minutes = seconds ~/ 60;
  return '$minutes ${minutes == 1 ? 'minuto' : 'minutos'}';
}

int _bitCount(int value) {
  var count = 0;
  while (value != 0) {
    count += value & 1;
    value >>= 1;
  }
  return count;
}
