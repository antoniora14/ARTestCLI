# DEV-01.3 — Preparación Python privada para Integrate

Estado: **DEV-01.3 ACCEPTED**, según la decisión previa del Architect. DEV-01.1, DEV-01.2
y DEV-01.2-A permanecen ACCEPTED. No se habilitó Integrate ni se añadió Prepare,
Build, registro, ejecución de Test plans o funcionalidad de Studio.

`PreparationService` recibe la selección local actual de Python (la misma que
Generate/Edit guarda), revalida los recursos desde el ejecutable instalado y
ejecuta el adaptador privado `python/tools/prepare.py`. El SDK no se selecciona
por CWD, PATH ni preferencias. La validación del inventario corre fuera del hilo
de UI; la operación y sus descendientes se supervisan sin shell ni PowerShell.
La UI existente no llama todavía a este servicio: lo activan sus pruebas.

El adaptador delega configuración y preparación a `project.py`, y empaquetado,
venv, instalación offline, recibos e inventarios a `package.py`. El lector público
`load_project` conserva exactamente su firma y exigencia de `cliExecutable`.
Su implementación privada permite inyectar Python y wheel sólo en memoria para
preparación; CLI puede estar ausente en esa ruta. No crea rutas ficticias ni
escribe configuración portable/local, rutas del fabricante o preferencias.
Las operaciones públicas que necesitan CLI conservan sus comprobaciones.

Archivos de esta entrega (los demás cambios del working tree son previos):

| Área | Archivos |
| --- | --- |
| Servicio y contención | `source/ARTestDev/PreparationService.h/.cpp`, `TreeProcess.h/.cpp` |
| Delegación Python | `source/ARTestDev/resources/prepare.py`, `source/ARTest.Python/tools/project.py` |
| Staging | `source/ARTestDev/StageDevelopment.ps1`, `SdkLocation.cpp`, `resources/STAGING-NOTICES.md` |
| Build y pruebas | `source/ARTestDev/CMakeLists.txt`, `tests/PreparationTests.cpp`, `tests/StageDevelopmentTests.ps1`, `source/ARTest.Python/tests/test_project.py` |
| Documentación | `docs/planning/artestdev-roadmap.md`, `docs/sdk/artestdev-dev01-2.md`, esta guía |

La identidad sigue siendo la de preparación existente y añade dentro de sus
entradas privadas el hash del adaptador y el inventario offline. No cambia el
formato de recibos, manifiestos, ABI/API ni IPC de Engine. Fuentes, lock, SDK,
intérprete/runtime, venv/ensurepip y herramientas participan; el Test plan no.
Las revisiones quedan en `.artest/stage3/revisions/<identity>` desde su creación.
No se trasladan entornos ni se parchean launchers. La reutilización vuelve a
verificar entradas, recibos e inventarios y no reinstala dependencias.
El adaptador conserva también los candidatos fallidos en su ubicación definitiva,
con evidencia privada en `incomplete` y el marcador de `work` que bloquea nuevos
intentos. No aplica el traslado de cuarentena del flujo histórico, cuyo
comportamiento público permanece intacto.

## Contención y publicación

El proceso empieza suspendido, entra en un Job con kill-on-close y sólo entonces
se reanuda. Los handles heredados se limitan a stdin/stdout/stderr. El lanzamiento
desacoplado de consola evita contar un host de consola como trabajo Python.
La salida retenida se limita a 256 KiB, cada lectura a bloques acotados y el
tiempo por defecto a cinco minutos (máximo configurable privado: treinta minutos).
Se conserva `ProcessAdapter` para las operaciones aceptadas de Generate/Edit.

Antes de llamar al publicador existente, el adaptador espera autorización privada
por stdin. El supervisor sólo la concede cuando queda un proceso en el Job, no
hay cancelación ni timeout y queda espacio para el resultado. Éste es el punto de
compromiso: después se informa el resultado de publicación, no una cancelación
tardía. Si publicar no concluye en cinco segundos, se informa estado no confirmado
y se mantiene el servicio ocupado hasta observar la salida real. Un fallo al
liberar el lock después de publicar se informa como publicación completada con
diagnóstico de recuperación; no se afirma que la selección anterior siga activa.

