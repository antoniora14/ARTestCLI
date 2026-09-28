# Generate — identificadores legibles antes de DEV-01.5

Estado (2026-09-27): ACCEPTED por el Architect, incluida la corrección de
sustitución en una pasada y su evidencia Debug/Release. DEV-01.1/01.2/01.2-A/
01.3/01.4/01.4-A permanecen ACCEPTED; DEV-01.5 sigue pendiente.

[DEV-01.4-B — Autoría simplificada e identidades administradas](artestdev-dev01-4-b.md)
también está ACCEPTED; simplifica la edición posterior en código con Description
opcional y conserva IDs históricos. Esta guía mantiene el comportamiento aceptado
de Generate y sus pruebas. La siguiente implementación autorizada es DEV-01.5.

El formulario acepta el nombre del proyecto y de cada componente seleccionado.
Por ejemplo, `Bench Power`, `Power Supply` y `Power-On` sugieren `bench-power`,
`bench-power.power-supply` y `bench-power.power-on`, igual en Python y C++.
Los nombres visibles mantienen espacios y mayúsculas. Los valores iniciales de
componentes son `Simulated Source` y `Measure Value`.

Se reutiliza la normalización de Generate: descomposición Unicode, eliminación
de marcas diacríticas, ASCII en minúsculas y separación con guiones. Un nombre
sin letras/números ASCII se rechaza; no se sustituye por un nombre genérico.
Se conserva el prefijo `extension-` para nombres que comienzan con un número.
Una extensión de una sola palabra recibe el sufijo descriptivo `-extension`
porque el validador Python existente exige al menos un punto o guion. No cambia
ese validador ni ningún formato público.

Los IDs sugeridos son visibles. **Opciones avanzadas: editar IDs** permite
sustituirlos; los campos editados se mantienen al cambiar nombres. Los componentes
sin override siguen el ID de extensión seleccionado. **Restaurar IDs sugeridos**
descarta los overrides del formulario. Desmarcar opciones avanzadas sólo impide
editar: conserva los valores. Los componentes no seleccionados están deshabilitados.
IDs vacíos, mayúsculas, separadores inválidos y duplicados impiden Generate.

Generate fija los IDs una vez y escribe las referencias en fuentes, configuración
y Test plan. Metadata/schema generation y preparación siguen los mecanismos
aceptados. Abrir, Edit y Build no recalculan ni migran los IDs, incluidos los
históricos `local.<valor>`. Se conservan UUID privados de staging y Visual Studio.
No se reemplazan destinos existentes ni se añade azar para resolver colisiones.
La comprobación local consulta sólo `artest-sdk-project.json` de subdirectorios
inmediatos del workspace (máximo 1024), sin registro ni búsqueda de instalaciones.
No es una reserva global de IDs ni una garantía frente a creaciones concurrentes
en destinos diferentes. Las colisiones de una instalación corresponden a DEV-01.5.

Los contratos propios nuevos mantienen el nombre descriptivo y versionado
`<extension>.contract.simulated-source.v1`. Los schemas conservan sus roles y
versiones con el prefijo descriptivo de extensión. `Power-On` es sólo un nombre:
la plantilla continúa simulando/leyendo valores y no implementa encendido.
Command-only conserva `com.example.artest.contract.value-source.v1`, la operación
`com.example.artest.instrument.value-source.v1/read` y el driver compatible de
referencia. No se renombran contratos compartidos.

No se cambia Engine, ABI/API/IPC, formatos, publicación, recuperación, hashes de
integridad ni selección/preparación de entornos. Generate no requiere CLI.
Los nuevos inventarios del staging candidato se producen por el ensamblador
existente en un destino nuevo; no se reparan ni sustituyen kits/evidencias aceptados.

## Validación y evidencia

Archivos del ajuste: `source/ARTestDev/Authoring.h/.cpp`,
`source/ARTestDev/AuthoringWidget.h/.cpp`, las suites `AuthoringTests.cpp`,
`UiTests.cpp`, `PreparationTests.cpp` y `NativeTests.cpp` bajo
`source/ARTestDev/tests/`, esta guía, `docs/sdk/artestdev-dev01-2.md` y
`docs/planning/artestdev-roadmap.md`. No se modificaron recursos de plantilla,
servicios de preparación/Build, targets ni verificadores de integridad.

