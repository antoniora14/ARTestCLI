# ARTestDev DEV-01.1 — apertura e inspección

Estado: **DEV-01.1 ACCEPTED**, confirmado por el propietario. Este documento
describe esa entrega aceptada; el nuevo flujo está en [Generate/Edit DEV-01.2](artestdev-dev01-2.md).

Esta entrega incorpora sólo la aplicación base. Seleccione una carpeta de proyecto
existente y el directorio de un kit de desarrollo extraído. La ventana muestra
lenguaje, variante, extensión, SDK y herramienta local, además de correcciones
para archivos y prerrequisitos faltantes. Para Python, ejecuta exclusivamente
`python/tools/project.py check` con el intérprete configurado en el proyecto;
para C++ no inicia Python. La lectura del kit comprueba el inventario SHA-256
completo fuera del hilo de la interfaz. No se prepara, construye, registra ni
ejecuta ningún Test plan.

DEV-01.1 valida completamente sólo el manifiesto del kit histórico 0.4.0 que
declara CLI, Engine y Python runtime. Un directorio sin ese manifiesto o con
otro esquema se informa como formato no soportado y validación parcial; no se
trata como un kit histórico íntegro ni se presupone que el futuro SDK de autoría
incluya esos runtimes. Su nuevo manifiesto corresponde a DEV-01.7.

## Herramientas y combinación fijada

