# ARTestDev DEV-01.2 — Generate y Edit

DEV-01.1, DEV-01.2 y DEV-01.2-A están ACCEPTED por confirmación del propietario.
Esta guía conserva su flujo y evidencia. La entrega posterior
[DEV-01.3](artestdev-dev01-3.md) añade preparación privada para revisión;
no autoriza DEV-01.4 ni etapas posteriores.

## Uso sin terminal

1. Abra ARTestDev. Welcome muestra **Welcome to ARTestDev**, **The Art of Testing**
   y **Create your own Driver, Command or both**. Pulse ese botón.
2. Indique nombre, Python o C++, Driver/Command/ambos y workspace. El valor inicial
   es `C:\Users\Public\ArtestDev`; puede cambiarlo y se recuerda por usuario.
   Cada proyecto ocupa una subcarpeta con su nombre. El SDK se localiza desde
   ARTestDev.exe automáticamente; pulse **Volver a comprobar** tras reparar recursos.
3. En Python, seleccione CPython **3.13 Windows x64 con GIL**, con los componentes
   venv y pip, instalado en el equipo fuera del SDK. Puede localizar directamente
   `python.exe`. Un intérprete ausente/incompatible bloquea Generate Python,
   pero no la apertura de proyectos ni Generate C++.
4. Pulse **Generate**. Los datos se conservan ante errores. Un destino existente,
   incluso vacío, se rechaza. El diagnóstico identifica permisos, recursos o rutas
   que corregir. Si falla después de reservar un temporal `.artest-new-*`, éste
   se conserva para inspección y no se reutiliza automáticamente.
5. Pulse **Edit**. Elija un candidato detectado o **Seleccionar IDE .exe…**.
   Python admite editores como VS Code/PyCharm; C++ abre Visual Studio mediante
   `devenv.exe`. La preferencia es local al usuario. La pantalla indica el archivo
   de comportamiento: `src/extension.py`, `SimulatedValueSource.h`,
   `ReadValueCommand.h`, o ambos headers según la variante. En proyectos Python
   existentes se resuelve el módulo desde su configuración portable.
   Cerrar ARTestDev no cierra el IDE.

Welcome muestra sólo el texto y el botón de creación, sin barra de menú.
En las pantallas de trabajo, **Project > New** abre un formulario nuevo conservando
preferencias y sin borrar proyectos; **Project > Load** permite seleccionar uno
existente para inspeccionarlo y editarlo. **Integrate > To ARTestCLI** permanece
deshabilitado hasta DEV-01.5; **To ARTestStudio** es una opción futura sin acción.
**Help > About ARTestDev** muestra información de la aplicación.
No se descargan herramientas, ejecutan instaladores ni modifica PATH. Después de
una instalación manual, pulse **Volver a comprobar**. Un IDE no sustituye al
intérprete ni al compilador.

C++ detecta separadamente MSBuild, compilador y bibliotecas x64 **v145/14.5x**,
el PlatformToolset v145 y cabeceras/bibliotecas Windows SDK 10.x. La aplicación
ARTestDev sigue usando Qt 6.8.3 y v143; ese toolset no prueba los prerrequisitos
de las extensiones. La falta de compilador o IDE no bloquea generar ni abrir.

## Límites y compatibilidad

- DEV-01.2-A usa el descriptor privado de staging; el lector del kit histórico
  **0.4.0** y sus pruebas permanecen intactos. No modifica ese kit ni define el
  manifiesto público de DEV-01.7. Generate/Edit no necesitan ARTestCLI.
- Los recursos se resuelven respecto al ejecutable real. Las fuentes se generan
  fuera del SDK. No se copian CLI, Engine o Python al proyecto. Las preferencias
  usan QSettings por usuario (`ARTest/ARTestDev`); el SDK nativo se referencia en
  `ARTestSDK.local.props`, excluido del control de versiones por la plantilla.
- Se conservan formatos e IDs de la creación guiada existente. Python delega
  `create` a `project.py`, sin preparar entornos. Las variantes simples portan
  la adaptación de `authoring.ps1`. C++ adapta la plantilla instalada en C++.
  Command-only conserva `com.example.artest.contract.value-source.v1` y la
  operación `com.example.artest.instrument.value-source.v1/read`; su Test plan
  referencia el driver compatible existente. Driver-only no añade comandos.
