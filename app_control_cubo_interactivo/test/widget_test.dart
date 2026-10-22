import 'dart:io';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:app_control_cubo_interactivo/main.dart';
import 'package:app_control_cubo_interactivo/services/artwork_store.dart';
import 'package:app_control_cubo_interactivo/services/cube_ble.dart';

void main() {
  testWidgets('Una sola emoción seleccionada y acciones sin conexión', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final ble = CubeBle();
    final store = ArtworkStore();
    await tester.pumpWidget(CubeApp(store: store, ble: ble));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Emociones'));
    await tester.pumpAndSettle();
    expect(find.text('Estoy feliz'), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('emotion-1')));
    await tester.pumpAndSettle();
    expect(find.text('Estoy triste'), findsOneWidget);
    expect(find.text('Estoy feliz'), findsNothing);
    final button = tester.widget<FilledButton>(
      find.widgetWithText(FilledButton, 'Mostrar'),
    );
    expect(button.onPressed, isNull);
    await tester.tap(find.text('Editar'));
    await tester.pumpAndSettle();
    expect(find.text('ESTOY TRISTE'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
  testWidgets('Vista móvil, sin desbordamientos y captura', (tester) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final key = GlobalKey();
    await tester.pumpWidget(
      RepaintBoundary(
        key: key,
        child: CubeApp(store: ArtworkStore(), ble: CubeBle()),
      ),
    );
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    final boundary =
        key.currentContext!.findRenderObject()! as RenderRepaintBoundary;
    await tester.runAsync(() async {
      final image = await boundary.toImage(pixelRatio: 2);
      final data = await image.toByteData(format: ui.ImageByteFormat.png);
      await Directory('build/previews').create(recursive: true);
      await File(
        'build/previews/emociones.png',
      ).writeAsBytes(data!.buffer.asUint8List());
      image.dispose();
    });
    tester.view.physicalSize = const Size(320, 568);
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    await tester.tap(find.text('Emociones'));
    await tester.pumpAndSettle();
    expect(find.text('CUBO / EMOCIONES'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
