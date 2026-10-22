import 'package:flutter/material.dart';
import 'package:image/image.dart' as img;
// Hardware test: requires Cubo-ESP and factory artwork on its SD.
// Restores the original 16 images after testing. Run only on a development cube.
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:app_control_cubo_interactivo/main.dart';
import 'package:app_control_cubo_interactivo/services/artwork_store.dart';
import 'package:app_control_cubo_interactivo/services/cube_ble.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  testWidgets(
    'BLE real: buscar, mostrar, cancelar, transferir y restaurar',
    (tester) async {
      final ble = CubeBle();
      final store = ArtworkStore();

      await tester.pumpWidget(CubeApp(store: store, ble: ble));
      await tester.runAsync(() async {
        debugPrint('HARDWARE: scan');
        await ble.scan();
        final deadline = DateTime.now().add(const Duration(seconds: 18));
        while (ble.devices.isEmpty && DateTime.now().isBefore(deadline)) {
          await Future<void>.delayed(const Duration(milliseconds: 250));
        }
        expect(
          ble.devices.isNotEmpty,
          true,
          reason: ble.error ?? 'Cubo no encontrado',
        );
        final device = ble.devices.values.firstWhere(
          (d) => d.id.toLowerCase() == '80:45:6b:35:b1:fe',
        );
        debugPrint('HARDWARE: connect');
        await ble.connect(device);
        expect(ble.connected, true, reason: ble.error);
        expect(ble.sdReady, true);
        await Future<void>.delayed(const Duration(seconds: 3));
        try {
          debugPrint('HARDWARE: show factory');
          await ble.show(6, null);
          expect(ble.message, 'Imagen confirmada en el cubo');
          final data = await rootBundle.load('assets/emotions/excited.png');
          final originalPng = data.buffer.asUint8List(
            data.offsetInBytes,
            data.lengthInBytes,
          );
          // Change pixels so restore must actually rewrite the factory file.
          final edited = img.decodePng(originalPng)!;
          img.fillRect(
            edited,
            x1: 8,
            y1: 8,
            x2: 35,
            y2: 35,
            color: img.ColorRgb8(255, 0, 255),
          );
          final png = img.encodePng(edited);
          void cancel() {
            if (ble.progress > .1 && ble.progress < 1) ble.cancelUpload();
          }

          ble.addListener(cancel);
          await expectLater(ble.show(6, png), throwsStateError);
          ble.removeListener(cancel);
          debugPrint('HARDWARE: upload complete image');
          await ble.show(6, png);
          expect(ble.progress, 1);
          expect(ble.message, 'Imagen confirmada en el cubo');
          debugPrint('HARDWARE: restore all');
          await ble.restore();
          expect(ble.message, 'Las 16 imágenes originales están listas');
          // Reconnect proves advertising resumes and the service is reusable.
          await ble.disconnect();
          await Future<void>.delayed(const Duration(seconds: 1));
          debugPrint('HARDWARE: connect');
          await ble.connect(device);
          expect(ble.connected, true, reason: ble.error);
          await Future<void>.delayed(const Duration(seconds: 2));
          await ble.show(0, null);
        } finally {
          await ble.disconnect();
          ble.dispose();
        }
      });
      await tester.pumpWidget(const SizedBoxForTest());
    },
    timeout: const Timeout(Duration(minutes: 6)),
  );
}

// Avoid retaining the BLE notifier after the test.
class SizedBoxForTest extends StatelessWidget {
  const SizedBoxForTest({super.key});
  @override
  Widget build(BuildContext context) => const SizedBox.shrink();
}
