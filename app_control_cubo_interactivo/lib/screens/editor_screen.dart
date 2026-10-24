import 'dart:async';

import 'package:audioplayers/audioplayers.dart';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:image_picker/image_picker.dart';

import '../main.dart';
import '../models/emotion.dart';
import '../services/artwork_store.dart';
import '../services/tft_codec.dart';
import '../services/wav_codec.dart';

class EditorScreen extends StatefulWidget {
  const EditorScreen({super.key, required this.index, required this.store});
  final int index;
  final ArtworkStore store;
  @override
  State<EditorScreen> createState() => _EditorScreenState();
}

class _EditorScreenState extends State<EditorScreen> {
  Uint8List? source, preview, audioSource;
  WavInfo? audioInfo;
  List<double>? waveform;
  bool cover = true, working = false, removeAudio = false, playing = false;
  double zoom = 1, audioStart = 0;
  String? audioName, error, workingMessage;
  final AudioPlayer player = AudioPlayer();
  StreamSubscription<void>? completeSubscription;

  @override
  void initState() {
    super.initState();
    completeSubscription = player.onPlayerComplete.listen((_) {
      if (mounted) setState(() => playing = false);
    });
  }

  @override
  void dispose() {
    completeSubscription?.cancel();
    player.dispose();
    super.dispose();
  }

  Future<void> _render() async {
    if (source == null) return;
    setState(() {
      working = true;
      error = null;
    });
    try {
      final result = await compute(prepareImage, {
        'bytes': source!,
        'cover': cover,
        'zoom': zoom,
      });
      if (mounted) setState(() => preview = result);
    } catch (e) {
      if (mounted) setState(() => error = e.toString());
    } finally {
      if (mounted) setState(() => working = false);
    }
  }

  Future<void> _pickImage() async {
    setState(() => working = true);
    try {
      await widget.store.beginPick(widget.index);
      final file = await ImagePicker().pickImage(
        source: ImageSource.gallery,
        maxWidth: 2048,
        maxHeight: 2048,
      );
      await widget.store.finishPick();
      if (file == null || !mounted) return;
      source = await file.readAsBytes();
      zoom = 1;
      await _render();
    } catch (_) {
      if (mounted) setState(() => error = 'No se pudo abrir la galería.');
    } finally {
      if (mounted) setState(() => working = false);
    }
  }

  Future<void> _pickAudio() async {
    setState(() {
      working = true;
      error = null;
    });
    try {
      final file = await FilePicker.pickFile(
        type: FileType.custom,
        allowedExtensions: const ['wav'],
      );
      if (file == null) return;
      final bytes = await file.readAsBytes();
      if (bytes.length > 30 * 1024 * 1024) {
        throw const FormatException('El WAV supera el límite de 30 MB.');
      }
      final info = inspectWav(bytes);
      if (mounted) {
        setState(() {
          audioSource = bytes;
          audioInfo = info;
          waveform = buildWaveform(bytes);
          audioName = file.name;
          audioStart = 0;
          removeAudio = false;
        });
      }
    } catch (e) {
      if (mounted) {
        setState(
          () => error = e is FormatException
              ? e.message
              : 'No se pudo abrir el audio WAV.',
        );
      }
    } finally {
      if (mounted) setState(() => working = false);
    }
  }

  Future<void> _previewAudio() async {
    if (playing) {
      await player.stop();
      if (mounted) setState(() => playing = false);
      return;
    }
    if (audioSource == null) return;
    setState(() {
      working = true;
      workingMessage = 'Preparando vista previa…';
      error = null;
    });
    try {
      final wav = await canonicalizeWavAsync({
        'bytes': audioSource!,
        'start': audioStart,
        'duration': widget.store.audioDurationSeconds,
      });
      await player.play(BytesSource(wav));
      if (mounted) setState(() => playing = true);
    } catch (e) {
      if (mounted) {
        setState(() => error = 'No se pudo reproducir el fragmento: $e');
      }
    } finally {
      if (mounted) {
        setState(() {
          working = false;
          workingMessage = null;
        });
      }
    }
  }