La evidencia local del candidato anterior se conserva en
`source/ARTestDev/build/readable-ids-evidence/`; su staging independiente es
`source/ARTestDev/build/readable-ids-staging/{Debug,Release}`. No hubo commit/push,
contacto con otras tareas, registro ni ejecución manual de Test plans/hardware.

Los comandos raíz exigidos por AGENTS pasaron en Debug y Release: 242 tests
aprobados y 31 deshabilitados por configuración, ABI, thin host, fronteras SDK,
distribución y consistencia XML/HTML. No se cambió ninguno de esos subsistemas.
La regresión completa inicial pasó 6/6 suites CTest por configuración: autoría
(42 casos), UI (4), generación nativa sin intérprete (5), preparación (16),
inspección/procesos y ensamblado de staging. La matriz nativa inicial pasó las
tres variantes por configuración, incluyendo SDK de sólo lectura, integridad,
diagnósticos y validación de descriptores con el CLI separado.

La revisión posterior añadió regresiones para nombres visibles que coinciden
con texto de la plantilla. Los nombres se insertan en el componente concreto,
después de sustituir identidades: no se vuelve a interpretar un nombre como
token de plantilla. Los logs `initial-*` y `before-python-name-fix-*` preservan
las ejecuciones anteriores; no sustituyen las comprobaciones del candidato final.
El primer build falló por acceso denegado de MSBuild FileTracker en el sandbox;
las compilaciones posteriores usaron la misma toolchain fuera de esa restricción.
Permanece la advertencia preexistente de traducciones de windeployqt; no hay
advertencias nuevas del compilador C++.

Edit se verifica mediante el probe de editor existente: argumentos, lanzamiento
asíncrono, fallos y conservación de archivos/IDs al abrir. No se realizó una
sesión manual de edición en Visual Studio/Python IDE ni se certifican sus plugins.
Build sí usa MSBuild real y preparación usa CPython externo 3.13.15 x64 con GIL.
Las pruebas de integridad/fallos usan fixtures; no acceden a hardware. La matriz
amplia previa y las comprobaciones finales focalizadas no reabren las aceptaciones
de rendimiento/retención de DEV-01.4-A.

### Resultado del candidato anterior (sustituido por la corrección siguiente)

Debug y Release compilan sin errores. Tras las dos correcciones de sustitución
de nombres, las comprobaciones focalizadas finales pasan en ambas configuraciones:

| Verificación | Debug | Release |
| --- | --- | --- |
| CTest Generate/Edit y generación nativa sin intérprete | 3/3 suites | 3/3 suites |
| Autoría (seis variantes, nombres, overrides, colisiones y guards) | 44 casos | 44 casos |
| UI / generación nativa sin intérprete | 4 / 5 casos | 4 / 5 casos |
| Preparación y reutilización de las tres variantes Python | 5 casos | 5 casos |
| Build y validación de las tres variantes C++ | 5 casos | 5 casos |

Todos sin fallos ni skips; los conteos QTest incluyen init/cleanup. Los tests
finales nativos comprueban nombres visibles, IDs, contratos requeridos y schemas,
validan descriptores contra el CLI/Engine separado y mantienen byte a byte
fuentes/configuración/Test plan, también con un ID histórico `local.*`.
Los Python comprueban IDs publicados, preparación/reutilización y conservación
de entradas. Los Test plans generados no se ejecutan.

`candidate-source-hashes.json`, `candidate-binary-hashes.json`, `candidate.patch`
y `evidence-hashes.json` identifican el candidato. `generated/` contiene copias
de fuentes, configuración, Test plans y metadatos/schemas finales;
`retained-fixtures.json` ubica los originales. `process-audits.json` conserva
conteos y hashes de las auditorías nativas. `README.md` distingue regresiones
amplias previas de las comprobaciones finales. No se alteraron inventarios ni
evidencias de etapas ACCEPTED.

Para reproducir, use el CMake local documentado y los prerrequisitos existentes,
sin descargas. Configure `ARTESTDEV_TEST_STAGING`, `ARTESTDEV_TEST_PYTHON`,
`ARTESTDEV_TEST_CLI` y `ARTESTDEV_TEST_MSBUILD` con los paths registrados en los
logs; ejecute CTest de autoría/UI/native-only por configuración,
`ARTestDevPreparationTests generatedIdentities` y
`ARTestDevNativeTests readableIdentities`. Los gates raíz usan exactamente los
dos comandos `scripts/build.ps1` de AGENTS, Debug/Release y x64.