- **Generate/Edit no compilan ni integran ni ejecutan Test plans.** Los proyectos
  C++ conservan la plantilla; este staging no incluye el publicador histórico
  dependiente de Engine. El build completo corresponde a DEV-01.4 y no se presenta
  como implementado. Cada proyecto incluye
  `GENERATE-EDIT.md` para distinguir este flujo del README histórico.
- No se implementan menús operativos de Integrate, descubrimiento de Studio,
  preparación, registro, ejecución, instalación ni nuevos contratos.
- Se admiten rutas locales absolutas, espacios y otro CWD. Se rechazan reparse
  points y destinos anidados con el SDK; hay un límite conservador de 190 caracteres
  para la ruta del proyecto y de 80 para el nombre. En el SDK nativo se rechazan
  `$`, `%` y `;` por la evaluación de propiedades MSBuild.
- La publicación usa un movimiento en el mismo volumen sin reemplazo del destino.
  Se vuelve a comprobar el inventario al generar. Esto no constituye una barrera
  frente a un proceso malicioso del mismo usuario que cambie rutas concurrentemente.
  Un proceso Python cuya salida no se confirme sigue bloqueando nuevas operaciones.

## Validación de la entrega

Resultados del 2026-09-21:

| Verificación | Resultado |
| --- | --- |
| ARTestDev Debug/Release, C++20, Qt 6.8.3, v143 | Compilación correcta en ambas configuraciones |
| CTest base DEV-01.1 | Aprobado Debug/Release |
| Autoría: seis combinaciones y guardas | 14 casos aprobados por configuración, incluyendo setup/cleanup; ninguno omitido |
| Matriz C++ sin intérprete configurado | Tres variantes aprobadas Debug/Release |
| UI Generate/Edit y preferencias | Aprobada Debug/Release tras corregir la espera del callback del lanzador en la prueba |
| Smoke `--smoke-ui`, CWD `C:/Windows/Temp` | Salida 0 en Debug y Release |
| Regresión `scripts/build.ps1`, Debug/Release x64 | 242 pruebas aprobadas y 31 deshabilitadas por configuración; ABI, thin-host y consistencia XML/HTML correctos |
| Whitespace de cambios tracked y nuevos de texto | Sin hallazgos |

Se probaron destino ocupado (incluido conflicto al publicar), ACL sin escritura,
junction en un ancestro del workspace, escapes, nombres reservados, colisión
insensible a mayúsculas con el starter, nombres con el mismo stem e IDs distintos,
espacios, CWD diferente, SDK en D: y proyectos temporales en C:, workspace
personalizado, contrato command-only, fallos de IDE, segunda apertura con un
editor activo, preferencias aisladas y comprobación renovada de componentes.
La auditoría del flujo sólo encuentra el probe Python, `project.py create` y
lanzamiento desacoplado del editor; no hay invocaciones a preparación, build,
registro, Test plan, shells o instaladores. Las pruebas de generación comprueban
la ausencia de `.venv`, outputs y copias de CLI/Engine/Python en el proyecto.

Incidencias de validación: FileTracker produjo `E_ACCESSDENIED` dentro del sandbox;
los builds se ejecutaron fuera de esa restricción. Una primera ejecución de la
suite de autoría sin log por caso dejó de progresar y se detuvo; se añadieron
logs y límite CTest de 180 segundos. La ejecución aislada permitió detectar que
Windows no permitía crear symlinks; la prueba usa ahora un junction propio y pasa.
También se corrigió el aislamiento INI de la prueba UI y se retiraron únicamente
sus preferencias temporales del registro. `windeployqt` avisa de la ausencia del
catálogo de traducciones Qt; no afecta al build ni al smoke de esta aplicación.

Las pruebas de autoría usan `ARTESTDEV_TEST_KIT` (kit histórico íntegro) y
`ARTESTDEV_TEST_PYTHON` (CPython externo); no alteran sus contenidos. CTest conserva
logs en `source/ARTestDev/build/authoring-<config>.txt` y `ui-<config>.txt`.
Sin un kit configurado, estas pruebas se omiten explícitamente y esa ejecución
no cuenta como aceptación de Generate/Edit. La matriz nativa vacía su selección
de intérprete y no invoca Python.
Las pruebas de IDE emplean un ejecutable de prueba independiente: verifican
argumentos, fallos de lanzamiento y supervivencia tras liberar el lanzador,
incluida una segunda apertura con un proceso ya activo. No certifican plugins,
indexación ni builds de un IDE real. El smoke visual usa la ventana Qt real.