  Future<void> _save() async {
    setState(() {
      working = true;
      workingMessage = 'Preparando el audio…';
      error = null;
    });
    try {
      if (preview != null) await widget.store.save(widget.index, preview!);
      if (audioSource != null) {
        final wav = await canonicalizeWavAsync({
          'bytes': audioSource!,
          'start': audioStart,
          'duration': widget.store.audioDurationSeconds,
        });
        if (mounted) {
          setState(() => workingMessage = 'Guardando en el teléfono…');
        }
        await widget.store.saveAudio(widget.index, wav);
      } else if (removeAudio) {
        await widget.store.removeAudio(widget.index);
      }
      if (mounted) Navigator.pop(context);
    } catch (e) {
      if (mounted) setState(() => error = 'No se pudo guardar: $e');
    } finally {
      if (mounted) {
        setState(() {
          working = false;
          workingMessage = null;
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final current = preview ?? widget.store.custom[widget.index];
    final hasStoredAudio = widget.store.hasAudio(widget.index);
    final clipSeconds = widget.store.audioDurationSeconds;
    final maxStart = ((audioInfo?.durationSeconds ?? 0) - clipSeconds).clamp(
      0.0,
      double.infinity,
    );
    final changed = preview != null || audioSource != null || removeAudio;
    return Scaffold(
      appBar: AppBar(title: const Text('Imagen y sonido')),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.all(22),
          children: [
            Text(
              'ESTOY ${emotionLabels[widget.index].toUpperCase()}',
              style: const TextStyle(
                color: orange,
                fontWeight: FontWeight.w900,
                letterSpacing: 1,
                fontSize: 22,
              ),
            ),
            const SizedBox(height: 8),
            const Text(
              'Elige una imagen y, si deseas, un sonido para esta emoción.',
            ),
            const SizedBox(height: 20),
            Center(
              child: Container(
                width: 260,
                height: 260,
                padding: const EdgeInsets.all(8),
                decoration: BoxDecoration(
                  color: sage,
                  borderRadius: BorderRadius.circular(28),
                ),
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(20),
                  child: current == null
                      ? Image.asset(
                          'assets/emotions/${emotionIds[widget.index]}.png',
                          fit: BoxFit.contain,
                        )
                      : Image.memory(
                          current,
                          fit: BoxFit.contain,
                          gaplessPlayback: true,
                        ),
                ),
              ),
            ),
            const SizedBox(height: 14),
            OutlinedButton.icon(
              onPressed: working ? null : _pickImage,
              icon: const Icon(Icons.photo_library_outlined),
              label: const Text('Elegir imagen'),
            ),
            if (source != null) ...[
              const SizedBox(height: 12),
              SegmentedButton<bool>(
                segments: const [
                  ButtonSegment(value: true, label: Text('Llenar cuadro')),
                  ButtonSegment(value: false, label: Text('Imagen completa')),
                ],
                selected: {cover},
                onSelectionChanged: working
                    ? null
                    : (v) {
                        setState(() => cover = v.first);
                        _render();
                      },
              ),
              if (cover) ...[
                const SizedBox(height: 8),
                const Text('Acercar al centro'),
                Slider(
                  value: zoom,
                  min: 1,
                  max: 3,
                  label: '${zoom.toStringAsFixed(1)}×',
                  onChanged: working ? null : (v) => setState(() => zoom = v),
                  onChangeEnd: (_) => _render(),
                ),
              ],
            ],
            const Divider(height: 38),
            const Text(
              'SONIDO DE LA EMOCIÓN',
              style: TextStyle(fontWeight: FontWeight.w900, letterSpacing: 1),
            ),
            const SizedBox(height: 8),
            Text(
              audioSource != null
                  ? '$audioName · ${audioInfo!.durationSeconds.toStringAsFixed(1)} s'
                  : hasStoredAudio && !removeAudio
                  ? 'Hay un audio personalizado guardado.'
                  : 'Sin audio personalizado.',
              style: const TextStyle(fontSize: 13),
            ),
            const SizedBox(height: 10),
            OutlinedButton.icon(
              onPressed: working ? null : _pickAudio,
              icon: const Icon(Icons.audio_file_outlined),
              label: const Text('Seleccionar audio WAV'),
            ),
            if (audioSource != null) ...[
              const SizedBox(height: 10),
              SizedBox(
                height: 92,
                child: CustomPaint(
                  painter: _WaveformPainter(
                    waveform!,
                    startFraction: audioStart / audioInfo!.durationSeconds,
                    endFraction:
                        (audioStart + clipSeconds).clamp(
                          0,
                          audioInfo!.durationSeconds,
                        ) /
                        audioInfo!.durationSeconds,
                  ),
                ),
              ),
              const SizedBox(height: 8),
              Text(
                'Inicio: ${audioStart.toStringAsFixed(1)} s · fragmento de hasta ${_audioDurationLabel(clipSeconds)}',
              ),
              Slider(
                value: audioStart.clamp(0, maxStart),
                min: 0,
                max: maxStart == 0 ? 1 : maxStart,
                onChanged: maxStart == 0 || working
                    ? null
                    : (v) {
                        if (playing) {
                          player.stop();
                          playing = false;
                        }
                        setState(() => audioStart = v);
                      },
              ),
              FilledButton.tonalIcon(
                onPressed: working ? null : _previewAudio,
                icon: Icon(
                  playing
                      ? Icons.stop_circle_outlined
                      : Icons.play_circle_outline,
                ),
                label: Text(
                  playing
                      ? 'Detener vista previa'
                      : 'Escuchar ${_audioDurationLabel(clipSeconds)}',
                ),
              ),
              const Text(
                'La app lo convertirá a WAV PCM mono de 16 kHz para el cubo.',
                style: TextStyle(fontSize: 12),
              ),
            ],
            if (hasStoredAudio || audioSource != null)
              TextButton.icon(
                onPressed: working
                    ? null
                    : () => setState(() {
                        audioSource = null;
                        audioInfo = null;
                        waveform = null;
                        audioName = null;
                        removeAudio = true;
                      }),
                icon: const Icon(Icons.volume_off_outlined),
                label: const Text('Quitar sonido de esta emoción'),
              ),
            if (working)
              Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  children: [
                    const LinearProgressIndicator(),
                    if (workingMessage != null) ...[
                      const SizedBox(height: 8),
                      Text(
                        workingMessage!,
                        style: const TextStyle(fontSize: 12),
                      ),
                    ],
                  ],
                ),
              ),
            if (error != null)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 12),
                child: Text(
                  error!,
                  style: TextStyle(color: Theme.of(context).colorScheme.error),
                ),
              ),
            const SizedBox(height: 16),
            FilledButton(
              onPressed: !changed || working || error != null ? null : _save,
              child: const Text('Guardar cambios'),
            ),
            const SizedBox(height: 10),
            const Text(
              'Los cambios quedan en el teléfono. Al pulsar «Mostrar», se sincronizan con Doc_microSD/user y luego se muestran o reproducen.',
              style: TextStyle(fontSize: 12, height: 1.5),
            ),
          ],
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

class _WaveformPainter extends CustomPainter {
  const _WaveformPainter(
    this.values, {
    required this.startFraction,
    required this.endFraction,
  });
  final List<double> values;
  final double startFraction, endFraction;

  @override
  void paint(Canvas canvas, Size size) {
    final background = Paint()..color = paper.withValues(alpha: .28);
    final selected = Paint()..color = sage;
    final center = size.height / 2;
    final width = size.width / values.length;
    for (var i = 0; i < values.length; i++) {
      final fraction = (i + .5) / values.length;
      final height = values[i] * (size.height - 12);
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromLTWH(i * width + 1, center - height / 2, width - 2, height),
          const Radius.circular(2),
        ),
        fraction >= startFraction && fraction <= endFraction
            ? selected
            : background,
      );
    }
    final border = Paint()
      ..color = orange
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2;
    canvas.drawRect(
      Rect.fromLTRB(
        startFraction * size.width,
        2,
        endFraction * size.width,
        size.height - 2,
      ),
      border,
    );
  }

  @override
  bool shouldRepaint(covariant _WaveformPainter oldDelegate) =>
      oldDelegate.startFraction != startFraction ||
      oldDelegate.endFraction != endFraction ||
      oldDelegate.values != values;
}
