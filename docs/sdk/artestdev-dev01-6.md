# DEV-01.6 — Validación y ejecución explícita de Test plans

Candidato **con corrección autorizada del arranque Python, verificada en Debug y Release** sobre
`138717eaba5023bba393a5e0a7b31ce2a5593833`. Esta guía no declara aceptación.
DEV-01.5 y las etapas anteriores permanecen
ACCEPTED. No se inicia DEV-01.7.

## Uso

1. Abra el proyecto. En **Project → Run Test plan**, seleccione un archivo Test
   plan y un perfil de ARTestCLI previamente configurado mediante Integrate.
2. El modo predeterminado es **Revision integrada**: usa el código registrado,
   aunque haya editado las fuentes. **Fuentes locales** es una elección explícita:
   prepara/reutiliza Python o comprueba el Build C++ existente, y compone un
   catálogo privado que conserva los otros paquetes del destino. C++ obsoleto
   exige Build en Visual Studio; no se recompila automáticamente.
3. Seleccione Debug/Release para fuentes C++ y el límite del supervisor
   (1–30 minutos; inicialmente 5). Pulse **Validar offline**. Se muestran CLI,
   modo, revisión y SHA-256 del Test plan. El CLI instalado valida el catálogo y
   compila offline. Un rechazo impide ejecutar.
4. Tras **VALIDADO OFFLINE**, pulse **Ejecutar Test plan validado** o **Cancelar**.
   La validación expira después de diez minutos sin autorización. El destino y
   las entradas permanecen bloqueados durante esa espera; cambiar de selección
   requiere cancelar y validar nuevamente.
5. Consulte estado, resumen Engine, incertidumbre, diagnóstico completo y ruta
   de evidencia. **Clear** en el menú contextual sólo limpia los mensajes
   visibles. Conserva operación, controles, resultado, resumen y archivos; los
   mensajes posteriores siguen apareciendo.

El selector toma los bytes exactos del archivo elegido. No diseña ni transforma
el Test plan, no sustituye rutas ni aplica `planBindings` de otro archivo local.
Una copia ya materializada por el flujo existente puede seleccionarse como
cualquier otro Test plan. La revisión mostrada corresponde al código de la
extensión; el Test plan se identifica por separado mediante SHA-256.

## Orquestación y límites

`TestPlanDialog` sólo inicia operaciones mediante los botones explícitos.
`TestPlanService` ejecuta fuera del hilo UI y reutiliza perfiles/locks de
`RegistrationStore`, `PreparationService`, `NativeService` y `TreeProcess`.
Invoca directamente `extensions validate`, `compile` y `extension-run` del CLI
seleccionado. No interpreta ni ejecuta pasos. La corrección privada autorizada
de Engine se describe abajo; Core, SDK público, ABI/API/IPC, formatos y política
del secuenciador permanecen sin cambios.

La revisión integrada requiere registro e inventario concordantes. El modo local
copia los paquetes de otros propietarios y el paquete propio a un catálogo
aislado; mantiene los recibos Python en sus rutas originales. No publica catálogo,
asociaciones ni perfil, no recupera transacciones automáticamente y no cambia
silenciosamente de modo. Un journal pendiente remite a Integrate.

Ambas invocaciones CLI usan la misma copia del Test plan. Handles de lectura
Win32 impiden escrituras/renombres ordinarios de los archivos fijados durante
validación y ejecución. Se vuelven a verificar inventarios antes de ejecutar,
incluidos catálogo y entornos; un cambio de topología aborta el intento. Los
locks existentes del perfil/destino excluyen escritores cooperativos y otras
operaciones sobre ese destino hasta la terminación observada. Como en la
compilación existente, esto no constituye defensa contra un escritor malicioso
ni sandbox de extensiones nativas confiadas.

La preparación Python y comprobación de Build conservan sus límites existentes.
Cada proceso CLI retiene como máximo 1 MiB de salida; las comprobaciones offline
tienen límite de 120 s. El límite externo de ejecución es distinto del timeout de
cada paso: no cambia el documento ni la política Engine. Si el supervisor cancela,
vence su plazo o agota la salida, solicita terminar el árbol de procesos. **No es
cancelación cooperativa del Engine ni prueba de limpieza física**. Se muestran
el motivo, exit code y cualquier resultado final disponible; sin resultado no se
inventa PASS. Una terminación no confirmada mantiene bloqueados operación y
destino hasta observar asentamiento, incluso después del primer diagnóstico.

