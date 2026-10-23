import 'package:flutter/material.dart';
import '../main.dart';
import '../models/emotion.dart';
import '../services/artwork_store.dart';
import '../services/cube_ble.dart';
import 'editor_screen.dart';

class EmotionsScreen extends StatefulWidget {
  const EmotionsScreen({
    super.key,
    required this.store,
    required this.ble,
    this.startupError,
    this.onDevice,
  });
  final ArtworkStore store;
  final CubeBle ble;
  final String? startupError;
  final VoidCallback? onDevice;
  @override
  State<EmotionsScreen> createState() => _EmotionsScreenState();
}

class _EmotionsScreenState extends State<EmotionsScreen> {
  int selected = 0;
  final scroll = ScrollController();
  @override
  void initState() {
    super.initState();
    if (widget.startupError != null) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) _notice(widget.startupError!);
      });
    }
  }

  @override
  void dispose() {
    scroll.dispose();
    super.dispose();
  }

  void _notice(String text) => ScaffoldMessenger.of(context).showSnackBar(
    SnackBar(content: Text(text), behavior: SnackBarBehavior.floating),
  );
  Future<void> _show() async {
    try {
      final removingAudio = widget.store.audioRemoved.contains(selected);
      await widget.ble.show(
        selected,
        widget.store.custom[selected],
        widget.store.audio[selected],
        removeAudio: removingAudio,
      );
      if (removingAudio) await widget.store.confirmAudioRemoval(selected);
      if (mounted) {
        _notice(
          'El cubo está mostrando: ${emotionLabels[selected].toLowerCase()}.',
        );
      }
    } catch (e) {
      if (mounted) _notice(userError(e));
    }
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: Listenable.merge([widget.store, widget.ble]),
    builder: (context, _) {
      final ble = widget.ble;
      return Scaffold(
        body: SafeArea(
          child: _PageViewport(
            child: Center(
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 680),
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(18, 12, 18, 12),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Row(
                        children: [
                          const Icon(Icons.view_in_ar_rounded, color: orange),
                          const SizedBox(width: 8),
                          const Expanded(
                            child: Text(
                              'CUBO / EMOCIONES',
                              style: TextStyle(
                                fontWeight: FontWeight.w900,
                                letterSpacing: 1.4,
                                fontSize: 12,
                              ),
                            ),
                          ),

                          IconButton(
                            tooltip: 'Buscar y conectar cubo',
                            onPressed: ble.busy ? null : widget.onDevice,
                            icon: Icon(
                              ble.connected
                                  ? Icons.bluetooth_connected
                                  : Icons.bluetooth,
                              color: ble.connected ? sage : paper,
                            ),
                          ),
                        ],
                      ),
                      Container(
                        padding: const EdgeInsets.all(18),
                        decoration: BoxDecoration(
                          color: orange,
                          borderRadius: BorderRadius.circular(24),
                        ),
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  const Text(
                                    'UN CUBO, MUCHAS FORMAS DE SENTIR',
                                    style: TextStyle(
                                      color: ink,
                                      fontSize: 9,
                                      fontWeight: FontWeight.w800,
                                      letterSpacing: 1,
                                    ),
                                  ),
                                  const SizedBox(height: 8),
                                  const Text(
                                    '¿Cómo te\nsientes hoy?',
                                    style: TextStyle(
                                      color: ink,
                                      fontSize: 32,
                                      height: 1.04,
                                      fontWeight: FontWeight.w900,
                                      letterSpacing: -1.3,
                                    ),
                                  ),
                                  const SizedBox(height: 10),
                                  Text(
                                    'Elige una cara. Dale tu propio toque.',
                                    style: TextStyle(
                                      color: ink.withValues(alpha: .8),
                                      fontSize: 12,
                                    ),
                                  ),
                                ],
                              ),
                            ),
                            const Icon(Icons.north_east, size: 54, color: ink),
                          ],
                        ),
                      ),
                      const SizedBox(height: 12),
                      Row(
                        children: [
                          Container(
                            width: 7,
                            height: 7,
                            decoration: BoxDecoration(
                              color: ble.connected ? sage : coral,
                              shape: BoxShape.circle,
                            ),
                          ),
                          const SizedBox(width: 7),
                          Expanded(
                            child: Text(
                              ble.connected
                                  ? (ble.sdReady
                                        ? 'Cubo-ESP conectado'
                                        : 'Cubo sin microSD')
                                  : 'Tu cubo está desconectado',
                              style: const TextStyle(
                                fontSize: 12,
                                color: paper,
                              ),
                            ),
                          ),
                          TextButton(
                            onPressed: ble.busy ? null : widget.onDevice,
                            child: Text(
                              ble.connected ? 'Ver cubo' : 'Conectar ↗',
                            ),
                          ),
                        ],
                      ),
                      Padding(
                        padding: const EdgeInsets.only(bottom: 10),
                        child: Wrap(
                          alignment: WrapAlignment.spaceBetween,
                          spacing: 12,
                          runSpacing: 4,
                          children: [
                            const Text(
                              'EXPLORA TUS EMOCIONES',
                              style: TextStyle(
                                fontSize: 10,
                                fontWeight: FontWeight.w800,
                                letterSpacing: 1,
                              ),
                            ),

                            Text(
                              '16 emociones · 1 selección',
                              style: TextStyle(
                                fontSize: 10,
                                color: paper.withValues(alpha: .6),
                              ),
                            ),
                          ],
                        ),
                      ),
                      Expanded(
                        child: Scrollbar(
                          controller: scroll,
                          thumbVisibility: true,
                          child: GridView.builder(
                            controller: scroll,
                            padding: const EdgeInsets.only(right: 8, bottom: 8),
                            gridDelegate:
                                const SliverGridDelegateWithFixedCrossAxisCount(
                                  crossAxisCount: 3,
                                  crossAxisSpacing: 10,
                                  mainAxisSpacing: 10,
                                  childAspectRatio: .84,
                                ),
                            itemCount: emotionIds.length,
                            itemBuilder: (context, i) {
                              final active = selected == i,
                                  custom = widget.store.custom[i];
                              final hasAudio = widget.store.hasAudio(i);
                              return Semantics(
                                selected: active,
                                label:
                                    'Estoy ${emotionLabels[i].toLowerCase()}',
                                button: true,
                                child: InkWell(
                                  key: ValueKey('emotion-$i'),
                                  onTap: ble.busy
                                      ? null
                                      : () => setState(() => selected = i),
                                  borderRadius: BorderRadius.circular(18),
                                  child: AnimatedContainer(
                                    duration: const Duration(milliseconds: 160),
                                    padding: const EdgeInsets.all(5),
                                    decoration: BoxDecoration(
                                      color: active
                                          ? sage
                                          : const Color(0xFF2A2E27),
                                      borderRadius: BorderRadius.circular(18),
                                      border: Border.all(
                                        color: active
                                            ? sage
                                            : const Color(0xFF3E4439),
                                        width: 2,
                                      ),
                                    ),
                                    child: Column(
                                      children: [
                                        Expanded(
                                          child: Stack(
                                            fit: StackFit.expand,
                                            children: [
                                              ClipRRect(
                                                borderRadius:
                                                    BorderRadius.circular(11),
                                                child: custom != null
                                                    ? Image.memory(
                                                        custom,
                                                        fit: BoxFit.cover,
                                                        errorBuilder:
                                                            (
                                                              _,
                                                              e,
                                                              s,
                                                            ) => const Icon(
                                                              Icons
                                                                  .broken_image,
                                                            ),
                                                      )
                                                    : Image.asset(
                                                        'assets/emotions/${emotionIds[i]}.png',
                                                        fit: BoxFit.cover,
                                                      ),
                                              ),
                                              Positioned(
                                                top: 3,
                                                right: 3,
                                                child: Container(
                                                  decoration: BoxDecoration(
                                                    shape: BoxShape.circle,
                                                    color: active
                                                        ? ink
                                                        : Colors.black54,
                                                  ),
                                                  padding: const EdgeInsets.all(
                                                    3,
                                                  ),
                                                  child: Icon(
                                                    active
                                                        ? Icons.check
                                                        : Icons.circle_outlined,
                                                    size: 13,
                                                    color: active
                                                        ? sage
                                                        : paper,
                                                  ),
                                                ),
                                              ),
                                              if (custom != null || hasAudio)
                                                Positioned(
                                                  bottom: 4,
                                                  left: 4,
                                                  child: Container(
                                                    padding:
                                                        const EdgeInsets.symmetric(
                                                          horizontal: 4,
                                                          vertical: 3,
                                                        ),
                                                    decoration: BoxDecoration(
                                                      color: Colors.black54,
                                                      borderRadius:
                                                          BorderRadius.circular(
                                                            7,
                                                          ),
                                                    ),
                                                    child: Row(
                                                      mainAxisSize:
                                                          MainAxisSize.min,
                                                      children: [
                                                        if (custom != null)
                                                          const Icon(
                                                            Icons.photo,
                                                            size: 14,
                                                            color: Colors.white,
                                                          ),
                                                        if (custom != null &&
                                                            hasAudio)
                                                          const SizedBox(
                                                            width: 3,
                                                          ),
                                                        if (hasAudio)
                                                          const Icon(
                                                            Icons.music_note,
                                                            size: 14,
                                                            color: orange,
                                                          ),
                                                      ],
                                                    ),
                                                  ),
                                                ),
                                            ],
                                          ),
                                        ),
                                        const SizedBox(height: 5),
                                        Text(
                                          emotionLabels[i],
                                          maxLines: 1,
                                          overflow: TextOverflow.ellipsis,
                                          style: TextStyle(
                                            fontSize: 11,
                                            fontWeight: FontWeight.w800,
                                            color: active ? ink : paper,
                                          ),
                                        ),
                                      ],
                                    ),
                                  ),
                                ),
                              );
                            },
                          ),
                        ),
                      ),
                      const SizedBox(height: 10),
                      Row(
                        children: [
                          const Text(
                            'SELECCIONADA',
                            style: TextStyle(
                              fontSize: 9,
                              letterSpacing: 1,
                              color: sage,
                            ),
                          ),
                          const SizedBox(width: 8),
                          Expanded(
                            child: Text(
                              'Estoy ${emotionLabels[selected].toLowerCase()}',
                              style: const TextStyle(
                                fontWeight: FontWeight.w700,
                                fontSize: 13,
                              ),
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 10),
                      Row(
                        children: [
                          Expanded(
                            child: FilledButton.icon(
                              onPressed:
                                  ble.busy || !ble.connected || !ble.sdReady
                                  ? null
                                  : _show,
                              icon: const Icon(Icons.play_arrow_rounded),
                              label: const Text('Mostrar'),
                              style: FilledButton.styleFrom(
                                backgroundColor: coral,
                                foregroundColor: Colors.white,
                              ),
                            ),
                          ),
                          const SizedBox(width: 10),
                          Expanded(
                            child: FilledButton.icon(
                              onPressed: ble.busy
                                  ? null
                                  : () => Navigator.push(
                                      context,
                                      MaterialPageRoute<void>(
                                        builder: (_) => EditorScreen(
                                          index: selected,
                                          store: widget.store,
                                        ),
                                      ),
                                    ),
                              icon: const Icon(Icons.tune_rounded),
                              label: const Text('Editar'),
                              style: FilledButton.styleFrom(
                                backgroundColor: sage,
                                foregroundColor: ink,
                              ),
                            ),
                          ),
                        ],
                      ),
                      if (ble.busy) ...[
                        const SizedBox(height: 8),
                        LinearProgressIndicator(
                          value: ble.progress > 0 && ble.progress < 1
                              ? ble.progress
                              : null,
                        ),
                        const SizedBox(height: 5),
                        Text(ble.message, style: const TextStyle(fontSize: 11)),
                        if (ble.progress > 0 && ble.progress < 1)
                          TextButton(
                            onPressed: ble.cancelUpload,
                            child: const Text('Cancelar envío'),
                          ),
                      ] else
                        Padding(
                          padding: const EdgeInsets.only(top: 8),
                          child: Text(
                            ble.message,
                            textAlign: TextAlign.center,
                            style: TextStyle(
                              fontSize: 10,
                              color: paper.withValues(alpha: .65),
                            ),
                          ),
                        ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ),
      );
    },
  );
}

class _PageViewport extends StatelessWidget {
  const _PageViewport({required this.child});
  final Widget child;
  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, c) {
      final minimum =
          740 *
          MediaQuery.textScalerOf(context).scale(1).clamp(1.0, 2.0).toDouble();
      return SingleChildScrollView(
        child: SizedBox(
          height: c.maxHeight < minimum ? minimum : c.maxHeight,
          child: child,
        ),
      );
    },
  );
}
