# Etapa A — Base segura de ARTestCLI

## Objetivo

Estabilizar el prototipo actual antes de separar `ARTestEngine` como biblioteca.
Esta etapa evita que la refactorización futura dependa de comportamientos
implícitos o de una CLI que reporte éxito ante fallos reales.

## Límites adoptados

1. **Frontera de documento.** `ScriptDocumentLoader` es el único responsable de
   leer el archivo y aceptar `format = ARTest.Script`, `version = 1`.
2. **Construcción offline.** Cargar definiciones y compilar comandos no inicializa
   hardware. El comando `compile` es seguro para validación y CI.
3. **Ciclo de vida explícito.** `InstrumentFactory` crea, inicializa y apaga los
   instrumentos. Ante una inicialización parcial, limpia los recursos ya abiertos.
4. **Resultados, no éxito implícito.** Instrumentos, pasos y corridas devuelven
   resultados tipados con diagnóstico. El ejecutor se detiene ante la primera
   falla y la CLI propaga un código de salida distinto de cero.
5. **Validación atómica.** Instrumentos duplicados, pasos duplicados, referencias
   inexistentes o comandos desconocidos invalidan el documento completo. No se
   ejecuta una secuencia parcial.
6. **Frontera de excepción.** Las excepciones de configuración, inicialización y
   ejecución se convierten en diagnósticos o en un código de falla controlado.

## Formato canónico mínimo

```json
{
  "format": "ARTest.Script",
  "version": 1,
  "instruments": [],
  "commands": []
}
```

El cargador limita el archivo a 4 MiB, exige un objeto raíz y arreglos para
`instruments` y `commands`. Las versiones futuras deberán entrar mediante una
migración explícita, no mediante tolerancia silenciosa.

## Decisiones diferidas

Esta etapa no convierte aún el motor en DLL ni define el ABI de plugins. También
difiere cancelación, timeout, paralelismo, control de flujo completo, telemetría y
drivers reales. Esas decisiones se tomarán después de caracterizar ARTestCLI y
definir el contrato común con ARTestStudio.

## Criterios de cierre

- Debug y Release x64 compilan sin warnings de nivel 4.
- Las 15 pruebas Google Test pasan en ambas configuraciones.
- `compile` valida sin inicializar hardware.
- Un error de archivo, esquema, binding, inicialización o ejecución produce un
  código de salida no cero.
- XML y HTML reportan el mismo veredicto por caso y global.
