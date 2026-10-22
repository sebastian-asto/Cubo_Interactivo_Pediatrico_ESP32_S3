import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart';
import 'package:image_picker/image_picker.dart';
import '../main.dart';
import '../models/emotion.dart';
import '../services/artwork_store.dart';
import '../services/tft_codec.dart';

class EditorScreen extends StatefulWidget {
  const EditorScreen({super.key, required this.index, required this.store});
  final int index;
  final ArtworkStore store;
  @override
  State<EditorScreen> createState() => _EditorScreenState();
}

class _EditorScreenState extends State<EditorScreen> {
  Uint8List? source, preview;
  bool cover = true, working = false;
  double zoom = 1;
  String? error;
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
      if (mounted) {
        setState(() {
          preview = result;
          error = null;
        });
      }
    } catch (e) {
      if (mounted) setState(() => error = e.toString());
    } finally {
      if (mounted) setState(() => working = false);
    }
  }

  Future<void> _pick() async {
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
      if (!mounted) return;
      await _render();
    } catch (e) {
      if (mounted) {
        setState(
          () => error = 'No se pudo abrir la galería. Vuelve a intentarlo.',
        );
      }
    } finally {
      if (mounted) setState(() => working = false);
    }
  }

  Future<void> _save() async {
    setState(() => working = true);
    try {
      await widget.store.save(widget.index, preview!);
      if (mounted) Navigator.pop(context);
    } catch (e) {
      if (mounted) {
        setState(() {
          error = 'No se pudo guardar: $e';
          working = false;
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final current = preview ?? widget.store.custom[widget.index];
    return Scaffold(
      appBar: AppBar(title: const Text('Dale tu propio toque')),
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
            const Text('Elige una imagen que represente esta emoción para ti.'),
            const SizedBox(height: 22),
            Center(
              child: Container(
                width: 280,
                height: 280,
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
            const SizedBox(height: 10),
            const Text(
              'Vista previa de la pantalla del cubo',
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 12),
            ),
            const SizedBox(height: 24),
            OutlinedButton.icon(
              onPressed: working ? null : _pick,
              icon: const Icon(Icons.photo_library_outlined),
              label: const Text('Elegir de mi galería'),
            ),
            if (source != null) ...[
              const SizedBox(height: 18),
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
                const SizedBox(height: 14),
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
            if (working)
              const Padding(
                padding: EdgeInsets.all(16),
                child: LinearProgressIndicator(),
              ),
            if (error != null)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 12),
                child: Text(
                  error!,
                  style: TextStyle(color: Theme.of(context).colorScheme.error),
                ),
              ),
            const SizedBox(height: 20),
            FilledButton(
              onPressed: preview == null || working || error != null
                  ? null
                  : _save,
              child: const Text('Usar esta imagen'),
            ),
            const SizedBox(height: 10),
            const Text(
              'Se guardará en este teléfono. Después pulsa «Mostrar» para enviarla y reemplazar esa emoción en el cubo.',
              style: TextStyle(fontSize: 12, height: 1.5),
            ),
          ],
        ),
      ),
    );
  }
}
