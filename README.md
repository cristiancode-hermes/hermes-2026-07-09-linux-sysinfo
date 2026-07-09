# linux-sysinfo

Herramienta de información del sistema Linux que lee datos de `/proc`, `sysfs`, usa `mmap` para archivos regulares e `ioctl` para consultas de terminal.

## Features

- **CPU** — modelo, vendor, frecuencia MHz, caché, núcleos e hilos
- **Memoria** — total, libre, disponible, buffers, caché, swap + uptime y load average
- **Discos** — dispositivos de bloque (nombre, tamaño, tipo, solo lectura, extraíble)
- **Red** — interfaces de red (nombre, MAC, estado operativo, MTU, velocidad/duplex)
- **Procesos** — snapshot de procesos activos (PID, nombre, estado, CPU ticks, RSS)
- **Terminal** — tamaño de terminal vía ioctl TIOCGWINSZ (filas, columnas, píxeles)
- **mmap** — lector RAII para archivos regulares con fallback a `read()` para /proc
- **JSON output** — toda la salida es JSON estructurado (formateado o compacto)
- **Multi-formato** — `--json` (defecto) o `--text` plano

## Stack

| Componente | Tecnología |
|-----------|-----------|
| Lenguaje | C++23 (`std::print`, `std::expected`, `std::format`, ranges, string_view) |
| Build | CMake 3.28+ con presets (Ninja) |
| Dependencias | FetchContent (header-only: nlohmann-json, spdlog, Catch2) |
| Docker | multi-stage: gcc:14 → debian:bookworm-slim |
| Tests | Catch2 v3 (unit + integración) |
| GitHub | Push automático vía GIT_ASKPASS |

## Requisitos

- **Sistema operativo**: Linux (kernel 5.x+)
- **Compilador**: GCC 14 o Clang 18+
- **CMake**: 3.28+
- **Ninja**: opcional (recomendado para builds rápidos)

## Compilación

### Con CMake presets (recomendado)

```bash
# Debug con sanitizers
cmake --preset debug
cmake --build --preset debug
./build/debug/linux-sysinfo --all

# Release con LTO
cmake --preset release
cmake --build --preset release
./build/release/linux-sysinfo --cpu --memory
```

### Sin presets

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/linux-sysinfo --help
```

### Tests

```bash
cmake --preset debug
cmake --build --preset debug
cd build/debug && ctest --output-on-failure -V
```

### Docker

```bash
docker build -t linux-sysinfo .
docker run --rm linux-sysinfo --all
# Para acceder al hardware real del host:
docker run --rm --pid=host --privileged -v /sys:/sys:ro linux-sysinfo --all
```

## Uso

```
linux-sysinfo --all                    # Toda la información (defecto)
linux-sysinfo --cpu --memory           # Solo CPU y memoria
linux-sysinfo --disks --network        # Discos y red
linux-sysinfo --all --text             # Salida en texto plano
linux-sysinfo --processes --pretty     # Procesos como JSON formateado
linux-sysinfo --help                   # Ayuda completa
```

### Flags

| Flag | Descripción |
|------|-----------|
| `--all, -a` | Todas las categorías |
| `--cpu` | Información de CPU |
| `--memory, --mem` | Información de memoria |
| `--disks, --disk` | Dispositivos de bloque |
| `--network, --net` | Interfaces de red |
| `--processes, --procs` | Lista de procesos |
| `--terminal, --tty` | Tamaño de terminal |
| `--json` | Salida como JSON (defecto) |
| `--pretty` | JSON formateado (defecto) |
| `--text` | Salida como texto plano |
| `--help, -h` | Ayuda |

## Ejemplo de salida

```json
{
  "cpu": [
    {
      "processor": 0,
      "vendor": "GenuineIntel",
      "model": "Intel(R) Core(TM) i7-8700K CPU @ 3.70GHz",
      "mhz": 3700.0,
      "cache_kb": 12288,
      "cores": 6,
      "siblings": 12
    }
  ],
  "memory": {
    "total_kb": 16326484,
    "free_kb": 8273844,
    "available_kb": 10352568,
    "used_kb": 5973916,
    "swap_total_kb": 2097148,
    "swap_free_kb": 2097148,
    "uptime_secs": 1234567.89,
    "load_1m": 0.45,
    "load_5m": 0.52,
    "load_15m": 0.48
  },
  "disks": [
    {
      "name": "sda",
      "type": "disk",
      "size_sectors": 234441648,
      "size_bytes": 120034123776,
      "size_gb": 120.0,
      "read_only": false,
      "removable": false
    }
  ],
  "network": [
    {
      "name": "lo",
      "mac": "00:00:00:00:00:00",
      "state": "unknown",
      "mtu": 65536
    },
    {
      "name": "eth0",
      "mac": "aa:bb:cc:dd:ee:ff",
      "state": "up",
      "mtu": 1500,
      "duplex": "full",
      "speed_mbps": 1000
    }
  ]
}
```

## Arquitectura

Ver [docs/architecture.md](docs/architecture.md) para diagramas y decisiones técnicas.

## Notas técnicas

- `/proc/cpuinfo` usa `key\t: value` (tabulador antes de `:`). El parser recorta trailing whitespace de la key.
- `/proc` no soporta `mmap()` (st_size == 0). Los readers usan `ifstream`.
- `MmapReader` tiene fallback automático a `read()` para archivos virtuales.
- `std::expected` evita excepciones en el flujo de errores de I/O.
- C++23 `std::println` requiere `<print>` y GCC 14+ / Clang 18+.

## Licencia

MIT
