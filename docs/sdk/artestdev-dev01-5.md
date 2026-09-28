# DEV-01.5 — Integrate to ARTestCLI

**DEV-01.5 ACCEPTED** por el Architect el 2026-09-28, incluidas las correcciones
de Clear. Base de implementación: `5f3c022ac3a050dee278878730491b3a8d1421bc`.
Se preservan DEV-01.1–01.4-B, los IDs legibles y la evidencia manual previa.
El propietario autoriza commit/push de esta unidad; DEV-01.6/01.7 no se inician.

## Uso

1. Genere o abra un proyecto y edite sus fuentes mediante Edit. Para C++, haga
   Build en Visual Studio de la configuración que integrará (Debug o Release).
2. Abra **Integrate → To ARTestCLI**. Seleccione un perfil guardado/candidato o
   use **Seleccionar carpeta… / Seleccionar ARTestCLI.exe…**. Si hay varios
   candidatos, la selección es explícita. El diálogo muestra CLI, catálogo y
   configuración; encontrar un ejecutable no acredita compatibilidad.
3. Pulse **Integrate**. Python se prepara/reutiliza internamente con el intérprete
   local elegido; C++ consume outputs del IDE y exige Build si faltan, cambiaron
   sus entradas o no superan integridad/validación. No se recompila automáticamente.
4. El resultado muestra destino, revisión y diagnóstico. CONFIRMADO exige
   publicación y descubrimiento instalado. Cancelar solicita terminación y espera
   recuperación antes de permitir cerrar/reintentar.

En los paneles de mensajes de autoría, Integrate y de inspección tras **Load**, haga clic derecho → **Clear** para vaciar
los mensajes visibles, incluso durante una operación. Los mensajes nuevos siguen
apareciendo; Clear no cancela ni cambia el estado/resultado, ni borra logs o evidencia.

**To ARTestStudio** permanece visible y deshabilitado. No hay búsqueda de Studio,
Test plan automático, hardware, instalación de dependencias externas, elevación,
reemplazo de runtime DLLs ni cambios de Engine/Core/ABI/API/IPC.

## Orquestación y formatos

`IntegrationDialog` fija la selección durante la operación. `IntegrationService`
trabaja fuera del hilo UI, usa `PreparationService`, `NativeService` y `TreeProcess`
y no ejecuta shell/PowerShell. Se verifican recursos del SDK, proyecto, capacidades
CLI y los informes Engine existentes. Para el catálogo completo se usan
`extensions validate` y `compile` de un documento intrínseco de descubrimiento,
como `registration.ps1`; no se ejecuta ese documento. `NativeService` conserva
además la validación de descriptores nativos. El comando CLI `extensions doctor`
no acepta asociaciones Python: no se le inventan argumentos ni se modifica CLI.

Los perfiles mantienen `artest.schema.sdk-installations.v1`; el estado conserva
`artest.schema.sdk-registration-state.v1`; asociaciones y manifiestos no cambian.
Los perfiles históricos conservan sus rutas. Los nuevos perfiles manuales usan
una raíz local por instalación en `ARTest/sdk-authoring/i/cli-<hash>/`, separada
 del runtime y del SDK/proyecto. Se actualiza `selected` sólo tras descubrimiento
instalado. La búsqueda está limitada a 64 perfiles, 32 entradas PATH y dos rutas
conocidas; no recorre discos ni busca Studio. La ausencia de instalación ofrece
selección manual, no instalación/descarga automática. Si varios perfiles comparten
un mismo ejecutable, se elige el perfil explícitamente en la lista; la selección
manual del exe no elige arbitrariamente uno.

Python delega `output_root` al tooling existente; las revisiones/venvs/recibos se
crean directamente bajo `configuration/r/<id>/py`. No se mueven entornos ni se
reescriben recibos/launchers. Sin `output_root`, PreparationService conserva su
comportamiento de proyecto aceptado. Los paquetes C++ se copian con inventario a
una revisión propia de la instalación y se vuelve a comprobar la salida del IDE.
Renombrar presentación/Description no cambia IDs. La similitud utiliza el
comparador aceptado y sólo emite avisos; ownership y validación resuelven conflictos.