Cancelar, timeout o exceso de salida antes del compromiso terminan todo el Job.
Sólo se declara terminación confirmada después de observar cero procesos y salida
del principal. Si no se confirma en dos segundos, se informa
`TerminationUnconfirmed`, se bloquea otra operación y se sigue supervisando.
Los locks y marcadores de un proceso interrumpido se conservan: el servicio no
los borra ni reintenta automáticamente. El siguiente intento los rechaza con
diagnóstico de inspección. La destrucción del propietario cierra el Job; no
convierte la falta de observación final en una cancelación confirmada.
La contención es para procesos de autoría confiables, no un sandbox contra código
malicioso del mismo usuario. El soporte sigue limitado a Windows x64; no implica
portabilidad Linux, aceptación de hardware ni integración con un runtime.

## Recursos y límites offline

El staging nuevo tiene 51 archivos inventariados: los 43 recursos anteriores,
el adaptador privado, tres archivos de `artest_sdk`, el wheel SDK 0.2.0, protobuf
6.33.4, pywin32 311 y el inventario offline existente. Los tres wheels se toman
de una ruta de mantenimiento explícita y se verifican contra hashes fijados;
no se descarga nada. Se conserva el inventario estricto de los kits históricos.
No se incluyen CLI, Engine, intérprete, entornos preparados ni outputs de proyectos.
El wheel SDK contiene sus módulos necesarios para instalar el SDK/host Python,
no un runtime de ARTestCLI ni un intérprete.

El perfil offline privado admite líneas `nombre==versión --hash=sha256:...`,
comentarios y líneas vacías. Rechaza URLs, directivas pip y sintaxis no soportada;
no intenta resolverlas por red. Revalida también el lock empaquetado. Todos los
comandos pip install de esta ruta fuerzan `--no-index --no-cache-dir`; las
dependencias deben existir en el wheelhouse verificado. Los comandos históricos
de `package.py` no cambian. No instala software del fabricante ni paquetes globales.

## Reproducir y revisar

Con la combinación Qt/v143/CMake documentada en DEV-01.1, configure un destino
nuevo mediante `ARTESTDEV_STAGING_ROOT` y una carpeta local con los cuatro inputs
offline mediante `ARTESTDEV_WHEELHOUSE`. La validación usó
`artifacts/sdk-packages/x64/Release/ARTestDevelopmentKit-0.4.0-evaluation-windows-x64/python/wheels`
como origen explícito de esos archivos, sin copiar ni modificar el kit completo.
El destino es `source/ARTestDev/build/development staging dev013/<config>`.
Los dos stagings anteriores (`development staging` y
`development staging gitignore fix`) se conservan sin reparación de hashes.

Compile Debug/Release y ejecute CTest con `ARTESTDEV_TEST_PYTHON` apuntando a
CPython externo 3.13 x64 con GIL. `ARTestDevPreparationTests` exige estos inputs;
no los convierte en skips de aceptación. Sus fixtures y logs de operación se
conservan en la carpeta temporal indicada por el log. Los tests que modifican y
sellan inventarios usan exclusivamente copias nuevas identificadas como fixtures;
no reparan instalaciones previas. La matriz nativa sigue ejecutándose con la
selección de Python vacía.

Resultados finales (2026-09-21, CPython externo **3.13.15** Windows x64 con GIL):

| Verificación | Resultado |
| --- | --- |
| Build ARTestDev Debug/Release, C++20/Qt 6.8.3/v143, `/W4` | Aprobado en ambas configuraciones |
| CTest completo | **6/6** por configuración; Debug 617.49 s / Release 482.61 s |
| Servicio de preparación | **13 casos aprobados** por configuración, incluidos setup/cleanup; 0 fallos y 0 skips |
| Generate/Edit, inventario y guardas | 29 casos aprobados por configuración; 0 skips |
| Nativo sin selección de Python | 5 casos aprobados por configuración; 0 skips |
| UI y preferencias | 4 casos aprobados por configuración; 0 skips |
| `scripts/build.ps1` Debug/Release x64 | 242 pruebas aprobadas y 31 deshabilitadas por configuración; ABI, thin-host y XML/HTML correctos |
| Python `test_project` | 68 casos: 66 aprobados y 2 skips por creación de symlinks no disponible en Windows |
| Python `test_package` / `test_sdk` | 5 / 10 pruebas aprobadas |
| Launchers reales en rutas definitivas, `-I -B -S ... --help` | Salida 0 en ambas configuraciones, sin cargar extensiones ni ejecutar Test plans |
| Smoke GUI desde `C:/Windows/Temp` | Salida 0 Debug/Release |
| Whitespace tracked y lista explícita de archivos afectados | Sin hallazgos |

