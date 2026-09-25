# DEV-01.4 — C++ IDE builds and integration-ready outputs

DEV-01.4 está ACCEPTED tras la corrección de recuperación revisada el 2026-09-22.
DEV-01.1/01.2/01.2-A/01.3 permanecen ACCEPTED. **DEV-01.4-A ACCEPTED** el
2026-09-25 tras revisar la corrección selectiva y su evidencia Debug/Release. No se habilita Integrate ni
se implementa DEV-01.5. No hay registro, ejecución de Test plans, hardware,
descubrimiento de instalaciones, instalador o publicación de una distribución.

Archivos de esta entrega: `Authoring.cpp`, `CMakeLists.txt`, `SdkLocation.cpp`,
`StageDevelopment.ps1`, `TreeProcess.h/.cpp`, `resources/STAGING-NOTICES.md`;
nuevos `NativeMain.cpp`, `NativeOutputs.h/.cpp`, `NativeService.h/.cpp`,
`resources/ARTestDevNative.targets`, `tests/NativeTests.cpp` bajo
`source/ARTestDev/`, y los probes nativos bajo `tests/TestSupport/Fakes/`.
Esta guía describe la entrega aceptada y, al final, su ajuste DEV-01.4-A.
Las descripciones de Rebuild obligatorio y retención ilimitada corresponden a la
base aceptada: el ajuste autoriza sustituir esas políticas privadas, no la
integridad ni la recuperación. No se reescriben los resultados de aceptación.

## Recorrido y separación

El staging corregido `source/ARTestDev/build/development staging dev014 fix/<config>`
añade `ARTestDevNative.exe` y `native-sdk/build/native/ARTestDevNative.targets`.
El descriptor privado mantiene `internalVersion: 1` y declara
`nativeBuildProfile: 1`; su inventario estricto contiene 53 archivos.
Los perfiles históricos conservan sus inventarios y targets. Los proyectos
nuevos generados con este perfil importan el target privado; no se migran
proyectos existentes ni se modifica `ARTestMetadata.targets` o
`Publish-ARTestPackage.ps1`.

Generate/Edit, menú, preferencias, configuración portable/local y Python
conservan su recorrido. El proyecto C++ incluye una guía `GENERATE-EDIT.md` y
excluye `.artest/` de Git. Visual Studio usa MSBuild y el helper nativo para
compilar DLL y ejecutable de metadatos en directorios separados. Este último
reutiliza `ARTEST_METADATA_GENERATOR` y la definición declarativa existente.
El helper materializa el bundle, schemas e integridad SHA-256 de la DLL.
Ni el proyecto ni el kit incorporan CLI, Engine/Core o intérprete Python.
El recorrido nuevo de compilación no llama PowerShell, tampoco mediante MSBuild.
`Exec` usa el shell estándar de MSBuild (cmd); no es un shell PowerShell.

El ensamblado **de mantenimiento** del kit sigue usando el script histórico de
staging y CMake local de ARTestDev. Es distinto de compilar los proyectos
entregados: esos scripts no se distribuyen ni intervienen en el build nativo.
No se migra el SDK general a CMake ni se alteran ABI/API/IPC o recibos públicos.

## Contenido y estados

Cada Build/Rebuild explícito del IDE realiza compilación nueva en
`.artest/native/<Debug|Release>/work-<revision>/`. Se desactiva el fast-up-to-date
check del IDE; no se acepta existencia, fecha, tamaño o cierre de Visual Studio
como prueba de vigencia. No hay reconstrucción automática desde inspección o
validación. Clean conserva resultados y evidencia privada.

La publicación local deja:

- `current/package/`: DLL, `artest-extension.json` y `schemas/`.
- `current/inputs.json`: fuentes, configuración, imports evaluados por MSBuild,
  inventario completo del SDK, MSBuild, compilador x64, headers/librerías y Windows
  SDK seleccionados, y variables de entorno relevantes, todos por SHA-256.
- `current/ownership.json`: propietario absoluto, revisión, identidad de entradas
  e inventario completo de salidas, incluyendo directorios.
- `work-*`, `evaluation-*` y `validation-*`: logs y evidencia retenidos.

El inventario de fuentes excluye sólo directorios superiores de outputs/IDE/Git
(`bin`, `obj`, `out`, `.artest`, `.vs`, `.git`). Es conservador: documentación o
Test plans dentro del proyecto también pueden volver obsoleto el resultado C++.
Los tlogs de compilador/linker comprueban que sus dependencias pertenecen a raíces
inventariadas o a recursos del sistema operativo. Dependencias de autor externas
requieren colocarse dentro del proyecto; no se afirma soporte para cualquier
custom build arbitrario. Los proyectos/extensiones son código confiable, no un
sandbox de código hostil. Límites privados: archivo de hash <=512 MiB, JSON
<=32 MiB, inventario de archivos <100000 entradas. El hash completo cuesta tiempo;
no se sustituye por cachés de timestamps.

`compiled` significa resultado íntegro y vigente. `stale` significa cambio de
fuentes/SDK/configuración/toolchain; `missing`, ausencia de compilación;
`failed`, corrupción, selección inválida o estado no seguro. `target-validated`
vincula una revisión concreta con hashes de CLI/Engine y sus informes. No es un
estado permanente: cambiar entradas, salidas o destino obliga a verificar otra vez.

## Publicación, recuperación y procesos

Un lock Win32 exclusivo por proyecto/configuración serializa build, inspección y
validación. Se verifica ownership e inventario antes de reemplazar o retirar
resultados. Un journal canónico privado registra fase y topología. JSON privado
no canónico (incluidas claves duplicadas), archivos/directorios desconocidos,
reparse points, inventarios corruptos y topologías ambiguas se preservan y rechazan.

El candidato se ensambla fuera de `current`; las transiciones son
`building -> staged -> ready -> backed-up -> promoted`. La recuperación diferencia
el rename del cambio de fase: restaura backup si faltó promover o conserva la
revisión promovida si el rename ya ocurrió. Funciona incluso con inventarios de
payload idénticos. Sólo se retiran árboles propios y verificados. Fallos de
compilación confirmados conservan el resultado anterior y los logs del intento.
No se promete disponibilidad atómica del nombre, durabilidad ante pérdida de
energía, hot reload ni limpieza automática de evidencia.

Un journal interrumpido en `building`, `checking` o `validating` queda bloqueado:
la muerte del supervisor no demuestra que todos los descendientes terminaron.
Se requiere inspeccionar el estado y confirmar su terminación antes de intervención
manual; no se elimina automáticamente el marcador para permitir otro escritor.

Los procesos arrancan suspendidos y entran en un Job kill-on-close antes de
continuar; la salida se limita a 1 MiB y cada hijo a cinco minutos. El target
exterior y la API privada admiten como máximo treinta minutos. La terminación
sólo se confirma con raíz finalizada y cero procesos activos. Si no se confirma,
se conserva busy/lock y se impide solapar operaciones incompatibles.
`NativeService` inspecciona SDK mediante QtConcurrent y supervisa el helper de
forma asíncrona; aún no se conecta a Integrate (DEV-01.5).