El resultado JSON v2 del Engine se conserva completo, incluidos verdict, errores,
cancelación, timeout y `outcome.indeterminate` de pasos/intentos. La GUI no
reintenta ni continúa ejecuciones. El Engine conserva su política existente de
no repetir/continuar tras incertidumbre. Un nuevo intento exige otra acción
explícita; después de incertidumbre se indica inspeccionar antes de repetir.

Evidencia por intento: `.artest/test-runs/<id>/inputs.json`, `catalog.txt`,
`compile.txt`, `execution.txt` cuando se ejecutó y `result.json`, más la copia
del plan y asociaciones. Son salidas privadas, sin nuevos contratos públicos.
No se borran intentos ni se promete retención global acotada. No hay ejecución al
abrir, Generate, Edit, Integrate, recuperar ni cerrar. No hay PowerShell/consola
en el recorrido GUI, Studio, .NET, instaladores o dependencias nuevas. La
verificación utiliza exclusivamente simuladores; no certifica hardware ni impide
que un Test plan elegido explícitamente llame a los drivers de su instalación.

## Corrección privada Python autorizada

El propietario autorizó el 2026-09-30 corregir `ManagedIntegrity.cpp`,
`FileIntegrity.cpp` y un helper privado. La entrada #3 del recibo era
`Lib/site-packages/__pycache__/pythoncom.cpython-313.pyc` (267 caracteres físicos):
relativa y contenida, pero `is_regular_file` devolvía false/error Windows 3.
El mismo archivo con ruta extendida sí existía; `weakly_canonical` retiraba el
prefijo. Se verificaron 8348 hashes en cuatro recibos sin discrepancias.
El diagnóstico original queda en `python-inventory-diagnosis.json` y sus probes.

`PrivatePath.h` convierte rutas ordinarias de unidad/UNC a acceso extendido y
rechaza dispositivos, streams y nombres reservados. `ManagedIntegrity.cpp`
mantiene la representación extendida coherente para contención, duplicados,
exclusión del recibo e inventario enumerado. Rechaza reparse points antes de leer
los archivos y en ancestros del entorno/recibo. `FileIntegrity.cpp` usa ese acceso
para hashing. SHA-256, proveedor CNG por hilo y máximo de ocho workers siguen
intactos. No se alteran recibos, entornos, layout DEV-01.5 o configuración global
de Windows; tampoco contratos o comportamiento de `--extensions`.

`ManagedIntegrityTests.cpp` usa archivos físicos y recibo/launcher de más de
300 caracteres. La regresión anterior da ruta corta PASS/ruta larga FAIL
(`integrity-red-Debug.xml`). Los 20 casos nuevos pasan en los gates corregidos:
ausentes, alterados, adicionales, escapes, absolutos, duplicados y junctions,
además de dispositivos/streams y normalización de unidad/UNC. UNC se comprobó
léxicamente; no se certifica un recurso de red real.

## Corrección autorizada del worker (2026-10-01)

El propietario autorizó importar `pywintypes` antes de `win32file` y `win32event`
en `source/ARTest.Python/artest_host/transport.py`. El comentario explica que el
cargador implícito de `win32file` trunca la ruta DLL en los entornos preparados
largos. No se modifica la corrección privada de Engine previamente revisada.

La regresión `source/ARTest.Python/tests/test_transport_startup.py` crea procesos
independientes con el intérprete base y `-I -B -S`, comprueba que no hay módulos
precargados y usa el setup del launcher real. El control negativo restaura sólo
el orden anterior en memoria; debe fallar importando pywintypes. El positivo usa
el transport de producción. No se acortan rutas ni se editan los entornos.
Las rutas DLL históricas de Debug y Release tienen 264 caracteres. La auditoría
final repite la prueba con transport instalado en nuevas revisiones, además de
los recorridos completos Engine/CLI de la matriz.

`package.py sdk` regenera el wheel con protoc existente. `StageDevelopment.ps1`
fija su nuevo SHA-256; no cambian versiones, dependencias ni layout. Los recursos
se generan en `artifacts/dev016-worker-final/wheels`, los entornos de integración
en `artifacts/dev016-worker-final/runtime` y los staging en
`source/ARTestDev/build/dev016-worker-staging/{Debug,Release}`. Se conserva el
wheelhouse anterior. El SHA-256 nuevo del wheel es
`dda4863823389537f68ce40fce10c787af5d2ebc681585a0f749a0f0973e0cf9`.