### Corrección del blocker: overrides iguales a identidades de plantilla

El candidato anterior no cubría sustituciones encadenadas. En C++ driver+command,
`extensionId=bench-power`, `driverId=com.example.artest.command.read-value` y
`commandId=bench-power.power-on` convertían el driver en el comando durante Generate.
La evidencia anterior se conserva como historial; no demuestra esta corrección.

`Authoring.cpp` ahora sustituye literales completos en una sola pasada sobre el
texto original de cada fuente/header y de `MultipleInstruments.json`. Los valores
insertados no se vuelven a interpretar, tampoco cuando contienen otro ID como
prefijo. Se mantienen los diagnósticos de plantilla incompatible y los validadores
existentes: ningún ID válido queda prohibido por coincidir con una identidad de
plantilla. Nombres visibles se insertan después; command-only mantiene contrato y
operación compartidos. No hay migración ni recálculo al abrir o construir.

Las regresiones comparten `tests/IdentityOverrides.h`: reproducción exacta,
intercambio de componentes, IDs originales de contratos/schemas/extensión y
coincidencias parciales. Autoría comprueba seis juegos de overrides en las seis
variantes Python/C++, incluidos todos los instrumentos/comandos de los Test plans.
Build nativo compara IDs publicados, contratos y schema IDs exactos, además de
preservar las entradas byte a byte. El límite CTest de autoría pasa a 360 segundos
por las 36 generaciones adicionales. Los demás límites y mecanismos no cambian.

El staging nuevo es `source/ARTestDev/build/readable-ids-fix-staging/{Debug,Release}`;
logs, fuentes generadas, metadatos, auditorías y hashes del candidato corregido
se conservan en `source/ARTestDev/build/readable-ids-fix-evidence/`.
Los cambios de esta corrección se limitan a `Authoring.cpp`, pruebas de autoría/
Build, su matriz compartida, timeout de CTest y esta guía.

Resultados del candidato corregido (Debug y Release, sin fallos ni skips en QTest):

| Verificación | Debug | Release |
| --- | --- | --- |
| Autoría completa, incluidos 36 escenarios de overrides | 50 casos | 50 casos |
| Build nativo y metadatos: nueve proyectos, incluida reproducción exacta | 11 casos | 11 casos |
| Preparación/reutilización Python, tres variantes | 5 casos | 5 casos |
| UI / generación nativa sin intérprete | 4 / 5 casos | 4 / 5 casos |
| Gates raíz de AGENTS, repetidos para esta corrección | 242 aprobados / 31 deshabilitados | 242 aprobados / 31 deshabilitados |

Los conteos QTest incluyen init/cleanup. Autoría Debug tarda 297 segundos y
Release 76; los nueve Builds/validaciones nativos, 462 y 275 respectivamente.
`final-exits-*` y `root-exits.txt` registran salida cero. La reproducción conserva
exactamente los tres IDs indicados en fuentes/configuración/Test plans/metadatos.
`generated/<config>/cpp/repro` y sus logs/auditorías permiten revisarlo sin ejecutar
el Test plan. El manifiesto y los schemas se generan y validan por las rutas
aceptadas; no se cambian hashes de integridad ni contratos públicos.

## Prompt para el Architect

Revise sólo identificadores legibles en Generate, antes de DEV-01.5, en
`D:\GitHub\main\ARTestCLI`. Lea AGENTS, scope, roadmap, guías DEV-01.2/01.4 y esta
guía. Preserve las etapas ACCEPTED y evidencias históricas. Verifique nombres,
normalización común, IDs sugeridos/avanzados, seis variantes, colisiones locales,
coherencia de fuentes/configuración/schemas/Test plan, preservación al abrir/Edit/
Build y contrato/operación de referencia command-only. Contraste pruebas Debug/
Release, preparación Python, Build nativo y hashes en `readable-ids-fix-evidence`.
Revise especialmente overrides iguales a IDs originales de componentes,
contratos/schemas, sustitución en una pasada y `MultipleInstruments.json`;
confirme Build del caso reproducido sin modificar sus IDs solicitados.
Emita **ACCEPTED** o **REQUIRES FIXES** con hallazgos reproducibles y archivo/línea.
No implemente DEV-01.5, ejecute Test plans/hardware, haga commit/push ni contacte
otras tareas.