La combinación verificada es Qt **6.8.3** `msvc2022_64`, el toolset **MSVC v143
14.44.35207** de Visual Studio **2026 Insiders**, CMake **4.3.1-msvc1** y Windows
SDK **10.0.26100.0**. CMake identificó el compilador como **MSVC 19.44.35229**.
`CMakeLists.txt` exige Qt 6.8.3 exactamente y rechaza v145. La
[matriz de Qt 6.8](https://doc.qt.io/qt-6.8/supported-platforms.html) enumera
MSVC 2022 x64; el IDE Insiders puede usar el toolset v143 instalado en paralelo.
Se compiló y abrió la aplicación en Debug y Release con esta combinación. No se
afirma compatibilidad de los binarios Qt con v145.

El paquete público oficial `qtbase` 6.8.3 se extrajo sin cuenta Qt en
`source/ARTestDev/Qt/6.8.3/msvc2022_64`. Contiene Core, Widgets, Concurrent,
Test, herramientas y plugins necesarios para esta aplicación. Su SHA-1 coincide
con el publicado junto al archivo en el
[repositorio oficial](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683/qt.qt6.683.win64_msvc2022_64/).
Esta es una instalación portable de desarrollo dentro de `source/ARTestDev/Qt/`,
ignorada por Git; no es una instalación global ni una modificación del kit
aceptado. La aplicación y las
extensiones pueden emplear toolsets distintos porque se comunican por archivos
y procesos, sin compartir objetos C++.

## Compilar y abrir

Para trabajar en Visual Studio Insiders, abra
`source/ARTestDev/build/ARTestDev.slnx`. La solución generada
marca `ARTestDev` como proyecto de inicio. Seleccione `Debug | x64`, compile
con **Ctrl+Mayús+B** y arranque el depurador con **F5**. Los archivos editables
están en `source/ARTestDev`: `Main.cpp` contiene la ventana, `Inspection.cpp`
lee proyectos y kits, `ProcessAdapter.cpp` gestiona el proceso asíncrono y
`tests/Tests.cpp` contiene las pruebas. `CMakeLists.txt` define la solución;
los `.vcxproj` dentro de `build/` son generados y pueden regenerarse.

El resumen distingue configuración portable, kit, herramientas locales y
ARTestCLI como instalación separada. Para Python, `check` muestra pendiente,
correcto, fallido o prerrequisito faltante/incompatible a partir de su JSON;
una salida correcta no significa que se haya preparado el entorno. El botón
**Volver a comprobar** relee las selecciones después de una corrección manual.
No descarga ni instala componentes. La supervisión impide solapar otra
comprobación mientras la terminación anterior no esté confirmada.
La apertura C++ comprueba la ruta local de MSBuild y del SDK nativo, pero no
certifica todavía que el compilador, toolset y Windows SDK puedan construir el
proyecto. Tampoco valida una instalación ARTestCLI separada: no es requisito
para abrir y esa selección corresponde a fases posteriores.

La compilación de `ARTestDev` copia automáticamente las DLL y plugins de Qt
necesarios junto al ejecutable mediante `windeployqt`, para que F5 y la
ejecución directa funcionen sin ajustar `PATH`. `source/ARTestDev/build/`
contiene la solución, los proyectos de Visual Studio y los ejecutables;
`source/ARTestDev/Qt/` contiene la distribución portable de Qt. Ambas carpetas
son locales y están ignoradas por Git. Los intentos fallidos, temporales y logs
anteriores bajo `artifacts/dev01.1/` se eliminaron a petición del usuario.

Desde la raíz del repositorio:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$qt = (Resolve-Path source/ARTestDev/Qt/6.8.3/msvc2022_64).Path
& $cmake -S source/ARTestDev -B source/ARTestDev/build `
  -G 'Visual Studio 18 2026' -A x64 -T v143 `
  '-DCMAKE_GENERATOR_INSTANCE=D:/Program Files/Microsoft Visual Studio/18/Insiders' `
  "-DCMAKE_PREFIX_PATH=$qt"
& $cmake --build source/ARTestDev/build --config Debug
& $cmake --build source/ARTestDev/build --config Release
$env:PATH = "$qt\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$qt\plugins"
& source/ARTestDev/build/Debug/ARTestDev.exe
```

`ctest --test-dir source/ARTestDev/build -C Debug
--output-on-failure` y el equivalente `-C Release` ejecutan las pruebas. Los
ejecutables están en `source/ARTestDev/build/Debug/ARTestDev.exe` y
`source/ARTestDev/build/Release/ARTestDev.exe`. `ARTestDev.exe --smoke-ui`
muestra la ventana y sale tras 500 ms. En el entorno Codex, la detección inicial
del compilador y MSBuild requirieron ejecutarse fuera del sandbox por un error
de acceso del `FileTracker`.

## Archivos para un futuro commit

El código, `CMakeLists.txt`, `.gitignore`, las pruebas y esta guía son los
archivos de fuente que se revisan y versionan. `build/`, `Qt/` y `downloads/`
son locales e ignorados por `source/ARTestDev/.gitignore`; no deben subirse en
un commit. Tras clonar el repositorio, otro desarrollador debe proporcionar
Qt 6.8.3 `msvc2022_64` y ejecutar la configuración CMake indicada arriba.

## Dependencias de ejecución

Para esta depuración local, `windeployqt` copia las DLL y plugins detectados
para cada configuración. Una entrega futura debe revisar las dependencias
reales y sus notices. Se necesita el redistribuible Microsoft Visual C++
correspondiente; Debug requiere las bibliotecas de desarrollo de MSVC y no es
una configuración para redistribución. Incluya los textos de licencia de Qt
y los avisos de los componentes de terceros realmente enviados. Consulte la
[guía de despliegue Windows](https://doc.qt.io/qt-6.8/windows-deployment.html)
y las [licencias Qt](https://doc.qt.io/qt-6.8/licensing.html). DEV-01.1 no
modifica el ZIP aceptado ni produce un paquete distribuible.

## Validación realizada

La corrección DEV-01.1 compiló en Debug/Release y CTest informó 1/1 suites
aprobadas en cada configuración. Los casos enfocados cubren rutas normalizadas
y escapes, otro CWD, separación portable/local, estado JSON de Python,
resultados obsoletos, independencia C++ de Python, timeout, salida acotada y
terminación no confirmada provocada mediante una terminación inyectada que no
cierra el hijo. El smoke UI desde otro CWD terminó con salida 0 en ambas.
No se repitió la regresión completa del Engine ni se reconstruyó el SDK.
Antes de esta corrección, los builds Debug y Release de `scripts/build.ps1` pasaron con 242 pruebas
aprobadas y 31 deshabilitadas por configuración; la consistencia XML/HTML se
comprobó antes de borrar los resultados. El primer intento dentro del sandbox
falló por `MSB4018`/`FileTracker` y se repitió con éxito fuera de esa
restricción. El usuario pidió eliminar los logs y resultados generados de
la primera entrega de DEV-01.1, por lo que estos datos históricos se registran
como resumen. La nueva prueba de terminación no confirmada ejerce la rama real
del adaptador con inyección controlada; no afirma haber provocado un proceso
del sistema imposible de terminar.