## Publicación y recuperación

Se comparten los locks exclusivos Win32 de `registration.ps1`, tanto para perfiles
como para cada destino. Antes de reemplazar se comprueba el inventario del paquete
registrado y se valida un candidato con el catálogo completo, incluidas las
asociaciones y los paquetes de otros propietarios. Una repetición conserva la
revisión y el registro anterior si los bytes son iguales.

Se conserva el journal histórico de 13 campos y sus fases. Los snapshots guardan
bytes exactos del estado, asociaciones y perfil para rollback. La fase y la
topología determinan si se promovió; la igualdad de inventarios nunca decide sola.
Un cursor privado `.artest-register-transaction.json.recovery` permite reanudar
los pasos unpromote → restore → files → done. Cada rename y restauración puede
repetirse tras interrupción. El journal y el cursor terminados se archivan.

No se eliminan árboles durante rollback: se renombran y se conservan candidatos,
backups y evidencia retirada. No hay limpieza global ni promesa de retención
acotada del registro en esta unidad. Los estados corruptos, reparse points,
archivos ajenos y topologías ambiguas se preservan y bloquean el intento con
una indicación de inspección. No se promete disponibilidad atómica, durabilidad
ante corte eléctrico, hot reload ni modificación de sesiones activas.

Límites: JSON 8 MiB/64 niveles, inventarios 8192 entradas/2 GiB y archivo de hash
512 MiB; cada proceso CLI 120 s y salida 1 MiB. Los adaptadores conservan sus
límites de preparación/validación. Una terminación no confirmada se comunica y
mantiene el servicio/locks ocupados hasta observar asentamiento. La cancelación
no convierte un estado incierto en éxito ni libera un escritor prematuramente.

## Verificación y procedencia

Evidencia local: `source/ARTestDev/build/dev015-evidence/`. Staging independiente:
`source/ARTestDev/build/dev015-staging/{Debug,Release}`. No se modifican inventarios
ni evidencia de los stagings ACCEPTED. Los fixtures temporales se retienen y sus
rutas aparecen en los logs QtTest.

Resultados finales (detalles y fallos previos en el [índice de evidencia](../../source/ARTestDev/build/dev015-evidence/README.md)):

| Verificación | Debug | Release | Evidencia |
|---|---:|---:|---|
| Gate raíz AGENTS | exit 0; 243 PASS / 31 DISABLED | exit 0; 243 PASS / 31 DISABLED | `root-*.txt`, `root-exits.json` |
| Matriz Integrate | 43 PASS / 0 FAIL / 0 SKIP | 43 PASS / 0 FAIL / 0 SKIP | `integration-*-delivery.txt` |
| Selección GUI | 4 PASS / 0 FAIL / 0 SKIP | 4 PASS / 0 FAIL / 0 SKIP | `integration-ui-*-delivery-3.txt` |
| Cancelación publicada y salida nativa ausente | 3 PASS / 0 FAIL / 0 SKIP | 3 PASS / 0 FAIL / 0 SKIP | `cancellation-*.txt` |
| Regresión CTest existente | 6/6 en dos llamadas; preparación 17 PASS | 6/6; preparación 17 PASS | `regression-*.txt`, `preparation-*.txt` |
| Tooling Python común | 66 PASS / 2 SKIP, más 5 PASS de package | misma ejecución común | `python-project.txt`, `python-package.txt` |

