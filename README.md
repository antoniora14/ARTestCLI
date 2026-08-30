# ARTestCLI

ARTestCLI es el prototipo de motor de secuencias de prueba de ARTest. La Etapa A
establece una base segura y comprobable antes de extraer el motor reutilizable
por ARTestStudio y por futuros plugins.

## Estado de la Etapa A

- Visual Studio 18 Insiders, toolset `v145`, plataforma `x64`.
- C++20, `/W4`, conformidad estándar y archivos fuente UTF-8.
- Documento JSON canónico y versionado: `ARTest.Script`, versión `1`.
- Compilación/validación sin inicializar instrumentos.
- Inicialización y apagado explícitos de instrumentos sólo durante ejecución.
- Resultados tipados por paso y por corrida; una falla produce un código de
  salida distinto de cero.
- Google Test integrado en la solución.
- Reportes XML y HTML con validación de consistencia del veredicto.

## Compilar y probar

Desde la raíz del repositorio:

```powershell
.\scripts\build.ps1 -Configuration Debug -Platform x64
.\scripts\build.ps1 -Configuration Release -Platform x64
```

También puede ejecutarse `build.cmd`; la ventana permanece abierta para mostrar
el resultado. La ruta predeterminada de Visual Studio es:

```text
D:\Program Files\Microsoft Visual Studio\18\Insiders
```

La solución está en `source\ARTestCLI.sln`. El binario se genera en
`artifacts\bin\x64\<Configuration>\ARTestCLI.exe`.

## Uso

```powershell
$cli = '.\artifacts\bin\x64\Debug\ARTestCLI.exe'
& $cli compile '.\source\Scripts\TestScript.json'
& $cli run     '.\source\Scripts\TestScript.json'
& $cli debug   '.\source\Scripts\TestScript.json'
& $cli break   '.\source\Scripts\TestScript.json' 1 3
```

`compile` sólo analiza y valida el documento, los instrumentos, los comandos y
los parámetros. No abre recursos de hardware.

## Códigos de salida

| Código | Significado |
|---:|---|
| 0 | Operación completada correctamente |
| 2 | Argumentos inválidos |
| 3 | Script o configuración inválidos |
| 4 | Falló la inicialización de instrumentos |
| 5 | Falló la ejecución de la secuencia |
| 10 | Falla inesperada contenida en la frontera del proceso |

Consulta [TESTING.md](TESTING.md) para el procedimiento de regresión y
[docs/architecture/stage-a-safe-base.md](docs/architecture/stage-a-safe-base.md)
para las decisiones de esta etapa.