La suite del servicio cubre preparación real offline sin CLI/Engine en staging,
reutilización con recibo/launcher intactos y sin reinstalar, fuente del mismo
tamaño, lock, SDK, herramientas, selección de intérprete y herramientas venv,
Test plan sin invalidación, corrupción, dependencia ausente, cambio durante
preparación, dos servicios concurrentes, timeout/cancelación con descendiente
escritor, terminación no confirmada y límite de salida. También prueba rutas con
espacios, otro CWD, SDK en D: con proyectos en C:, y una copia de SDK con ACL de
sólo lectura. El segundo intérprete es una copia completa de la instalación
externa en un fixture separado, no otra versión ni un runtime del SDK/proyecto.
Se conservan todos los fixtures de preparación; no se ejecutaron Test plans
desde el servicio ni la suite opcional de integración del runtime Python.

Evidencia y procedencia bajo `source/ARTestDev/build/dev013-evidence/`
(outputs ignorados por Git):

- `build-{Debug,Release}-retention.txt`, `ctest-{Debug,Release}-retention.txt`,
  `preparation-<config>.txt`, `authoring-<config>.txt`, `native-only-<config>.txt`
  y `ui-<config>.txt`.
- `engine-{Debug,Release}.txt`, `python-{project,package,sdk}.txt`,
  `launchers-final.txt`, `smoke-retention.txt` y `whitespace*.txt`.
- `source-provenance.json`, `inputs-provenance.json`, `staging-provenance.json`
  y `binary-provenance.json`: fuentes, CPython/wheels, inventarios y binarios.
  Los cuatro árboles históricos verifican sus inventarios originales (42/43
  archivos por configuración); los nuevos contienen 51 archivos por configuración.
- `before-retention/` conserva la matriz previa aprobada. Los logs de preparación
  indican las rutas temporales con cada resultado y sus outputs definitivos.
  `assembly-<config>-<id>/` conserva los fixtures de rechazo del ensamblador.

Incidencias conservadas: `build-Debug-1.txt` registra el bloqueo de FileTracker
por `E_ACCESSDENIED` del sandbox; los builds posteriores fuera de esa restricción
pasaron. La primera preparación llegó al punto de publicación pero agotó su
espera porque `CREATE_NO_WINDOW` agregaba un host de consola al Job. Se conservan
`ctest-preparation-Debug-1.txt`, `preparation-Debug-attempt1.txt` y los primeros
logs `protocol-Debug*.txt`; se detuvo esa suite tras registrar el fallo. El
lanzamiento `DETACHED_PROCESS` y su prueba de handshake corrigieron la causa.
La matriz se repitió tras conservar los candidatos fallidos sin traslado.
Persisten sólo los avisos conocidos de VulkanHeaders/traducciones Qt ausentes;
no hubo advertencias nuevas del compilador. No se reconstruyeron ni modificaron
los kits históricos y no se hizo commit/push.

## Prompt para el Architect

Revise exclusivamente DEV-01.3 en `D:\GitHub\main\ARTestCLI`, preservando el
working tree previo y DEV-01.1/01.2/01.2-A ACCEPTED. Lea AGENTS, scope, roadmap y
esta guía. Revise PreparationService, TreeProcess, el adaptador privado prepare.py,
la factorización privada de load_project, staging/inventarios y pruebas. Contraste
evidencia Debug/Release, preparación offline real, reutilización sin reinstalar,
identidad, fallos/concurrencia, contención de descendientes y punto de publicación,
SDK de sólo lectura y conservación de configuración/stagings. No habilite UI,
registro, ejecución, DEV-01.4 ni Studio. Devuelva **DEV-01.3 ACCEPTED** o
**DEV-01.3 REQUIRES FIXES**, con hallazgos verificables y archivo/línea.
No haga commit/push.