La matriz existente crea nuevos proyectos y revisiones mediante Integrate y
PreparationService. Python driver-command y command-only usan revisión integrada
y fuentes locales; command-only consume el driver de otro paquete mediante el
broker. Tras editar fuentes, la revisión integrada debe conservar 5/84 y la local
devolver 10/168; ambos modos comprueban verdict FAIL y preservación del catálogo,
asociaciones y perfil. Se mantienen validación offline previa, ejecución explícita,
C++, incertidumbre sin reintentos, cancelación, timeout, solapamientos y Clear.

La primera matriz con imports corregidos expuso una expectativa de prueba que
antes no se alcanzaba: command-only esperaba 42, aunque su Test plan generado
fija `factor: 2` y el driver externo entrega 42. Se corrige exclusivamente esa
aserción de `TestPlanTests.cpp` a 84; la aserción local continúa exigiendo el doble
(168). El plan y las fuentes del producto no se cambian. El intento inicial y el
archivo de prueba previo se conservan separados de la repetición final. Ese intento
Debug terminó en 23 PASS / 1 FAIL / 0 SKIP (1781675 ms), con el único fallo en la
aserción 84 frente a 42; véase `worker-final/initial-expectation/matrix-Debug.txt`.
La auditoría detectó además que cinco casos `pythonEffects` seleccionaban por
ruta fija el fixture histórico `artifacts/python-c01-final`. El test ahora acepta
`ARTEST_PYTHON_ROOT` y conserva el valor anterior como fallback para otros runners;
el runner de este candidato selecciona explícitamente
`artifacts/dev016-worker-final/runtime`. No cambia código del producto ni recibos.
La matriz intermedia con la expectativa corregida (24 PASS / 0 FAIL / 0 SKIP,
1927758 ms) se conserva en `worker-final/before-fixture-selection`; sus cinco casos
con el worker histórico no acreditan el worker nuevo. La matriz final se vuelve
a ejecutar con el fixture nuevo en ambas configuraciones.

La comparación `wheel-delta.json` verifica que el wheel sólo cambia
`artest_host/transport.py` y `artest_python-0.2.1.dist-info/RECORD`.

La primera ejecución complementaria CTest Release pasó 6/8 suites: el runner no
fijaba la CLI para PreparationTests y su archivo de salida colisionaba con el de
UiTests. Se corrigió sólo el runner de evidencia (variables explícitas y salida
`ui-*.console.txt`). Los resultados iniciales quedan en `worker-final/runner-corrections`;
no se atribuyen al producto ni cuentan como la ejecución final.

## Evidencia final

Directorio exacto:
`D:\GitHub\main\ARTestCLI\source\ARTestDev\build\dev016-evidence\worker-final`.
`candidate.json` en ese directorio y el índice padre vinculan fuentes, recursos,
binarios, staging, revisiones y reportes mediante SHA-256. `fixture-audit.json`
identifica cada intento y recibo seleccionado, verifica los inventarios completos
y exige que el SDK y el transport instalado correspondan al wheel nuevo.
`preserved-before.json`/`preserved-after.json` comprueban los 12 recibos y 25172
archivos históricos, los staging anteriores y las fuentes de integridad Engine.
`preserved-implementation.json` verifica los 14 archivos de implementación del
candidato previo sin cambios; guía, aserción y selección de fixture del
test se documentan aparte.
La auditoría final verifica 52 intentos, 52 requests y siete recibos nuevos por
configuración, además de los dos recibos nuevos de integración Python. Las copias
de logs y resultados están en `attempts`/`requests`; `inventories` conserva los
manifiestos y recibos auditados. Los entornos originales permanecen en sus rutas.

Comandos reproducibles desde la raíz (Python base instalado, rutas completas en
`run.ps1` y registros `*.commands.txt`):

```powershell
$e = 'source/ARTestDev/build/dev016-evidence/worker-final'
# resources.ps1 conserva los comandos usados para generar el wheel y wheelhouse.
& "$e/resources.ps1" -OutputRoot 'artifacts/dev016-worker-final/wheels'
& "$e/run.ps1" -Phase prepare -Configuration Debug
foreach ($cfg in 'Debug','Release') {
    & "$e/run.ps1" -Phase root -Configuration $cfg
    & "$e/run.ps1" -Phase gui -Configuration $cfg
    & "$e/run.ps1" -Phase matrix -Configuration $cfg
    & "$e/run.ps1" -Phase python -Configuration $cfg
    & "$e/run.ps1" -Phase ui -Configuration $cfg
}
```