Las matrices cubren las seis combinaciones Python/C++ × driver/command/both,
registro inicial, repetición y actualización. Incluyen command-only con driver
de otro paquete; proyecto original apartado; destino ausente/incompatible;
permisos denegados y escritor concurrente; outputs corruptos/obsoletos;
colisión de ownership y preservación de archivos ajenos. Son 24 cortes de fase/
recuperación con inventarios iguales/diferentes, más ocho procesos publicadores
terminados antes/después de promover con/sin catálogo anterior. Los cortes de
recuperación reproducen el estado exacto entre renombres y restauraciones y
repiten recover dos veces. No equivalen a una prueba de corte eléctrico.

La auditoría Windows Job exige todos los procesos observados (`observed=expected`),
sin imágenes sin resolver ni PowerShell/pwsh. Las solicitudes C++ llevan Python
vacío y rechazan cualquier descendiente Python. La configuración anterior se
compara byte por byte tras los fallos/cancelación. Preparación también prueba
terminación no confirmada, salida acotada y handshake de publicación.

Los dos skips Python son enlaces simbólicos no permitidos por Windows (WinError
1314), sin elevación. Las 31 pruebas deshabilitadas de cada gate son opcionales,
no pases. La suite larga `ARTestDevNativeTests` completa no se repitió;
`NativeOutputs`/`NativeRetention` no cambian. Las matrices nativas reales,
regresión native-only y gates AGENTS sí se ejecutaron. La activación de workers
Python, hardware, sesiones activas y otras versiones de runtime quedan fuera de
esta evidencia; el catálogo Python se comprueba mediante preparación y compilación
offline, conforme al registro histórico Stage 4C.

Se preservan fallos de desarrollo: CMake inicialmente fuera de PATH; errores de
compilación corregidos; aserción de separadores Windows; config de un driver
Python de fixture; dos matrices Debug interrumpidas al revisar producción;
fixture de QFileDialog bloqueado y posterior timeout; ruta incorrecta del primer
suplemento. El índice detalla qué logs fueron sustituidos y cuáles acreditan la
entrega. No se ocultan esos intentos ni se suman a los resultados finales.

Trazabilidad en [delivery-provenance.json](../../source/ARTestDev/build/dev015-evidence/delivery-provenance.json):
SHA-256 de fuentes, binarios, CLI/Engine externos, Python y wheelhouse existente;
verificación de los 54 archivos de cada staging. Se preservan los binarios y la
fuente exacta de las matrices de 43 casos. El test final agrega el suplemento de
cancelación/salida ausente y CTest lo incorpora en futuras matrices de 44 casos;
no cambia producción. Los árboles temporales completos retienen recibos, journals,
requests, logs y auditorías. [commands.md](../../source/ARTestDev/build/dev015-evidence/commands.md)
permite reproducir los gates. El paquete histórico generado por el gate raíz no
es el SDK authoring entregado: éste es `dev015-staging/{Debug,Release}`, sin
CLI/Engine/Python runtime. La evidencia generada no se incorpora a Git.

Archivos afectados:

- Nuevos `IntegrationDialog.h/.cpp`, `IntegrationService.h/.cpp`,
  `RegistrationStore.h/.cpp`, `tests/IntegrationTests.cpp` y
  `tests/IntegrationUiTests.cpp`.
- Menú `Main.cpp`, accesores `AuthoringWidget.h/.cpp`,
  `PreparationService.h/.cpp`, `NativeService.h`, adaptador
  `resources/prepare.py` y targets de `CMakeLists.txt`.
- Esta guía. Sin cambios de Engine/Core, SDK público, formatos o registro histórico.

Referencias de comportamiento: `source/ARTest.SDK/development-kit/registration.ps1`
y `scripts/test-development-kit-stage4c.ps1`, `artestdev-dev01-3.md`, `artestdev-dev01-4.md`,
`artestdev-dev01-4-b.md`, scope y roadmap DEV-01. No se invoca el script histórico
desde Integrate; únicamente se porta su orquestación de perfiles/transacción.

## Complemento de usabilidad: Clear

