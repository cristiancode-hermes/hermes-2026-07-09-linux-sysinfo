# linux-sysinfo

## Diagrama de Arquitectura

```
┌─────────────────────────────────────────────────────────────┐
│                       linux-sysinfo                          │
│                    main.cpp (CLI entry)                      │
├─────────────────────────────────────────────────────────────┤
│                      sysinfo::query_*()                      │
│              (high-level aggregate functions)                │
├──────────┬──────────┬──────────┬──────────┬─────────────────┤
│ proc_    │ sysfs_   │ mmap_    │ term_    │ types.hpp        │
│ reader   │ reader   │ reader   │ ioctl    │ (Error, Result)  │
│ .hpp     │ .hpp     │ .hpp     │ .hpp     │                  │
├──────────┴──────────┴──────────┴──────────┴─────────────────┤
│                  Linux Kernel / Hardware                     │
│  /proc/cpuinfo  /sys/block  Regular files  ioctl(TIOCGWINSZ) │
│  /proc/meminfo  /sys/class/net  mmap()    ioctl(TCGETS)     │
│  /proc/[pid]/stat                                          │
└─────────────────────────────────────────────────────────────┘
```

## Flujo de datos

1. **main.cpp** parsea argumentos CLI (`--cpu`, `--memory`, `--all`, etc.)
2. Llama a las funciones `sysinfo::query_*()` de alto nivel
3. Cada `query_*()` instancia el reader correspondiente (ProcReader, SysfsReader, etc.)
4. Los readers acceden al kernel vía:
   - `/proc` → ifstream (NO mmap: los archivos procfs tienen `st_size = 0`)
   - `/sys/` → ifstream (sysfs attributes)
   - Archivos regulares → mmap (RAII wrapper `MmapReader`)
   - Terminal → ioctl (TIOCGWINSZ, TCGETS)
5. Los datos se serializan a `nlohmann::json`
6. main.cpp imprime JSON o texto plano según `--json` / `--text`

## Manejo de errores

Todas las operaciones retornan `Result<T> = std::expected<T, Error>`:

```
query_cpu() → Result<json>
    ↓
  ProcReader::read_cpu_info() → Result<vector<CpuInfo>>
    ↓
  read_proc_file("/proc/cpuinfo") → Result<string>
```

Si falla, el error se propaga con código (errno) y mensaje descriptivo.

## Readers

| Reader       | Fuente         | Mecanismo    | RAII       | Particularidades                          |
|-------------|----------------|-------------|------------|-------------------------------------------|
| ProcReader  | /proc          | ifstream    | move-only  | key\t: value (trim tab), st_size==0       |
| SysfsReader | /sys/block, /sys/class/net | ifstream | move-only | Texto plano, determinista                 |
| MmapReader  | Archivos regulares | mmap/munmap | move-only | Falla en /proc, st_size==0                |
| TerminalInfo| stdin + ioctl  | TIOCGWINSZ, TCGETS | static-only | Fallback a env vars y defaults |

## Decisiones técnicas

1. **Header-only library**: los readers son plantillas de clases inline en headers. Solo `main.cpp` es compilado. Esto elimina la necesidad de compilar una library separada.

2. **C++23 para `std::print`**: usamos `<print>` y `std::println` para output. Si el compilador no soporta C++23, cambiar a `fmt::println` o `std::cout`.

3. **`std::expected` sobre excepciones**: todas las operaciones de I/O con el sistema pueden fallar. Usar `std::expected` evita try/catch y hace el flujo de errores explícito.

4. **FetchContent sobre vcpkg**: para máxima portabilidad (no requiere toolchain externo). Catch2, nlohmann-json y spdlog se descargan en build time.

5. **Docker multi-stage**: GCC 14 (build) → Debian Bookworm Slim (runtime). El binary final es ~25MB.

6. **ProcReader no usa mmap**: los archivos /proc son virtuales, reportan `st_size == 0`, y `mmap()` falla con `EINVAL`. Se usa `ifstream::rdbuf()`.

7. **Trim de key en /proc**: `/proc/cpuinfo` usa `key\t: value`. El parser siempre hace `remove_suffix` de whitespace/tab en la key.

## Estructura de directorios

```
2026-07-09-linux-sysinfo/
├── CMakeLists.txt
├── CMakePresets.json
├── Dockerfile
├── vcpkg.json
├── .gitignore
├── .env.example
├── README.md
├── src/
│   └── main.cpp
├── include/
│   └── sysinfo/
│       ├── types.hpp          # Error, Result<T>
│       ├── sysinfo.hpp        # Umbrella + query_*() functions
│       ├── proc_reader.hpp    # /proc filesystem parser
│       ├── sysfs_reader.hpp   # /sys filesystem parser
│       ├── mmap_reader.hpp    # mmap RAII wrapper
│       └── term_ioctl.hpp     # ioctl terminal queries
├── tests/
│   ├── CMakeLists.txt
│   └── test_main.cpp
└── docs/
    └── architecture.md
```