La combinación de herramientas y el procedimiento de mantenimiento CMake siguen
en [DEV-01.1](artestdev-dev01-1.md). El usuario de Generate/Edit no necesita esos
comandos. Los binarios, logs y capturas bajo `build/` son resultados locales
ignorados por Git, no fuentes para commit.

## Corrección de los dos blockers de revisión

La selección explícita de Python se conserva al volver a comprobar y se guarda
en las preferencias locales. Generate permanece deshabilitado hasta terminar un
probe compatible; los resultados de una selección anterior se descartan mediante
una revisión de selección, incluso al cambiar de A a B y volver a A.

La prueba UI espera estados observables con timeout y compara rutas normalizadas.
Incluye cambio desde un probe incompatible CPython 3.12 simulado a CPython 3.13
real, nueva comprobación, descarte de resultados obsoletos y reapertura con la
selección persistida. El ejecutable incompatible es un doble de prueba; no prueba
una instalación real de Python 3.12.

Validación de esta corrección (2026-09-21): builds Debug/Release correctos; CTest
4/4 en ambas configuraciones con kit histórico e intérprete configurados, sin
skips de aceptación; smoke UI Debug/Release desde otro CWD con salida 0; whitespace
tracked y nuevo sin hallazgos. No se reconstruyeron SDK ni Engine. Se conservaron
los fallos previos de Generate y de separadores de rutas, junto con los resultados
nuevos, en `source/ARTestDev/build/dev01-2-blockers-20260921-171925/`.

## Prompt para revisión del Architect

El prompt siguiente es histórico de DEV-01.2; para la enmienda use el del final.

Revise exclusivamente DEV-01.2 en `D:\GitHub\main\ARTestCLI`, preservando el
working tree previo. DEV-01.1 está ACCEPTED. Lea AGENTS.md, el scope, el roadmap
y esta guía; inspeccione `source/ARTestDev/` y el fake de editor en
`tests/TestSupport/Fakes/ARTestDevEditorProbe.cpp`. Evalúe Welcome/Form/Generate/Edit,
seis variantes, IDs/contrato command-only, rutas/publicación sin sobrescritura,
prerrequisitos separados, preferencias locales, asincronía y vida del IDE.
Contraste los logs Debug/Release con los límites documentados: kit histórico
intacto, sin nuevo manifiesto, builds/integración/preparación/Studio ni cambios
Engine/API/ABI/IPC. Devuelva **DEV-01.2 ACCEPTED** o **DEV-01.2 REQUIRES FIXES**,
con hallazgos verificables y archivo/línea. No implemente DEV-01.3 ni haga commit/push.

## DEV-01.2-A — Localización automática y staging privado

DEV-01.2 y DEV-01.2-A están ACCEPTED por confirmación del propietario. La entrega
posterior [DEV-01.3](artestdev-dev01-3.md) añade el servicio privado de preparación
y recursos offline en un staging nuevo; conserva el flujo Generate/Edit descrito
aquí. Las cifras y rutas siguientes son evidencia histórica de DEV-01.2-A.

El Architect resolvió el bloqueo del formato histórico autorizando una carpeta
privada de desarrollo. La parada anterior fue correcta bajo la instrucción de
no inventar formatos; esta decisión permite `artestdev-staging.json` provisional,
sin convertirlo en el ZIP 0.5.0 ni en formato público/instalador de DEV-01.7.

`SdkLocation.cpp` valida desde la ubicación real de ARTestDev.exe. Inspección y
Generate comparten ese servicio. No hay selector, preferencia SDK, variable de
entorno, PATH, CWD, ruta personal ni búsqueda de checkout como fallback de recursos.
`beginCreation` revalida el origen: staging privado o kit histórico, sin cambiar
`inspectKit`. El constructor con ruta inyectada sólo existe al compilar la prueba
UI con `ARTESTDEV_TESTING`; no está en el ejecutable de usuario.