`DiagnosticMenu.h` añade Clear al menú contextual estándar de los paneles de
autoría e Integrate. Sólo vacía el documento visible; no cambia operaciones,
indicadores, resultados ni archivos. Las pruebas UI ejercitan el menú con un
evento de ratón, limpieza, mensajes posteriores y conservación de controles/evidencia.
Resultados, intento inicial con entorno Python incompleto y hashes de este
complemento: [evidencia Clear](../../source/ARTestDev/build/dev015-clear-evidence/README.md).
Se conserva el staging original; el actualizado está en `dev015-clear-staging`.

## Corrección Architect: Clear por inspección

`Main.cpp` aplica `DiagnosticMenu.h` al panel mostrado tras Load. Clear oculta
los mensajes de la inspección actual: una actualización visual de esa misma
información no los restaura. Una nueva acción **Load** o **Volver a comprobar**
inicia otra inspección y restablece la presentación de sus resultados, aunque
repitan exactamente el texto borrado. La supresión no dura toda la ventana.
El reinicio se realiza al comenzar `openProject`, después de su guardia de
operación activa; no se realiza en `updateView` ni en la llegada de resultados.

Es un filtro de presentación: Clear conserva proyecto, resumen, estado, acciones
y archivos. `MainUiTests::loadInvalidClearAndSubsequentDiagnostics` tiene dos
casos (`new-Load` y `new-recheck`). Cada uno carga una carpeta inválida mediante
el selector real, limpia desde el menú contextual, ejecuta un refresco encolado
de la misma información y exige el panel vacío. Después inicia la acción
explícita correspondiente y exige recuperar exactamente el texto anterior.
También comprueba mensajes distintos tras cargar un manifiesto corrupto,
conservación de indicadores/selección/acciones y un archivo de evidencia intacto.

Evidencia actual Debug/Release y trazabilidad:
[dev015-clear-scope-evidence](../../source/ARTestDev/build/dev015-clear-scope-evidence/README.md).
Staging: `dev015-clear-scope-staging`. La evidencia anterior
`dev015-main-clear-evidence` queda conservada como histórica; su expectativa de
suprimir también tras una nueva comprobación está sustituida por este fix.
Este ajuste afecta sólo `Main.cpp`, `tests/MainUiTests.cpp` y esta guía.
Integración, publicación, recuperación y el helper compartido no cambian.

## Cierre del Architect

La revisión final verificó las 20 fuentes/documentos del candidato compuesto,
los hashes del complemento final y ambos inventarios de staging sin discrepancias.
MainUiTests: 4 PASS/0 FAIL/0 SKIP en Debug y Release, distinguiendo refresco visual
de nuevas acciones Load/Volver a comprobar con el mismo diagnóstico. Los gates raíz
conservan 243 PASS y 31 DISABLED por configuración. Se reutiliza la evidencia
anterior aplicable de integración/recuperación y de los otros paneles Clear;
sus servicios permanecen intactos. No quedan blockers conocidos en este scope.

Las actualizaciones de aceptación en esta guía, AGENTS y roadmaps son posteriores
al sellado y sólo documentales; no sustituyen hashes ni evidencia histórica.

## Prompt histórico para Architect

Revise exclusivamente DEV-01.5 en `D:\GitHub\main\ARTestCLI`, base `5f3c022`.
Lea AGENTS, scope/roadmap, esta guía y los resultados/procedencia DEV-01.5.
Contraste UI explícita, selección manual/persistida, servicios reutilizados,
Python en rutas finales, C++ sin rebuild, validación completa, ownership,
exclusión, rollback de perfil y recuperación interrumpida con inventarios
idénticos. Revise los fallos/skips y la cobertura real antes de aceptar.
Preserve etapas ACCEPTED y cambios ajenos; no ejecute hardware ni Test plans,
no avance DEV-01.6/01.7 y no haga commit/push. Devuelva **DEV-01.5 ACCEPTED** o
**DEV-01.5 REQUIRES FIXES**, con hallazgos verificables por archivo/línea/evidencia.
