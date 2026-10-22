import 'dart:async';
import 'package:flutter/material.dart';
import 'package:permission_handler/permission_handler.dart';
import '../main.dart';
import '../services/cube_ble.dart';

class ScannerScreen extends StatefulWidget {
  const ScannerScreen({super.key, required this.ble});
  final CubeBle ble;
  @override
  State<ScannerScreen> createState() => _ScannerScreenState();
}

class _ScannerScreenState extends State<ScannerScreen> {
  @override
  void dispose() {
    unawaited(widget.ble.stopScan());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: widget.ble,
    builder: (context, _) {
      final ble = widget.ble;
      return Scaffold(
        appBar: AppBar(title: const Text('Conecta tu cubo')),
        body: SafeArea(
          child: ListView(
            padding: const EdgeInsets.all(22),
            children: [
              Container(
                padding: const EdgeInsets.all(24),
                decoration: BoxDecoration(
                  color: sage,
                  borderRadius: BorderRadius.circular(28),
                ),
                child: const Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        Text(
                          'CERCA DE TI',
                          style: TextStyle(
                            color: ink,
                            fontSize: 11,
                            letterSpacing: 2,
                            fontWeight: FontWeight.w900,
                          ),
                        ),
                        Icon(Icons.bluetooth_searching, color: ink, size: 42),
                      ],
                    ),
                    SizedBox(height: 20),
                    Text(
                      'Encuentra.\nConecta. Expresa.',
                      style: TextStyle(
                        color: ink,
                        fontSize: 34,
                        fontWeight: FontWeight.w900,
                        height: 1.1,
                      ),
                    ),
                    SizedBox(height: 14),
                    Text(
                      'Enciende el cubo y mantenlo cerca de tu teléfono.',
                      style: TextStyle(color: ink),
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 24),
              FilledButton.icon(
                onPressed: ble.scanning
                    ? ble.stopScan
                    : ble.connecting || ble.connected
                    ? null
                    : ble.scan,
                icon: Icon(ble.scanning ? Icons.stop : Icons.radar),
                label: Text(
                  ble.scanning ? 'Detener búsqueda' : 'Buscar Cubo-ESP',
                ),
              ),
              const SizedBox(height: 12),
              if (ble.scanning) ...[
                const LinearProgressIndicator(),
                const SizedBox(height: 12),
                const Text('Buscando cubos cercanos durante 15 segundos…'),
              ],
              if (ble.connecting)
                const Padding(
                  padding: EdgeInsets.all(20),
                  child: Center(child: CircularProgressIndicator()),
                ),
              if (ble.error != null)
                Container(
                  margin: const EdgeInsets.symmetric(vertical: 12),
                  padding: const EdgeInsets.all(16),
                  decoration: BoxDecoration(
                    color: coral.withValues(alpha: .22),
                    borderRadius: BorderRadius.circular(16),
                  ),
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
              if (ble.connected)
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(18),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Icon(
                          Icons.bluetooth_connected,
                          color: sage,
                          size: 30,
                        ),
                        const SizedBox(height: 10),
                        const Text(
                          'Cubo-ESP',
                          style: TextStyle(
                            fontSize: 23,
                            fontWeight: FontWeight.w900,
                          ),
                        ),
                        Text(
                          ble.sdReady
                              ? 'Conectado · microSD lista'
                              : 'Conectado · revisa la microSD',
                        ),
                        const SizedBox(height: 16),
                        FilledButton(
                          onPressed: () => Navigator.pop(context),
                          child: const Text('Elegir una emoción'),
                        ),
                        TextButton(
                          onPressed: ble.disconnect,
                          child: const Text('Desconectar'),
                        ),
                      ],
                    ),
                  ),
                )
              else
                ...ble.devices.values.map(
                  (d) => Card(
                    child: ListTile(
                      contentPadding: const EdgeInsets.all(14),
                      leading: const Icon(
                        Icons.view_in_ar,
                        color: orange,
                        size: 30,
                      ),
                      title: Text(d.name.isEmpty ? 'Cubo-ESP' : d.name),
                      subtitle: Text('${d.id}\nSeñal: ${d.rssi} dBm'),
                      isThreeLine: true,
                      trailing: const Icon(Icons.arrow_forward),
                      onTap: ble.connecting ? null : () => ble.connect(d),
                    ),
                  ),
                ),
              if (!ble.scanning && !ble.connected && ble.devices.isEmpty)
                const Padding(
                  padding: EdgeInsets.symmetric(vertical: 24),
                  child: Text(
                    'Aquí aparecerán los cubos disponibles. Si no encuentras el tuyo, comprueba que esté encendido y desconectado de otro teléfono.',
                  ),
                ),
              const SizedBox(height: 12),
              const Text(
                'La primera conexión puede pedir permiso para emparejar. Acepta la solicitud del sistema.',
                style: TextStyle(fontSize: 12, color: paper),
              ),
            ],
          ),
        ),
      );
    },
  );
}