Los errores detallan causa y reparación/reinstalación. Generate queda bloqueado;
abrir/editar proyectos existentes y seleccionar herramientas externas sigue
siendo posible. Workspace conserva `C:\Users\Public\ArtestDev` como sugerencia
editable y persistida por usuario. La antigua preferencia `sdk` se ignora y se
conserva sin leer/escribir ni borrar otras preferencias.

### Layout y procedencia

Cada configuración tiene su árbol independiente:

```text
source/ARTestDev/build/development staging/<Debug|Release>/
  ARTestDev.exe
  Qt6{Core,Gui,Widgets,Concurrent}[d].dll
  platforms/qwindows[d].dll
  notices/                         # aviso local y SPDX del Qt configurado
  native-sdk/
    sdk-version.json
    include/                       # headers públicos y nlohmann/json
    build/native/ARTestSDK.props    # includes y propiedades para edición
    templates/ARTestExtension/      # ocho archivos explícitos del starter
    THIRD_PARTY_NOTICES.md
  python/
    tools/{project,package}.py
    templates/minimal/             # seis archivos explícitos, incluido .gitignore
  artestdev-staging.json
```

`StageDevelopment.ps1` enumera los 43 archivos: ejecutable recién compilado,
DLL/plugin/SPDX del Qt 6.8.3 configurado, headers/props/starter/avisos de
`source/ARTest.SDK`, `source/ThirdParty/json.hpp`, y herramientas/plantillas de
`source/ARTest.Python`. Las variantes Python simples siguen embebidas como recursos
Qt del ejecutable, también inventariado. Generate sólo necesita la biblioteca
estándar de CPython externo; no requiere wheel ni preparación en esta etapa.
No se copia ningún paquete histórico completo ni se recorta después. No incluye
CLI, Engine, ARTestSdkValidate, intérprete, entorno preparado, outputs de extensiones
ni herramientas de publicación. Los avisos/SPDX se conservan; no se afirma que esta
carpeta privada haya pasado los requisitos de distribución pública de Qt/ARTest.

El descriptor contiene versión interna 1, windows-x64, configuración, versiones
nativeSdk 0.4.0/pythonSdk 0.2.0/Qt 6.8.3, rutas relativas y SHA-256 de todos los
archivos salvo él mismo. El inspector comprueba también la lista obligatoria;
rechaza faltantes, adicionales, corrupción, duplicados insensibles a mayúsculas,
rutas absolutas/escapes y reparse points en archivos/directorios/ancestros. No
modifica archivos ni hashes. El descriptor no es una firma ni una defensa contra
un atacante que controle simultáneamente ejecutable, descriptor y recursos.

El ensamblador verifica los outputs previos antes de escribir. Si encuentra
corrupción, archivos extraños, inventario incompatible o incompleto, conserva el
árbol y falla; use un destino nuevo desde fuentes verificadas para reparación de
desarrollo. Un build posterior válido puede actualizar sus inputs verificados;
no existe reparación automática al arrancar ARTestDev. No se promete publicación
atómica ni tolerancia a procesos maliciosos concurrentes.

### Visual Studio y comandos de mantenimiento

Use la combinación Qt/v143/CMake existente de DEV-01.1. Desde la raíz:

```powershell
$cmake = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
& $cmake -S source/ARTestDev -B source/ARTestDev/build
& $cmake --build source/ARTestDev/build --config Debug
& $cmake --build source/ARTestDev/build --config Release
# Seleccione su CPython 3.13 x64 con GIL ya instalado, fuera del staging:
$env:ARTESTDEV_TEST_PYTHON = '<ruta absoluta>/python.exe'
& $ctest --test-dir source/ARTestDev/build -C Debug --output-on-failure
& $ctest --test-dir source/ARTestDev/build -C Release --output-on-failure
```

Abra `source/ARTestDev/build/ARTestDev.slnx`, seleccione Debug o Release/x64 y
ARTestDev como proyecto inicial. F5 usa `VS_DEBUGGER_COMMAND` hacia el ejecutable
staged y working directory `build/`, distinto del SDK. Build Solution ejecuta
`ARTestDevStaging` incluso cuando sólo cambian recursos; compilar/relinkar ARTestDev
también lo ensambla. Tras editar sólo recursos, use Build Solution o compile
explícitamente el target `ARTestDevStaging` antes de F5.

