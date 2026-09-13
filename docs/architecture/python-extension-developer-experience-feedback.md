# Experiencia de desarrollo de extensiones Python: ejercicio PicoScope

> Disposición registrada el 2026-09-13: este archivo se conserva en ARTestCLI como
> feedback histórico del ejercicio de soporte. El alcance inicial aprobado es
> **PY-DX-01 — Python Developer Experience: create, prepare and run**.
> Consulta el [roadmap vigente](roadmap-pre-dotnet.md) y el
> [execution plan](../planning/py-dx-01-execution-plan.md).
> Las observaciones, propuestas y referencias al estado de revisión que siguen
> corresponden a la fecha original; no amplían el alcance aprobado.

Fecha: 2026-09-11.
Estado: observaciones y propuestas para discutir; no constituyen una decisión de arquitectura.

## Contexto y evidencia

Durante un ejercicio guiado de soporte se integró un PicoScope 2204A mediante
Python 3.13 x64, ctypes y la API ps2000 del PicoSDK instalado en Windows.
El usuario confirmó con la salida de ejecución que ARTest inicializó el driver,
ejecutó el comando de identificación, verificó el modelo y cerró la conexión.
La sesión terminó con un paso aprobado y código de salida 0.
Esta evidencia cubre identificación y ciclo de vida, no adquisición de señales.

El paquete contiene un driver y un comando. Ambos utilizan un único entorno
preparado para el paquete. Lib/site-packages contiene el SDK y dependencias;
Scripts contiene herramientas del entorno virtual. El recibo
artest-environment.json registra la preparación y su integridad, mientras que
python-environments.json asocia extensiones con recibos locales.

## Observación del usuario

Los pasos manuales para generar el paquete, preparar el entorno y escribir su
asociación resultan demasiado complejos para un desarrollador junior. El usuario
cuestionó la arquitectura al encontrar Lib y Scripts por entorno y pidió
conservar estas preguntas para una futura conversación con el arquitecto.
Indicó expresamente que el ejercicio del instrumento debe continuar.

## Fricciones observadas

- El desarrollador debe conocer package.py, el wheel del SDK, el archivo de
  dependencias, el punto de entrada, las rutas de salida y el archivo de asociación.
- Las rutas absolutas y las variables de PowerShell introducen carga de configuración.
- Una ruta relativa de salida terminó en una ubicación distinta de la esperada
  durante el tutorial; la causa no fue investigada.
- Cada paquete independiente requiere actualmente su propio entorno preparado,
  con posible duplicación de dependencias y consumo de disco.
- Los cambios de código requieren generar un nuevo destino y preparar un entorno
  correspondiente; el usuario termina administrando versiones de carpetas.
- La dependencia externa PicoSDK debe estar instalada y su DLL de arquitectura
  correcta debe localizarse explícitamente.

## Propuestas para evaluación

Separar la experiencia de desarrollo de las decisiones de aislamiento y
supervisión en ejecución. Mejorar la primera no exige por sí mismo sustituir IPC
ni incrustar Python dentro del Engine.

Ofrecer una configuración de proyecto y herramientas que automaticen:

1. Creación de un proyecto de extensión con ejemplos mínimos.
2. Generación y validación de metadatos.
3. Preparación del entorno y registro de su ubicación.
4. Ejecución de un plan de prueba.
5. Actualización de paquetes y gestión conservadora de versiones anteriores.

Ejemplo de interfaz deseada, no implementada ni comprometida:

```text
artest extension new Pico2204A --language python
artest extension build
artest extension run PicoIdentity.json
```

La simplificación debe conservar la validación de integridad y los errores de
preparación; no debe depender de editar recibos o instalar dependencias durante
una ejecución de pruebas.

## Preguntas para arquitectura

- ¿Cómo diferenciar el ciclo rápido de desarrollo de una distribución verificada?
- ¿Dónde administra ARTest entornos, asociaciones y versiones, y cómo los limpia
  sin eliminar datos ajenos o entornos en uso?
- ¿Conviene mantener entornos por paquete o reutilizar entornos compatibles?
  Evaluar conflictos de dependencias, actualización, aislamiento y ahorro de disco.
- ¿Cómo declarar, localizar y verificar dependencias externas como PicoSDK?
- ¿Cómo presentar errores por intérprete ausente, arquitectura incorrecta,
  DLL faltante, dispositivo ocupado o fallo de preparación?
- ¿Cómo garantizar que todas las rutas relativas se resuelvan de forma predecible?
- ¿Qué responsabilidades debe asumir la CLI y cuáles una futura interfaz Studio?

## Criterios sugeridos de mejora

- Un usuario nuevo puede crear y ejecutar una extensión siguiendo un flujo corto,
  sin escribir manualmente asociaciones ni localizar el wheel del SDK.
- Los cambios habituales de código no requieren elegir sufijos de carpetas.
- El desarrollador puede inspeccionar las ubicaciones y dependencias administradas.
- Los fallos muestran una causa concreta y una acción de recuperación.
- El funcionamiento nativo sigue sin requerir Python.

## Seguimiento

Continuar el driver y los comandos PicoScope. Próxima etapa propuesta:
captura limitada del canal A y cálculo de mínimo, máximo y voltaje pico a pico.
El usuario eligió usar el generador integrado del PicoScope. Se propone una
señal senoidal de 1 kHz, 1 V pico a pico y offset de 0 V, conectada externamente
al canal A. La conexión y la captura todavía no han sido verificadas.

Documento conservado localmente para una futura revisión. No se ha enviado al
arquitecto ni se ha solicitado una modificación de arquitectura.