`root` ejecuta exactamente `scripts/build.ps1 -Configuration Debug|Release -Platform
x64`; `python` ejecuta `scripts/test-python-runtime.ps1` con el nuevo PythonRoot.
La preparación usa sólo los wheels locales ya disponibles. Para repetir la
preparación con otros inputs se debe elegir un directorio nuevo; nunca editar un
recibo o reutilizar su ruta para inputs diferentes. `audit.py`, `preserve.py after`
y `seal.py` completan la auditoría y el manifiesto final usando Python `-I -B`.

| Prueba nueva | Debug | Release |
|---|---:|---:|
| Build, gates ABI/SDK/thin host, GTest y reportes XML/HTML | 263 PASS / 0 FAIL | 263 PASS / 0 FAIL |
| Integración Python explícitamente habilitada | 27 PASS / 0 FAIL / 0 SKIP | 27 PASS / 0 FAIL / 0 SKIP |
| Contratos worker Python | 4 PASS | 4 PASS |
| Matriz DEV-01.6 completa | 24 PASS / 0 FAIL / 0 SKIP | 24 PASS / 0 FAIL / 0 SKIP |
| Suites complementarias GUI/preparación/assembly | 8/8 PASS | 8/8 PASS |
| Regresión fría: orden anterior / corregido | FAIL esperado / PASS | FAIL esperado / PASS |

La matriz final Debug duró 2054686 ms y Release 1018843 ms. Cada configuración
ejecutó la regresión fría tanto sobre rutas históricas reales como sobre transport
instalado en una nueva revisión. El FAIL del control negativo es obligatorio y
no equivale a un fallo del candidato corregido.

Evidencia histórica: `worker-final/previous-candidate.json` y
`worker-final/previous-README.md` preservan el candidato previo de 22 PASS/2 FAIL.
El diagnóstico y la regresión roja de integridad Engine se reutilizan y no se
presentan como pruebas nuevas. Las suites largas completas Native/Integration
no se repiten; la matriz DEV-01.6 sí ejecuta los recorridos C++/Python reales de
integración y Test plan. Los 31 DISABLED del gate nativo no cuentan como pases
Python; la suite Python se habilita explícitamente por separado. Los cuatro
casos opcionales TcpHelloPython no se habilitan en esta corrección acotada.
Sin hardware ni un recurso UNC real. Los builds conservan el aviso Qt sobre
`translations/catalogs.json` con `--no-translations`.

Sin contratos, ABI/API/IPC, cambios de `--extensions`, DEV-01.7, commit ni push.
La aceptación formal corresponde al Architect.

## Prompt para revisión

Architect: revise únicamente DEV-01.6 sobre `138717e`, incluida la corrección del
orden de imports del worker expresamente autorizada. Lea AGENTS y esta guía;
verifique `D:\GitHub\main\ARTestCLI\source\ARTestDev\build\dev016-evidence\worker-final\candidate.json`
y sus hashes/reportes. Contraste regresión fría anterior/corregida, revisiones
nuevas sin reutilizar el worker antiguo, preservación histórica y matriz
Debug/Release integrada/local con driver externo, ediciones y FAIL. Compruebe
validación offline, ejecución explícita, incertidumbre sin replay, cancelación,
timeout, solapamientos y Clear. Distinga evidencia nueva de reutilizada y devuelva
**DEV-01.6 ACCEPTED** o **DEV-01.6 REQUIRES FIXES**, con hallazgos verificables.

## Cierre del Architect — 2026-10-01

**DEV-01.6 ACCEPTED.** El Architect verificó el candidato final sobre `138717e`,
1938 entradas selladas y 58580 archivos históricos/nuevos sin discrepancias.
Debug/Release: gates 263 PASS (31 deshabilitadas), Python 27 PASS, worker 4 PASS,
matriz 24 PASS/0 FAIL/0 SKIP y ocho suites complementarias aprobadas.
Los controles negativos e intentos intermedios se distinguen de los resultados finales.

Las secciones anteriores conservan el registro de entrega previo a la aceptación.
Los hashes sellados de documentación corresponden a ese registro previo; esta
actualización de cierre sólo cambia documentación, no fuentes ejecutables ni
evidencia. DEV-01.7 requiere su propio handoff; C-03/C-04 permanecen posteriores.