Excepción acotada al compilador: tras MSBuild exit 0 se permite terminar el tail
`vctip.exe` **únicamente** en la ruta exacta del toolchain inventariado y sólo si
todos los procesos restantes corresponden a ella. Todavía se exige confirmar
cero procesos; ruta ajena, timeout o terminación no confirmada nunca producen
éxito. El comportamiento estricto por defecto de PreparationService se conserva.
El comportamiento persistente de esa herramienta también está documentado en
[Microsoft BuildXL](https://github.com/microsoft/BuildXL/blob/main/Public/Sdk/Experimental/Msvc/VisualCpp/visualCpp.dsc).
No se cambia la configuración global de telemetría ni se permite breakaway.

## Validación explícita contra destino

La selección absoluta de `ARTestCLI.exe` debe pertenecer a una instalación separada
del proyecto y del SDK, con su `ARTestEngine.dll`. No hay búsqueda automática.
Después de comprobar vigencia e integridad privada, el helper delega en:

```text
ARTestCLI.exe extensions validate <state>/current
ARTestCLI.exe extensions doctor <state>/current
```

Se exige informe del catálogo v2, ABI nativa 0.2 y un único paquete válido;
doctor debe devolver catálogo activo válido. Engine realiza las comprobaciones
existentes de integridad y descriptores; no se reimplementa ese validador ni se
vincula Engine/Core al helper. Doctor carga la definición/descriptores nativos en
su proceso temporal, sin construir instancias de drivers/commands, registrar una
instalación ni ejecutar Test plans. Se vuelven a verificar entradas, outputs y
CLI/Engine antes de guardar `validation-*/result.json` y logs.

Comandos privados de diagnóstico (rutas absolutas, x64, Debug o Release):

```text
<SDK>/ARTestDevNative.exe check-project <project.vcxproj> Debug <SDK>/native-sdk
<SDK>/ARTestDevNative.exe validate-project <project.vcxproj> Debug <SDK>/native-sdk <separate>/ARTestCLI.exe
```

Son herramientas internas, no un contrato público nuevo. La inspección usa
MSBuild `/pp` para comprobar imports; no compila. Requiere mantener disponible
el toolchain registrado. No soporta rutas MSBuild con `$`, `%` o `;`, ni UNC.

## Verificación reproducible

Desde el checkout, con CMake/Qt y wheelhouse offline ya preparados, configurar un
staging **nuevo**. No reutilizar ni reconstruir los stagings/baselines históricos:

```powershell
cmake -S source/ARTestDev -B source/ARTestDev/build `
  -DARTESTDEV_STAGING_ROOT="$PWD/source/ARTestDev/build/development staging dev014 fix" `
  -DARTESTDEV_EVIDENCE_ROOT="$PWD/source/ARTestDev/build/dev014-fix-evidence/regression"
cmake --build source/ARTestDev/build --config Debug
cmake --build source/ARTestDev/build --config Release
$env:ARTESTDEV_TEST_MSBUILD = '<absolute MSBuild/Current/Bin/amd64/MSBuild.exe>'
$env:ARTESTDEV_TEST_PYTHON = '<external CPython 3.13 x64/python.exe>'
$env:QTEST_FUNCTION_TIMEOUT = '1800000'
# Repetir con Release y su CLI compatible explícito.
$env:ARTESTDEV_TEST_STAGING = "$PWD/source/ARTestDev/build/development staging dev014 fix/Debug"
$env:ARTESTDEV_TEST_CLI = '<separate Debug installation/ARTestCLI.exe>'
& source/ARTestDev/build/Debug/ARTestDevNativeTests.exe -o '<evidence>/native-Debug.txt,txt'
ctest --test-dir source/ARTestDev/build -C Debug --output-on-failure -E '^ARTestDevNativeTests$'
.\scripts\build.ps1 -Configuration Debug -Platform x64
.\scripts\build.ps1 -Configuration Release -Platform x64
.\scripts\test-native-compatibility.ps1 `
  -BaselineDirectory artifacts/compatibility/baselines/sdk-0.2.1-native-v1-x64-Release `
  -Configuration Debug -EnginePath artifacts/bin/x64/Debug/ARTestEngine.dll `
  -OutputDirectory <new-evidence-directory>
```

PowerShell arriba es exclusivamente el harness de mantenimiento/verificación.
La prueba nativa copia SDK y CLI a directorios separados fuera del checkout,
genera driver-only/command-only/driver-command en rutas con espacios, impone ACL
de sólo lectura al SDK y verifica que escribir falla, reduce PATH a System32 y
compila mediante MSBuild real. Registra imágenes de **todos** los descendientes
con completion port del Job y exige observed=expected, sin Python/PowerShell ni
procesos sin resolver. La evidencia usa MSBuild real de Visual Studio, no una
observación manual de clicks en el IDE. La selección del runtime sólo participa
después del build. Los tlogs verifican ausencia de dependencias del checkout.

La suite también cubre cambio del mismo tamaño, DLL/manifiesto corruptos, SDK y
toolchain incompatibles, resultados ausentes, descriptor inválido que alcanza
Engine, CLI ausente/incompatible, error de compilación con retención, escritores
concurrentes, publishers realmente terminados en tres ventanas de rename,
inventarios idénticos, ownership, junction/reparse, claves duplicadas, topología
ambigua y terminación no confirmada. Las pruebas de contención incluyen el modo
estricto previo, tail permitido y mismo basename en ruta ajena.

## Evidencia de esta entrega

Resultados del candidato inicial, 2026-09-22 (preservados; no cubren el blocker
de recuperación identificado posteriormente por el Architect):

| Verificación | Resultado |
| --- | --- |
| App/helper Debug y Release, C++20/Qt 6.8.3/v143 `/W4` | Builds aprobados; extensiones con v145/Windows SDK 10.0.26100.0 |
| Matriz nativa Release externa | 21 casos aprobados, 0 fallos, 0 skips; 494.04 s |
| Guardas nativas Debug | 18 casos aprobados, 0 fallos, 0 skips; 7.88 s |
| Matriz nativa Debug externa | 3 variantes aprobadas (5 casos con setup/cleanup), 0 fallos, 0 skips; 1132.48 s |
| Regresiones existentes CTest | 6/6 Debug (757.62 s) y 6/6 Release (550.53 s) |
| Generate/Edit, nativo sin Python, preparación, UI | 29 / 5 / 13 / 4 casos por configuración; 0 skips |
| `scripts/build.ps1` Debug/Release x64 | 242 aprobados, 31 deshabilitados por configuración; ABI, thin-host, reportes XML/HTML correctos |
| Publicación histórica | 12 escenarios SdkPublicationTests aprobados por configuración dentro del gate anterior |
| Consumidor congelado SDK 0.2.1 / ABI 0.1 | 8/8 escenarios contra cada Engine Debug/Release; sólo validación, sin reconstruir baseline |
| Inventarios históricos y nuevos | 42/43/51 originales intactos y 53 nuevos, por configuración |
| Imports PE del helper | Sólo QtCore y runtime C++/Windows; sin Engine/CLI/Python |

Los 31 casos deshabilitados pertenecen al gate histórico; no se presentan como
pruebas ejecutadas ni como aceptación del runtime Python opcional. No hubo skips
en la aceptación nativa o las suites de ARTestDev. Se conservan avisos conocidos
de despliegue Qt/traducciones; no se declara instalador ni SDK publicado.

Índice de evidencia:

- `source-provenance.json`, `binary-provenance.json`, `staging-provenance.json`:
  hashes de fuentes, binarios finales y descriptores; el manifiesto binario enlaza
  los otros dos por SHA-256.
- `external-{Debug,Release}-provenance.json` y `external-Debug-guards-provenance.json`: hashes de los fixtures externos,
  incluidos proyectos, SDK copiado, DLL, metadatos, schemas, logs, inputs,
  ownership y validaciones; enlaza las fuentes/binarios de esta entrega.
- `evidence-index.json`: inventario hash de la evidencia retenida y de esta guía.
- `native-Debug-matrix-final.txt`, `native-Debug-guards-final.txt`,
  `native-Release-final.txt`, `process-audit-*.txt`: matriz y auditorías completas
  (39 árboles por configuración, con observed=expected y sin procesos sin resolver).
- `regression-{Debug,Release}.txt` y `regression/*.txt`: resultados previos,
  incluidos fixtures retenidos de preparación y rechazo del ensamblador.
- `engine-*.txt`, `compatibility-*/*`, `helper-dependencies.txt`,
  `configuration-rejection.txt`, `sdk-selection-rejection.txt`, `whitespace*.json`.
- `initial-status.txt`, `initial-diff.txt`, `preserved-work.txt`, `final-status.txt`:
  el diff tracked final coincide con el inicial; el trabajo previo se conserva.

Fixtures externos finales: `C:/Users/anton/AppData/Local/Temp/dev014-hqJcmo`
(Debug), `dev014-yAGhFo` (guardas Debug) y `dev014-LbCtKB` (Release), todos bajo
la misma carpeta temporal. No eliminarlos antes de la revisión: contienen la
relación completa entre fuentes/SDK, resultados y comprobaciones contra destino.
Whitespace: `git diff --check` y los 52 archivos modificados/nuevos pasan. El
barrido global de 414 archivos detectó 24 líneas preexistentes en fuentes tracked
sin cambios; se conservan en `whitespace-all.json`, sin limpieza fuera de alcance.

La evidencia se conserva en `source/ARTestDev/build/dev014-evidence/` (ignorada por
Git) y en los fixtures externos indicados por los logs. No se hace commit/push.

Fallos intermedios conservados, no contados como aceptación:

1. `native-Debug-attempt1.txt`: Windows SDK expuesto como `10.0` en vez de su
   versión efectiva. Se corrigió usando `TargetPlatformVersion`.
2. `native-Debug-attempt2.txt`: auditoría por polling no resolvía procesos breves.
   Se reemplazó por lector inmediato de completion port y conteo completo.
3. `native-Debug-attempt3.txt`: build y validación positivos, pero el timeout de
   función QtTest de 300 s abortó la matriz extensa. El harness usa ahora 1800 s;
   no se amplió el límite de cinco minutos por operación hija.
4. `native-Debug-matrix-1.txt`: compilador finalizaba correctamente pero quedaba
   `vctip.exe`; el supervisor estricto rechazó el resultado. Se añadió la excepción
   exacta descrita arriba con pruebas positivas, ruta ajena y terminación incierta.

## Corrección del blocker de recuperación

El Architect devolvió **DEV-01.4 REQUIRES FIXES**: el candidato inicial no podía
recuperar primera publicación en `backed-up` con sólo candidate, promoción cuyo
backup ya se retiró, ni rollback terminado antes de retirar el journal. La
reproducción original se conserva intacta en
`C:/Users/anton/AppData/Local/Temp/dev014-architect-recovery-v6sizpk4`.
Los resultados positivos iniciales no demuestran resolución de este fallo.

La corrección se limita a `NativeOutputs.cpp`, `tests/NativeTests.cpp` y esta guía.
Antes de cualquier rename de publicación, el journal registra `candidateOwner`
y, cuando corresponde, `previousOwner`: objetos completos de ownership con
revisión e inventario. La igualdad de payloads no permite confundir identidades.
Toda recuperación vuelve a comprobar ownership e inventarios completos contra
esas identidades antes de mutar árboles.

Se registra `rolling-back` antes de restaurar la versión anterior y
`retiring-backup` antes de retirar el backup promovido. `rolled-back` y
`completed` preceden al retiro del journal. El retiro usa rename al mismo volumen
hacia `work-<revision>/retired-candidate` o `retired-backup`: se conserva el árbol
completo como evidencia, sin borrado recursivo susceptible de quedar parcial.
El coste es espacio retenido también después de publicación exitosa. No se añade
pruning automático ni se cambia Clean.

| Ventana | Recuperación |
| --- | --- |
| Primera publicación, `backed-up`, sólo candidate | Rollback a ausencia de resultado, candidato retenido; siguiente build permitido |
| Backup ya retirado, journal pendiente | Verifica current nuevo y backup retenido por identidad; termina limpieza |
| Rollback restaurado, candidato ya retirado | Verifica current anterior y candidato retenido; termina limpieza |
| Interrupción dentro de recuperación | Acepta sólo la topología inequívoca de esas fases/identidades; repite sin perder resultados |

Los journals del candidato rechazado sin estas identidades se conservan para
inspección; no se inventa la identidad anterior para desbloquearlos. No se migra
un contrato público ni se modifica la reproducción del Architect. Las nuevas
pruebas generan los estados con el protocolo corregido mediante procesos reales.

`cleanupInterruptions` mata el probe en seis ventanas: antes de primera promoción,
después del rename de backup a retirado, después de restaurar backup, después de
retirar candidate, y antes de retirar journal tanto tras promoción como rollback.
Cada caso usa payloads idénticos, verifica rechazo sin cambios frente a identidad
ajena/corrupción/archivo desconocido/reparse, recupera, repite la recuperación y
publica otra revisión. Los hooks y el marcador de pausa existen sólo al compilar
`ARTESTDEV_TESTING`; no se distribuyen en el helper.

Nueva evidencia bajo `source/ARTestDev/build/dev014-fix-evidence/`, con staging
`development staging dev014 fix/{Debug,Release}`. El staging, fixtures e índice
iniciales se preservan; `before/` conserva las fuentes y guía anteriores.

- `guards-Debug-final.txt`: **24 aprobados, 0 fallos, 0 skips** (12.08 s).
- `guards-Release-final.txt`: **24 aprobados, 0 fallos, 0 skips** (7.06 s).
- Fixtures: `dev014-CtPRtH` y `dev014-yvcFet`, bajo el mismo TEMP del usuario.
- `matrix-Release-final.txt`: tres variantes reales aprobadas (5 casos con setup/
  cleanup), 0 fallos, 0 skips; 472.97 s.
- `matrix-Debug-final.txt`: tres variantes reales aprobadas (5 casos con setup/
  cleanup), 0 fallos, 0 skips; 1098.86 s. Fixtures finales: `dev014-iijoTX`
  (Debug) y `dev014-eTxrVe` (Release), bajo TEMP. Cada configuración conserva
  39 auditorías completas de procesos, sin Python/PowerShell y sin CLI/Engine
  en el árbol de compilación. SDK externo copiado con espacios y ACL sólo lectura.
- Gates obligatorios repetidos: 242 aprobados y 31 deshabilitados por configuración.
  ABI, thin-host, publicación histórica y consistencia XML/HTML correctos.
- Diez inventarios de staging verificados. Whitespace de 52 archivos modificados/
  nuevos correcto; diff tracked igual al inicial.
- Compatibilidad congelada 8/8 conservada: ambos Engines siguen teniendo exactamente
  los hashes del ensayo anterior; baseline e inventario verificados sin reconstruir.
- Las seis suites CTest previas se conservan como regresión del código no modificado;
  esta corrección repite las pruebas nativas y gates de AGENTS, sin atribuir una
  repetición de esas seis suites. No hubo skips nuevos.
- Los manifiestos de procedencia nuevos vinculan fuentes, binarios, staging y
  fixtures externos; `evidence-index.json` incorpora sus hashes y el de esta guía.

Fallo intermedio conservado: `recovery-Debug.txt` contiene 18 aprobados y un fallo
en la prueba anterior `interrupted(backup-renamed)`, antes de alcanzar la pausa.
El estado quedó en `staged` con candidate movido; el harness leía repetidamente
el journal mientras el proceso lo reemplazaba, lo que podía impedir su rename en
Windows. La prueba ahora espera un marcador escrito en la pausa antes de leer el
journal y conserva stdout antes de los asserts. Las seis ventanas nuevas ya habían
pasado en ese primer ensayo. No se atribuye aquel ensayo como aceptación final.

## DEV-01.4-A — Incremental native builds and bounded retention

**Estado:** ACCEPTED el 2026-09-25 tras la corrección selectiva. Ajuste solicitado por el propietario el 2026-09-24.
Se intercala entre DEV-01.4 ACCEPTED y
DEV-01.5. Esta sección prevalece sobre las políticas de reconstrucción/retención
descritas arriba únicamente para la nueva implementación documentada al final.
La aprobación funcional anterior permanece vigente.

### Problema y resultado esperado

En la base aceptada, Build fuerza dos Rebuild en directorios nuevos y recorre SDK, compilador,
MSBuild y Windows SDK antes/después de compilar. Esto elimina la reutilización de
objetos y añade I/O ajeno a un cambio pequeño. Retener cada intento/backup evita
pérdida de evidencia, pero produce crecimiento indefinido. La aceptación anterior
no estableció un presupuesto de latencia o almacenamiento de uso cotidiano.

El developer debe editar, pulsar Build y obtener el paquete actual en una ruta
estable. Mantener `.artest/native/<config>/current/package/` como salida publicada,
con DLL, manifiesto y schemas; el log de Build debe indicar esa ruta claramente.
No añadir copias paralelas ni nombres de DLL basados en hashes. Los identificadores
privados de transacción pueden permanecer, pero no deben acumularse ni ser un paso
que el usuario deba administrar. No basta entregar sólo una DLL.

### Decisiones autorizadas y límites

- **Build incremental:** objetos/intermedios estables por proyecto/configuración,
  separados para DLL y generador. Compilar/enlazar/generar sólo lo invalidado;
  Rebuild solicita reconstrucción completa. Sin cambios reales, verificar/reutilizar
  la salida sin crear una revisión ni reescribir metadatos. No desactivar el helper
  de forma que una edición del mismo tamaño/fecha se salte las comprobaciones.
- **Identidad e integridad:** conservar SHA-256 de entradas relevantes y salidas;
  seguir cambios reales de fuentes, headers, imports, opciones, configuración,
  SDK y herramientas que afecten al resultado. Evitar recorridos repetidos o
  ajenos a las dependencias efectivas. Documentar qué identidad se verifica en
  Build, inspección y validación contra destino. No usar sólo timestamps/tamaños,
  ni reetiquetar objetos antiguos con hashes de fuentes nuevas. Si una edición
  conserva timestamp, forzar las compilaciones dependientes que MSBuild omitiría.
- **Caché acotada:** reutilizar cálculos dentro de una operación sin perder la
  detección de mutaciones concurrentes; una caché persistente necesita invalidación
  verificable por contenido. Un acierto por fecha/tamaño no autoriza publicación
  ni validación. Mantener el inventario estricto del kit; no eliminar entradas ni
  actualizar hashes para legitimar corrupción. Si la optimización requiere otra
  política de confianza, reportarla antes de implementarla.
- **Retención nativa:** después de completar una operación y su limpieza quedan
  el resultado actual, directorios estables de intermedios y diagnóstico rotativo
  del último éxito y último fallo (sin versiones históricas de paquetes). Puede
  existir un candidato/backup durante la transacción. Retirar work/evaluation/
  validation obsoletos y árboles retirados sólo con ownership, inventario y fin
  confirmado de sus procesos. Sin ZIP históricos ni archivos de evidencia por
  cada Build. Mantener el límite existente de salida por proceso y un resumen
  acotado de la operación; reportar los límites efectivos de archivos/bytes.
- **Recuperación antes que limpieza:** nunca borrar current, estado activo/ambiguo,
  archivos desconocidos o material aún necesario para rollback. Separar el estado
  necesario para recuperar publicación del estado de eliminación de basura ya
  retirada. Si la eliminación se interrumpe, sólo un registro propio y verificable
  permite continuar sobre los restos esperados; no aceptar un inventario incompleto
  como paquete válido. Repetir la limpieza debe ser seguro. Si falla por permisos
  o archivos en uso, conservar los restos y dar diagnóstico; no fingir limpieza
  completa. El crecimiento excepcional por recuperación pendiente se informa,
  no se resuelve borrando a ciegas.
- **Clean:** retirar únicamente intermedios propios regenerables cuando no haya
  operaciones/recuperación pendientes. Conservar resultado publicado y configuración;
  informar esa política. El siguiente Build debe reconstruir los intermedios
  necesarios. Nunca limpiar fuentes, SDK instalado, otros proyectos o destinos.
- **Compatibilidad:** conservar el perfil aceptado y targets históricos. La prueba
  principal usa proyectos nuevos en un staging nuevo. No migrar silenciosamente
  proyectos previos ni barrer sus directorios work/ sin prueba de ownership y de
  que ya no participan en recuperación. Informar cómo recrear el proyecto con
  sus fuentes o detectar que requiere actualización; no editar el ejercicio del
  propietario durante pruebas. Python y sus entornos/recibos quedan fuera.

### Ejecución en unidades verificables

| Paso | Trabajo | Verificación |
| --- | --- | --- |
| A. Medición | Instrumentar duración de evaluación/hash, compilación DLL, metadatos y publicación/limpieza; contar archivos/bytes leídos y retenidos | Baseline de la implementación aceptada, sin atribuir tiempos de suites completas a un Build |
| B. Build incremental | Ajustar targets/helper y seguimiento de dependencias; conservar el estado publicado y validación delegada | Build sin cambios no lanza cl/link/generador; edits reales invalidan; Rebuild recompila |
| C. Retención | Directorios estables, diagnóstico rotativo y limpieza propia recuperable | Repetición no acumula revisiones; interrupciones preservan resultado/rollback |
| D. Gate | Matriz funcional y comparación antes/después | Evidencia del candidato actual; decisión independiente DEV-01.4-A |

Componentes principales: `NativeOutputs.cpp`, `resources/ARTestDevNative.targets`
y `tests/NativeTests.cpp`; `NativeService`, authoring, CMake/staging y pruebas de
fallo sólo cuando sea necesario para conectar o verificar el cambio. Mantener
C++20/Qt, procesos acotados, native-only, metadata-only y Engine separado.

### Acceptance criteria y pruebas

1. Debug/Release, driver-only/command-only/ambos desde SDK externo y sólo lectura,
   rutas con espacios, sin checkout/Python/PowerShell/CLI para compilar. Validación
   positiva y negativa mediante CLI separado; nunca registrar ni ejecutar Test plans.
2. Medir primer Build, Build sin cambios, edición de un fuente y Rebuild en la misma
   máquina/configuración/plantilla antes/después. Publicar al menos tres muestras y
   medianas por escenario, con estado de caché explícito. Usar copias aisladas;
   conservar sólo resumen del baseline e inputs/hashes necesarios, no otro archivo
   histórico completo. No fabricar una comparación con tiempos de otras máquinas.
3. PASS de rendimiento exige cero compilaciones/enlaces/generaciones ni nueva
   publicación en Build sin cambios y al menos 50% de reducción de su mediana
   frente al baseline; exigir mejora medible de la edición de un fuente. Objetivo
   de usabilidad inicial: mediana <=5 s sin cambios en la máquina de referencia.
   Un incumplimiento de ese objetivo debe informarse expresamente al Architect;
   nunca omitir controles para alcanzar un tiempo. Registrar primer Build/Rebuild
   y explicar regresiones fuera de la variación observada.
4. Edición de fuente/header/import/opciones, incluida misma longitud y timestamp
   restaurado; dependencias añadidas/eliminadas; SDK/toolchain/configuración
   cambiados; outputs ausentes/corruptos. Usar también un fixture de varios fuentes
   para demostrar recompilación selectiva. Verificar que el comportamiento o los
   metadatos cambian realmente, no sólo el registro de inputs.
5. Test plan/documentación sin dependencia efectiva del build no deben recompilar
   la DLL. Una dependencia real declarada como entrada sí invalida, cualquiera que
   sea su extensión. Cambios durante la operación no deben publicar una mezcla.
6. Repetir al menos 20 Builds sin cambios, 20 ediciones/Builds exitosos y comprobaciones
   de vigencia: contar directorios/archivos/bytes tras asentarse. No acumular work,
   evaluation, validation o paquetes retirados. El tamaño de intermedios puede
   depender del proyecto, no del número de Builds. Probar Clean seguido de Build.
7. Preservar las tres ventanas originales y las seis de limpieza de DEV-01.4;
   extenderlas a la eliminación interrumpida, primera publicación, rollback,
   payloads idénticos, concurrencia, permisos/archivos en uso, identidad ajena,
   corrupción y reparse points. Repetir recuperación/limpieza y publicar después.
8. Ejecutar checks de AGENTS y regresiones afectadas, con equivalencia del camino
   histórico/consumidor congelado sin regenerar baselines. Evidencia acotada del
   candidato actual: tiempos, conteos, logs de fallos, hashes y resultados; no
   confundir esta evidencia del gate con retención obligatoria por cada Build.

### Fuera de scope y entrega

No DEV-01.5, registro/run/hardware, optimización Python, pools globales, limpieza
fuera del proyecto propio, cambios Engine/ABI/API/IPC/recibos/manifiestos públicos,
instalador, Linux, Studio/.NET, refactors ajenos ni reparación ARTESTPKG015.
No commit/push. No contacto con otras tareas ni inicio automático del Developer.

Entregar archivos afectados, comparación antes/después, política/layout de
retención, pruebas y límites; actualizar esta guía con comportamiento realmente
implementado y un prompt compacto de revisión que solicite **DEV-01.4-A ACCEPTED**
o **DEV-01.4-A REQUIRES FIXES**. Un bloqueo real de integridad/compatibilidad se
reporta antes de ampliar el alcance; la aceptación anterior no sustituye este gate.

Nota documental: por instrucción del propietario se eliminaron evidencias y
stagings históricos de build y su ZIP de respaldo tras la aceptación. Las rutas
históricas anteriores describen el ensayo realizado, no garantizan disponibilidad
actual. Se mantiene `dev014-fix-evidence` como registro del candidato aceptado;
este cambio de guía es posterior y no debe presentarse como el documento con el
hash original de aquel ensayo. No regenerar evidencia antigua para ocultar cambios.

## Prompt para el Architect (DEV-01.4 original, ya resuelto)

Revise exclusivamente DEV-01.4 en `D:\GitHub\main\ARTestCLI`, leyendo AGENTS,
scope, roadmap y esta guía. Preserve el working tree previo y DEV-01.1/01.2/
01.2-A/01.3 ACCEPTED. Compruebe targets/helper privados, identidad por contenido,
publicación/recuperación por fase y topología, incluidas las seis ventanas de
limpieza de la corrección y su evidencia en `dev014-fix-evidence`, validación delegada al CLI separado,
contención asíncrona, matrices externas Debug/Release y hashes de evidencia.
No registre, ejecute Test plans, cambie contratos públicos o avance DEV-01.5.
Devuelva **DEV-01.4 ACCEPTED** o **DEV-01.4 REQUIRES FIXES**, con hallazgos
verificables y archivo/línea. No haga commit/push.

## Candidato inicial DEV-01.4-A (REQUIRES FIXES; evidencia histórica)

Este candidato no declara ACCEPTED ni modifica las aceptaciones anteriores.
Los resultados de este apartado corresponden al candidato inicial rechazado;
las pruebas de DEV-01.4 no sustituyen su gate. Evidencia nueva y acotada:
`source/ARTestDev/build/dev014a-evidence/`. El staging aceptado permanece separado.
Cambian `CMakeLists.txt`, `NativeMain.cpp`, `NativeOutputs.h/.cpp`,
`resources/ARTestDevNative.targets` y `tests/NativeTests.cpp` bajo
`source/ARTestDev/`; se añaden `NativeRetention.h/.cpp`. Se actualizan esta guía
y el roadmap. El diff tracked previo del propietario permanece idéntico.

### Implementación privada

- `Build` evalúa/verifica las entradas y el inventario de las salidas. Si coinciden
  con el resultado y los intermedios, devuelve `compiled`, `reused: true`, la misma
  revisión y la ruta absoluta `current/package`; no invoca compilador, linker ni
  generador. `DisableFastUpToDateCheck` sigue habilitado: el IDE no elude el helper.
- `intermediates/{obj,metaobj,dll,metadata}` tiene rutas estables por configuración.
  Los traces de lectura/escritura relacionan cambios SHA-256 con objetos y salidas
  de enlace. Se invalidan esas salidas incluso con longitud/fecha conservadas.
  Una raíz de escritura agrupada de MSBuild invalida conservadoramente todo el
  lote asociado: no presupone una correspondencia uno-a-uno con raíces de lectura.
  Altas/bajas de entradas, imports/opciones, SDK/toolchain y `Rebuild` exigen
  reconstrucción completa y retiro previo de los intermedios propios obsoletos.
- No hay caché persistente de hashes por fecha/tamaño. Durante una operación,
  los hashes se reutilizan sólo mientras un handle Win32 impide escritura y
  sustitución de cada entrada. Se vuelve a enumerar para detectar altas/bajas.
  SHA-256 de esas entradas usa Windows CNG, independiente de la configuración
  Debug/Release del helper; no cambia el algoritmo ni los hashes del inventario.
  El inventario completo y estricto del kit se sigue verificando. Se conserva
  una cobertura conservadora de los árboles seleccionados de herramientas,
  headers y bibliotecas; no se rebaja la identidad a una versión declarada.
- MSBuild devuelve los items/opciones evaluados, además de los imports
  preprocesados. Se excluyen únicamente sus metadatos administrativos
  `AccessedTime`, `CreatedTime` y `ModifiedTime`; la identidad de archivos procede
  de SHA-256. Documentos `.md`, `.txt` y `.json` no participan salvo que sean
  inputs declarados o dependencias observadas del compilador/linker. El item
  privado `ARTestDevInput` permite declarar una entrada de cualquier extensión.
  Los nombres de posibles includes y sus altas/bajas también invalidan.
- Inspección y validación contra CLI calculan la misma identidad y verifican
  ownership e inventario completo de `current`. La validación sigue delegando
  integridad/descriptores al CLI/Engine separado; no registra ni ejecuta planes.

### Retención y recuperación

Al asentarse quedan `current`, `intermediates`, el lock vacío y hasta dos JSON
rotativos: `last-success.json` y `last-failure.json`. No se mantienen paquetes
históricos. Los logs hijos conservan el límite de 1 MiB por proceso; cada resumen
retiene como máximo los tres logs de build o los dos de validación, además del
resultado. Los intermedios y sus inventarios dependen del proyecto, no del número
de operaciones. Los conteos/bytes medidos se informan en la evidencia del gate.

MSVC también acumula historial dentro de sus PDB, aun sin crear nuevos archivos.
`intermediates/budget.json` conserva el tamaño de los cuatro árboles de salida
tras una reconstrucción completa. Una edición puede retener como máximo ese
tamaño más `max(8 MiB, tamaño limpio / 2)`. Si lo supera, ambos destinos ejecutan Rebuild antes de publicar
y se registra la nueva base limpia del proyecto; `metrics.compacted` lo indica.
La compactación usa la misma transacción de build y mantiene `current` intacto
hasta publicar. No se compacta ni invoca el compilador en un Build sin cambios.
La cota es relativa al proyecto actual, no un límite absoluto al tamaño de su DLL;
incluye un coste periódico de reconstrucción en ciclos largos de edición.
Los conteos de archivos del fixture no son cuotas universales: dependen de las
fuentes y schemas del proyecto. Continúan los límites de 512 MiB por archivo
verificado, 32 MiB al leer un JSON privado y 1 MiB de salida por proceso.

`NativeRetention` separa el borrado del journal de publicación. Sólo registra
retiro después de finalizar procesos y resolver la publicación. El registro
canónico conserva propietario, identidad NTFS del directorio e inventario completo
verificado. Una recuperación de borrado acepta únicamente un subconjunto exacto
de ese inventario: nunca considera ese subconjunto un paquete válido. Verifica
de nuevo el contenido bajo un handle que permite eliminar e impide reemplazos.
Archivos desconocidos, corrupción, directorio sustituto, reparse points, permisos
y archivos en uso conservan los restos y producen fallo; no se legitiman mediante
un inventario nuevo. El crecimiento excepcional pendiente se conserva para resolverlo.

`Clean` conserva el paquete publicado y la configuración; retira sólo intermedios
propios, sin transacciones pendientes. El siguiente Build los reconstruye. Estados
antiguos sin ownership de scratch se conservan y requieren recrear un proyecto
con el SDK actualizado y trasladar sus fuentes. No hay migración o barrido de
proyectos históricos. Una interrupción con procesos aún ambiguos sigue bloqueando
el proyecto, como en DEV-01.4; un journal de borrado no autoriza saltar ese bloqueo.

### Medición y gate

Las dos matrices finales (`native-current-Debug.txt` y `native-current-Release.txt`)
pasan 32 pruebas cada una, sin fallos ni skips. Incluyen tres variantes C++, SDK
externo con ACL de sólo lectura, rutas con espacios y auditoría de procesos;
no hay Python/PowerShell ni CLI/Engine en los descendientes del Build. La validación
explícita contra el CLI separado verifica inventarios y descriptores. Pasan cambios
de fuentes con tamaño/fecha conservados, headers, include `.txt`, imports/opciones
y entradas declaradas, documentos sin dependencia, salida ausente/corrupta,
SDK corrupto/selección distinta, compilador fallido, CLI ausente/incompatible,
concurrencia, cancelación, tres ventanas originales y seis de limpieza, ventana
adicional tras sellar work y borrado parcial recuperable/idempotente.
Una prueba adicional conserva el FILETIME completo de Windows (100 ns) al editar
fuente y header, exige nuevos hashes de ambos objetos (DLL y generador) y verifica
el contenido publicado. Un `TargetName` importado después de los targets privados
se respeta en Build/Clean/Rebuild y valida contra el Engine. Los argumentos se
expanden al ejecutar cada Exec, no anticipadamente en una propiedad global.

Retención del fixture de dos unidades C++, después de cada operación asentada:

| Configuración | Archivos / directorios | Inicial y tras 20 sin cambios (bytes) | Máximo de 20 ediciones (bytes) | Tras edición 20 (bytes) | Compactaciones |
| --- | --- | --- | --- | --- | --- |
| Release | 52 / 16 | 30 010 607 | 43 142 410 | 38 738 794 | 2 |
| Debug | 54 / 16 | 58 877 265 | 82 658 891 | 58 882 291 | 5 |

Los 20 Builds sin cambios conservan revisión, archivos y bytes y no ejecutan
compilador/linker/generador. Las 20 ediciones restauran timestamp y mantienen
longitud; cada manifiesto refleja su texto nuevo. Clean/Build conserva el paquete
y vuelve a 30 010 607 bytes (Release) / 58 877 265 bytes (Debug). Después de las
pruebas adicionales de dependencias quedan 30 021 736 / 58 888 602 bytes, con los
mismos conteos. No quedan work/evaluation/validation/backup en esos estados.
Los fixtures de fallo preservan deliberadamente estados ambiguos y datos ajenos;
no se incluyen como crecimiento ordinario de un Build exitoso.

Los comandos raíz obligatorios Debug/Release pasan: 242 tests aprobados y 31
deshabilitados por configuración, además de ABI, thin host, consumidor histórico
y consistencia XML/HTML. Las tres regresiones CTest afectadas pasan en cada
configuración. El consumidor SDK 0.2.1 ABI 0.1 congelado pasa 8/8 casos contra cada
Engine, sin reconstruirlo; su manifiesto mantiene SHA-256
`6b13eec226ab1207432776428896dbd504f8c7ee7fd6a87fb5a9bf8452cae1c0`.
No hay warnings C++; permanece la advertencia preexistente de traducciones de
windeployqt (se despliega con `--no-translations`).

La comparación final usa tres muestras por escenario/configuración, proyectos
independientes y MSBuild exterior supervisado. Las matrices funcionales habían
terminado antes del benchmark; Release y Debug se midieron secuencialmente.
Medianas en segundos (antes → después):

| Escenario | Release | Debug |
| --- | --- | --- |
| Primer Build | 49,726 → 17,927 | 91,108 → 22,847 |
| Sin cambios | 49,362 → 4,643 | 90,924 → 10,624 |
| Edición | 48,944 → 17,823 | 89,384 → 24,245 |
| Rebuild | 51,927 → 18,266 | 87,770 → 27,988 |

Muestras finales ordenadas, en ms:

| Configuración | Primero | Sin cambios | Edición | Rebuild |
| --- | --- | --- | --- | --- |
| Release | 17868 / 17927 / 20927 | 4622 / 4643 / 4644 | 17523 / 17823 / 17884 | 18253 / 18266 / 18347 |
| Debug | 22769 / 22847 / 22928 | 10485 / 10624 / 10645 | 24030 / 24245 / 24404 | 27648 / 27988 / 28908 |

Sin cambios mejora 90,59% / 88,32%; al editar mejora 63,58% / 72,88%
(Release / Debug). Ambas configuraciones superan el mínimo del 50% sin cambios
y tienen mejora medible al editar. **Release cumple ≤5 s; Debug no lo cumple:
10,624 s de mediana.** Es una limitación explícita del candidato para decisión
del Architect, no una aceptación implícita ni una cifra sustituida por Release.
Las ediciones que activan compactación pagan una reconstrucción adicional;
las tres muestras de edición son el primer cambio después del Build inicial.

`performance-summary.json` conserva las tres muestras antes/después y porcentajes;
`phase-comparison.json` compara evaluación/hash, DLL, metadatos, verificación y
publicación. Sin cambios, evaluación/hash baja de 17,434 a 4,173 s (Release) y
40,859 a 9,894 s (Debug); DLL/metadatos/publicación pasan a cero. Los timers de
fase no suman necesariamente el total: éste incluye MSBuild exterior, preparación
de intermedios, diagnóstico y supervisión. Los contadores instrumentados de
hashing bajan de 2.994.774.425 a 1.521.806.715 bytes (Release) y de 3.088.177.591 a
1.594.107.783 (Debug). No son I/O físico total ni incluyen el verificador separado
del kit o el rehash de borrado. Las nueve categorías de herramientas mantienen
exactamente sus hashes SHA-256 frente al baseline (`toolchain-content-equivalence.json`).

`validation-summary.json`, `matrix-current/` y `benchmark-current-data/` conservan
conteos, inventarios, pruebas FILETIME y 228 auditorías de procesos completas.
`candidate-source-hashes.json`, `candidate-binary-hashes.json` y
`evidence-hashes.json` identifican las fuentes y evidencias. Los intentos excluidos
están en `failure-history.json`: timestamps administrativos de MSBuild, escritura
de evidencia concurrente, invalidación de lotes y crecimiento interno de PDB,
todos corregidos; el enlace tardío de argumentos tiene regresión explícita.
No se cuentan los ensayos cancelados o anteriores como resultados finales.

### Prompt del candidato inicial (sustituido por la corrección siguiente)

Revise exclusivamente DEV-01.4-A en `D:\GitHub\main\ARTestCLI`. Lea AGENTS,
scope, roadmap, el amendment y el apartado de candidato de esta guía. Preserve
el trabajo previo y DEV-01.1/01.2/01.2-A/01.3/01.4 ACCEPTED. Compruebe Build
incremental/Rebuild/Clean, identidad por contenido y cambios con timestamp
restaurado, SDK externo sólo lectura, cero compilaciones/publicaciones sin
cambios, matriz Debug/Release y tres variantes, tiempos y retención tras 20+20
operaciones, las nueve ventanas anteriores y el borrado parcial recuperable.
Verifique hashes de la evidencia del candidato y los límites declarados. No
registre, ejecute planes, modifique contratos públicos ni avance DEV-01.5.
Evalúe explícitamente el objetivo de usabilidad incumplido en Debug (10,624 s
sin cambios), la compactación de PDB y los tests de FILETIME exacto/import tardío.
Devuelva **DEV-01.4-A ACCEPTED** o **DEV-01.4-A REQUIRES FIXES**, con hallazgos
reproducibles y archivo/línea. No haga commit/push ni contacte otras tareas.

## Corrección DEV-01.4-A — asociación selectiva de objetos (2026-09-25)

El Architect marcó el candidato inicial **REQUIRES FIXES**: una edición de
`Extension.cpp` invalidaba también `Independent.obj` cuando MSBuild agrupaba
ambos fuentes en una raíz de escritura. Las pruebas anteriores no comprobaban
la supervivencia del objeto independiente. Los resultados del apartado anterior
se conservan como históricos; no prueban la corrección de ese blocker.

La corrección cruza las dependencias de `CL.read.1.tlog`, los objetos de
`CL.write.1.tlog` y la asociación explícita fuente/objeto de `CL.items.tlog`.
Estos archivos pertenecen al inventario de intermedios verificado antes de
invalidar. Normaliza separadores y case; no adivina el objeto por basename ni
por posición dentro del lote. Sólo estrecha la invalidación si la asociación
es unívoca y consistente con las fuentes y objetos de la raíz de escritura.
Si falta el mapa o es ambiguo/incompleto, conserva la invalidación del lote.
Los cambios siguen detectándose por SHA-256, también con tamaño y FILETIME
idénticos. No cambia publicación, rollback, ownership ni limpieza/compactación.

La regresión `lateBoundArguments` incluye dos unidades de compilación. Mientras
edita primero el fuente y después su header conservando FILETIME exacto, mantiene
`Independent.obj` de DLL y generador abierto con `FILE_SHARE_READ`: el Build
falla si intenta escribirlo o eliminarlo. Exige cambio de hash de ambos
`Extension.obj`, invariancia de los independientes y `compacted: false`.
Comprueba también los metadatos publicados. Un Rebuild posterior debe reconstruir
los cuatro objetos; Clean/Build e import tardío siguen cubiertos. En las 20
ediciones de retención se verifica hash y fecha de los independientes en todas
las iteraciones sin compactación.

El nuevo oráculo falla con el helper anterior exactamente al intentar eliminar
`Independent.obj` al editar sólo `Extension.cpp` (`red-source-Release.txt`) y pasa con la corrección. Los
ensayos focalizados iniciales no sustituyen las matrices finales. La evidencia
de esta revisión está separada en
`source/ARTestDev/build/dev014a-selective-evidence/`; el staging es
`source/ARTestDev/build/dev014a selective staging/{Debug,Release}`.
Sólo cambian `NativeOutputs.cpp`, `tests/NativeTests.cpp`, esta guía y roadmap
respecto del candidato revisado. La revisión posterior del Architect aceptó esta corrección el 2026-09-25; véase el cierre al final.

### Gate del candidato corregido

Las matrices definitivas pasan **32 pruebas por configuración, 0 fallos y 0
skips**. Incluyen las tres variantes, SDK externo de sólo lectura, imports tardíos,
FILETIME exacto, corrupción, concurrencia y las ventanas de recuperación anteriores.
Se verificaron **230 auditorías de procesos**: 103 de matriz y 12 de benchmark
por configuración, sin descendientes prohibidos/incompletos ni compilación,
enlace, generación o nueva revisión en los 20 Builds sin cambios.
Los checks raíz vuelven a pasar con 242 tests y 31 deshabilitados por configuración;
CTest pasa 3/3 por configuración y el consumidor congelado pasa 8/8 contra cada
Engine, sin regenerar el baseline. Las nueve categorías de toolchain conservan
sus SHA-256 frente al baseline original.

Retención final del mismo fixture de dos fuentes, sin cambiar el presupuesto:

| Configuración | Archivos / directorios | Inicial y 20 sin cambios (bytes) | Máximo de 20 ediciones (bytes) | Tras edición 20 (bytes) | Compactaciones |
| --- | --- | --- | --- | --- | --- |
| Release | 52 / 16 | 30 010 605 | 43 095 868 | 38 705 476 | 2 |
| Debug | 54 / 16 | 58 877 265 | 82 640 987 | 58 882 291 | 5 |

Clean/Build vuelve a los bytes iniciales. Después de las pruebas adicionales de
dependencias quedan 30 021 734 / 58 888 602 bytes (Release / Debug), con los mismos
conteos y sin work/evaluation/validation/backup acumulados. La compactación sigue
reconstruyendo ambos destinos periódicamente; las ediciones sin compactación
conservan los objetos independientes. Se preservan los fixtures ambiguos de fallo.

Medianas en segundos, baseline anterior al incremento → candidato corregido:

| Escenario | Release | Debug |
| --- | --- | --- |
| Primer Build | 49,726 → 17,488 | 91,108 → 22,707 |
| Sin cambios | 49,362 → 4,584 | 90,924 → 10,583 |
| Edición | 48,944 → 17,603 | 89,384 → 24,264 |
| Rebuild | 51,927 → 18,287 | 87,770 → 27,549 |

Tres muestras finales ordenadas por escenario, en ms:

| Configuración | Primero | Sin cambios | Edición | Rebuild |
| --- | --- | --- | --- | --- |
| Release | 17387 / 17488 / 17549 | 4574 / 4584 / 4603 | 17543 / 17603 / 17624 | 18255 / 18287 / 18387 |
| Debug | 22668 / 22707 / 22767 | 10464 / 10583 / 10604 | 24184 / 24264 / 24363 | 27528 / 27549 / 30069 |

Mejora sin cambios: **90,71% / 88,36%**; al editar: **64,03% / 72,85%**.
Ambas configuraciones cumplen los requisitos obligatorios de rendimiento.
**Debug sigue incumpliendo el objetivo de ≤5 s: 10,583 s de mediana.** Release
lo cumple. El benchmark mantiene la plantilla de un fuente para comparabilidad
con el baseline; la selectividad se demuestra con el fixture de dos fuentes.
No se atribuye la mejora frente al baseline íntegramente a esta corrección.
Las ediciones medidas son las primeras después del Build inicial, sin compactación.

Las matrices terminaron antes de medir; Release y Debug se midieron secuencialmente,
con cachés del sistema calientes y tres proyectos independientes por configuración.
`performance-summary.json` conserva todas las muestras antes/después y
`phase-comparison.json` conserva evaluación/hash, DLL, metadatos, verificación y
publicación. Sin cambios, evaluación/hash pasa de 17,434 a 4,111 s (Release) y
40,859 a 9,871 s (Debug); DLL/metadatos/publicación son cero. Se mantienen las
salvedades anteriores sobre contadores instrumentados frente a I/O físico total.

`README.md`, `validation-summary.json`, `matrix-current/`, `benchmark-current-data/`,
`candidate-source-hashes.json`, `candidate-binary-hashes.json`, `document-hashes.json`
y `evidence-hashes.json` identifican el candidato actual. `correction.patch` aísla
los dos cambios C++ respecto del candidato rechazado. `failure-history.json`
registra los controles rojos esperados y dos capturas stdout vacías: ambas
ejecuciones devolvieron 0, pero se repitieron con informe QTest directo y no se
usan como informes definitivos. No hubo cambios de código por esa repetición.
El trabajo tracked previo sigue idéntico y los otros seis fuentes del candidato
anterior conservan sus hashes. No hubo commit/push ni contacto con otras tareas.

### Prompt compacto para el Architect — candidato corregido

Revise exclusivamente la corrección DEV-01.4-A en `D:\GitHub\main\ARTestCLI`.
Lea AGENTS, scope, roadmap y esta guía; preserve DEV-01.1/01.2/01.2-A/01.3/01.4
ACCEPTED. Contraste `NativeOutputs.cpp`, `tests/NativeTests.cpp` y la evidencia
`source/ARTestDev/build/dev014a-selective-evidence/`. Verifique el cruce CL.items/
read/write, la invalidación por contenido con FILETIME idéntico y la conservación
de objetos independientes en DLL y generador sin compactación. El control rojo
con el helper anterior debe fallar; el candidato debe pasar y Rebuild reconstruir
los cuatro objetos. Revise 32 pruebas por configuración, 230 auditorías, retención
20+20, recuperación, hashes y tres muestras por escenario. Evalúe explícitamente
el objetivo de 5 s incumplido en Debug (10,583 s) y la compactación periódica.
Devuelva **DEV-01.4-A ACCEPTED** o **DEV-01.4-A REQUIRES FIXES**, con hallazgos
reproducibles, archivo/línea y correcciones obligatorias. No implemente cambios,
registre paquetes, ejecute Test plans, haga commit/push, avance DEV-01.5 ni contacte
otras tareas.

## Cierre del Architect (2026-09-25)

**DEV-01.4-A ACCEPTED.** El cruce CL.items/read/write conserva objetos
independientes de DLL y generador con FILETIME idéntico; el control rojo falla
con el helper anterior y Rebuild reconstruye los cuatro objetos. Se verificaron
32 pruebas por configuración, 230 auditorías, retención 20+20 y recuperación,
con hashes coincidentes de 8 fuentes, 14 binarios/descriptores y 625 evidencias.

Release sin cambios: 4,584 s; Debug: 10,583 s. Ambos cumplen la mejora mínima
del 50%; Debug no cumple el objetivo de usabilidad de 5 s. La compactación
periódica (2/20 Release, 5/20 Debug) añade reconstrucción completa y no está
incluida en las medianas de la primera edición. Son limitaciones documentadas,
no blockers pendientes. Las etapas anteriores permanecen ACCEPTED.

Este cierre sólo actualiza documentación; no cambia el código aceptado ni
los registros de evidencia. Sus hashes documentales identifican la versión
revisada anterior a esta anotación. DEV-01.5 permanece pendiente de un handoff
separado; no se implementó registro ni ejecución de Test plans en este cierre.