Se puede elegir otra carpeta de mantenimiento mediante
`-DARTESTDEV_STAGING_ROOT=<ruta absoluta con espacios>` al configurar CMake.
Eso cambia el destino de ensamblado/depuración; no es un selector runtime del SDK.
Los PDB, logs y resultados CTest quedan en `build/`, fuera del staging. QSettings
permanece por usuario, los proyectos/outputs en el workspace. No abra el staging
como solución ni guarde allí proyectos. El runtime de MSVC/CPython/IDE deben estar
previamente instalados; no se descargan ni instalan dependencias. Los comandos
PowerShell aquí son de mantenimiento y no forman parte del flujo GUI.

### Evidencia de DEV-01.2-A

Resultados de la matriz Debug/Release anterior a la corrección del `.gitignore`
Python (42 archivos); se conservan como evidencia histórica:

| Verificación | Resultado |
| --- | --- |
| Build ARTestDev C++20/Qt 6.8.3/v143 | Correcto en ambas configuraciones; 42 archivos staged por configuración |
| CTest completo | 5/5 suites aprobadas por configuración; Debug 130.51 s, Release 46.41 s |
| Autoría, inventario privado, ACL y traslado | 28 casos aprobados por configuración, 0 fallos, 0 skips (incluye setup/cleanup) |
| Nativo sin intérprete configurado | 5 casos aprobados por configuración, 0 skips; tres variantes C++ |
| UI y preferencias | 4 casos aprobados por configuración, 0 skips; Generate C++/Python, antiguo SDK ignorado y Edit habilitado sin recursos |
| Inventario histórico | Suite DEV-01.1 aprobada; `inspectKit` y sus pruebas conservados |
| Ensamblador | Cuatro rechazos esperados por configuración: corrupción, faltante, adicional y duplicado; descriptor sin cambios |
| Smoke real del ejecutable | Debug/Release en D: y copias en C: con espacios, CWD C:/Windows/Temp, PATH sólo Windows: salida 0; recurso faltante: rechazo esperado con salida 2 |
| Runtimes prohibidos | Ninguno en los dos inventarios de 42 archivos |
| Whitespace | Sin hallazgos al reconocer CRLF como fin de línea |

La prueba con ACL demuestra que no se puede crear un archivo en el SDK ni abrir
`project.py` para escritura, mientras Generate/Edit Python y C++ funcionan desde
esa copia en C:. Se comparan inventarios antes/después. La generación cubre las
seis combinaciones de lenguaje/variante. Edit se verifica con un ejecutable de
prueba desacoplado, no certifica builds, plugins ni indexación de un IDE real.
Una última corrección exclusivamente textual reemplazó «seleccione un kit» por
reparar/reinstalar; se reconstruyeron ambas configuraciones y se repitieron UI/smoke.

Evidencia local en `source/ARTestDev/build/` (ignorada por Git):

- `dev012a-debug-ctest-final.txt`, `dev012a-release-ctest-final.txt` y
  `dev012a-{authoring,native-only,ui}-<config>.txt`.
- `dev012a-*-build-final.txt`, `dev012a-<config>-build-wording.txt`,
  `dev012a-<config>-ui-wording.txt` y `dev012a-smoke-wording.txt`.
- `dev012a-smoke.txt`, `dev012a-runtime-audit.txt`, `dev012a-whitespace-final.txt`
  y capturas `dev012a-ui-{Debug,Release}.png`.
- `dev012a-evidence/assembly-<config>-<id>/`: fixtures dañados y diagnósticos,
  conservados sin reparación. Las copias smoke incompletas permanecen en la ruta
  temporal de C: registrada en `dev012a-smoke.txt`.

Incidencias conservadas: el primer build restringido falló por
`FileTracker/E_ACCESSDENIED` (`dev012a-debug-build.txt`); la ejecución local fuera
del sandbox compiló correctamente. CMake avisa de VulkanHeaders ausentes y
windeployqt del catálogo de traducciones ausente; no impiden esta UI Widgets.
Un primer check de whitespace con autocrlf desactivado interpretó CR como espacio
sobrante; su log se conserva y la comprobación final usa `cr-at-eol`. No se
normalizaron masivamente fuentes para ocultar esos avisos.

