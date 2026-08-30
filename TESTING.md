# Pruebas automatizadas de ARTestCLI

## Estructura

El proyecto `tests\ARTestCLI.UnitTests.vcxproj` usa Google Test 1.18 y está
incluido en `source\ARTestCLI.sln`.

- `ScriptDocumentTests.cpp`: archivo ausente, JSON corrupto, formato y versión.
- `InstrumentFactoryTests.cpp`: definiciones, duplicados e inicialización
  separada de la carga.
- `CommandFactoryTests.cpp`: construcción válida y rechazo atómico de comandos.
- `ScriptExecutorTests.cpp`: compilación, detención ante falla y resultados.

La línea base de la Etapa A contiene 15 casos distribuidos en 4 suites.

## Ejecutar desde PowerShell

Desde la raíz del repositorio:

```powershell
.\scripts\build.ps1 -Configuration Debug -Platform x64
```

Antes de integrar cambios:

```powershell
.\scripts\build.ps1 -Configuration Release -Platform x64
```

El script devuelve un código distinto de cero si falla la compilación, alguna
prueba, la validación del generador o la consistencia del reporte.

Para compilar sin ejecutar las pruebas:

```powershell
.\scripts\build.ps1 -Configuration Debug -Platform x64 -SkipTests
```

## Ejecutar desde Visual Studio Test Explorer

1. Abrir `source\ARTestCLI.sln` con Visual Studio Insiders.
2. Seleccionar `x64` y `Debug` o `Release`.
3. Abrir **Test > Test Explorer**.
4. Compilar la solución con **Build > Build Solution**.
5. Confirmar que aparecen 15 pruebas en 4 suites.
6. Seleccionar **Run All Tests**.
7. Verificar que las 15 pruebas terminan con veredicto `Passed`.

## Reportes

Cada ejecución genera:

- `artifacts\test-results\<Platform>\<Configuration>\ARTestCLI.UnitTests.xml`
- `artifacts\test-results\<Platform>\<Configuration>\ARTestCLI.UnitTests.html`

El flujo primero prueba el propio generador con casos sintéticos `PASSED`,
`FAILED` y `SKIPPED`. Después compara los totales declarados por Google Test con
los veredictos de cada caso. Una contradicción detiene el build.

Los reportes son artefactos locales excluidos de Git. Se adjuntan como evidencia
de ejecución; no deben versionarse.

## Agregar una prueba

1. Elegir la suite de la responsabilidad modificada.
2. Nombrar el caso como comportamiento observable.
3. No depender de hardware real; usar dobles o configuraciones temporales.
4. Ejecutar Debug y Release.
5. Confirmar que el caso aparece en Test Explorer, XML y HTML.
