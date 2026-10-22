import 'package:flutter/material.dart';
import 'services/artwork_store.dart';
import 'services/cube_ble.dart';
import 'screens/emotions_screen.dart';

const ink = Color(0xFF171916),
    orange = Color(0xFFE5A05D),
    sage = Color(0xFF87A27E),
    coral = Color(0xFFBE6053),
    paper = Color(0xFFEDE8DE);
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final store = ArtworkStore();
  String? error;
  try {
    await store.load();
  } catch (_) {
    error = 'No se pudieron recuperar las imágenes guardadas en el teléfono.';
  }
  runApp(CubeApp(store: store, ble: CubeBle(), startupError: error));
}

class CubeApp extends StatelessWidget {
  const CubeApp({
    super.key,
    required this.store,
    required this.ble,
    this.startupError,
  });
  final ArtworkStore store;
  final CubeBle ble;
  final String? startupError;
  @override
  Widget build(BuildContext context) => MaterialApp(
    debugShowCheckedModeBanner: false,
    title: 'Cubo · Emociones',
    theme: ThemeData(
      useMaterial3: true,
      brightness: Brightness.dark,
      scaffoldBackgroundColor: ink,
      colorScheme: const ColorScheme.dark(
        primary: orange,
        secondary: sage,
        surface: Color(0xFF252821),
        onSurface: paper,
        error: Color(0xFFFF9E8E),
      ),
      appBarTheme: const AppBarTheme(
        backgroundColor: ink,
        foregroundColor: paper,
        centerTitle: false,
      ),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          minimumSize: const Size(48, 54),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(16),
          ),
          textStyle: const TextStyle(fontSize: 15, fontWeight: FontWeight.w800),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          minimumSize: const Size(48, 50),
          foregroundColor: paper,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(16),
          ),
        ),
      ),
    ),
    home: EmotionsScreen(store: store, ble: ble, startupError: startupError),
  );
}