Las pruebas de autoría/UI reciben el staging desde CTest. CPython externo se
declara mediante `ARTESTDEV_TEST_PYTHON`, sólo en pruebas; la matriz nativa lo vacía.
La ausencia de inputs produce un skip explícito en autoría o un fallo de setup
en UI y no cuenta como aceptación; esta ejecución no tuvo skips. No se compilaron
extensiones ni se ejecutaron gates de Engine/integración ajenos a DEV-01.2-A.

### Corrección del blocker: `.gitignore` Python

El staging incluye ahora el `.gitignore` original de
`source/ARTest.Python/templates/minimal/`, y `SdkLocation.cpp` lo exige en el
inventario obligatorio. El total actual es **43 archivos por configuración**.
Las tres variantes Python comparan el archivo generado con el recurso original
staged y comprueban las exclusiones `artest-project.local.json`, `.artest/`,
`__pycache__/` y `*.pyc`. El inspector se prueba tanto con ese archivo ausente
como con el archivo y su entrada eliminados del descriptor.

Los stagings previos bajo `build/development staging/{Debug,Release}` se conservan
sin modificar. La corrección usa un destino nuevo configurado con:

```powershell
& $cmake -S source/ARTestDev -B source/ARTestDev/build `
  "-DARTESTDEV_STAGING_ROOT=$((Get-Location).Path)/source/ARTestDev/build/development staging gitignore fix"
```

Visual Studio y CTest toman ese destino de la configuración CMake. No se
regeneran hashes de los árboles anteriores. La evidencia de la corrección queda
en `source/ARTestDev/build/dev012a-gitignore-fix/`, incluidos hashes previos y
copias de logs anteriores, builds/CTest por configuración y smoke.

Validación de la corrección: builds Debug/Release correctos; CTest **5/5** en
ambas configuraciones (133.81 s / 44.24 s), autoría **29 casos aprobados por
configuración, 0 fallos y 0 skips**, incluidas las tres variantes Python y el
rechazo de `.gitignore` no inventariado. Smoke desde otro CWD: **exit 0** en
ambas. Whitespace sin hallazgos. `inventory-final.txt` verifica los **43 hashes**
de cada staging nuevo, igualdad SHA-256 del `.gitignore` con el original y los
**86 archivos anteriores sin cambios**, incluidos sus dos descriptores.
Se mantienen únicamente los avisos ya documentados de Vulkan/traducciones Qt;
no se introdujeron fallos de build o pruebas en esta corrección.

Revisión del blocker: examine exclusivamente la inclusión/exigencia del
`.gitignore` en `StageDevelopment.ps1` y `SdkLocation.cpp`, las pruebas de
`tests/AuthoringTests.cpp` y esta guía. Contraste `dev012a-gitignore-fix/` y
confirme que los stagings anteriores no se alteraron. Devuelva
**DEV-01.2-A ACCEPTED** o **DEV-01.2-A REQUIRES FIXES**. No avance etapas ni
haga commit/push.

### Prompt para el Architect

Revise exclusivamente DEV-01.2-A, preservando DEV-01.1/01.2 aceptados y el menú.
Lea AGENTS, scope, roadmap y esta guía; revise SdkLocation.cpp/.h, Inspection.h y
la adaptación de lectura de props en Inspection.cpp, Authoring.cpp,
AuthoringWidget.cpp/.h, Main.cpp, CMakeLists.txt, StageDevelopment.ps1,
resources/STAGING-NOTICES.md, tests/AuthoringTests.cpp, tests/UiTests.cpp,
tests/StageDevelopmentTests.ps1 y el fake ARTestDevEditorProbe.cpp. Contraste
la evidencia Debug/Release: resolución real del ejecutable, integridad completa,
revalidación, ausencia de selectores/runtimes, preferencias ignoradas, ACL,
traslado/CWD y Generate/Edit. Mantenga inspectKit histórico y contratos intactos.
El descriptor es privado/provisional, no DEV-01.7. Devuelva **DEV-01.2-A ACCEPTED**
o **DEV-01.2-A REQUIRES FIXES**, con archivo/línea y evidencia. No implemente
DEV-01.3/01.4 ni integración/instalador; no haga commit/push.
