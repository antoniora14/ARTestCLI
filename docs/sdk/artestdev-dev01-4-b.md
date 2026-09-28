# DEV-01.4-B — Autoría simplificada e identidades administradas

Estado: DEV-01.4-B ACCEPTED por el Architect el 2026-09-27, incluidos los dos
blockers corregidos. Cierre documental el 2026-09-28; DEV-01.5 queda autorizado
como siguiente implementación independiente, sin ejecución de Test plans.
Posición: después del ajuste ACCEPTED de identificadores legibles en Generate y
antes de DEV-01.5. Las etapas DEV-01.1/01.2/01.2-A/01.3/01.4/01.4-A y PY-DX-01
permanecen ACCEPTED. No se inicia Integrate ni se reabre su trabajo previo.

Autoridad: [AGENTS](../../AGENTS.md), [scope](../architecture/artestdev-initial-scope.md),
[roadmap](../planning/artestdev-roadmap.md) y esta guía.
La [corrección de IDs](artestdev-readable-ids.md) es la base de compatibilidad.

## 1. Resultado para el developer

En los archivos de código existentes, el autor debe poder declarar una sola vez:

| Campo | Alcance | Ejemplo |
| --- | --- | --- |
| Nombre | Cada driver | `Picoscope2204A` |
| Nombre | Cada comando | `Read_Wave_Form`, `Set_Trigger` |
| Author | Extensión; común a los componentes | `Pico Technologies` |
| Version | Extensión; heredada por componentes por defecto | `0.0.1` |
| Description | Opcional e independiente por driver/comando | `Captura muestras del canal seleccionado.` |

Los nombres del cuadro ilustran la presentación; no implementan PicoScope.
El developer sigue escribiendo clases, parámetros, operaciones y lógica.
No debe editar ni sincronizar IDs largos, contratos repetidos, manifests o recibos.
No se añaden archivos de configuración que deba mantener ni pasos de consola.
La UI de Generate aporta valores iniciales; las declaraciones en código pasan a
ser autoridad sobre los nombres, autor, versión y descripción posteriores.
Abrir/Build no restaura los nombres originales del formulario.

Las nuevas ayudas deben ser pequeñas y compatibles, con funciones/tipos/decoradores
adecuados a cada lenguaje. No resolverlo sólo cambiando strings por macros que
obliguen a sincronizar las mismas referencias. No diseñar un DSL, parser general
de C++, reflexión dependiente del compilador ni una plataforma multilenguaje.
La sintaxis exacta se concreta dentro de esta unidad y se documenta con un ejemplo
compilable C++20 y otro Python antes de extender las seis variantes.

## 2. Identidad estable y única autoridad

- Separar presentación editable de identidad. Cambiar Nombre, Author, Version o
  Description no modifica extensionId, component IDs, contratos ni schema IDs.
  Esos cambios sí actualizan metadatos/salidas e integridad cuando corresponda.
- Conservar el namespace de extensión fijado al crear el proyecto. Cada componente
  nuevo necesita un ancla simbólica estable, asociada explícitamente a su declaración
  de código; la herramienta administra su identidad, no el nombre visible.
- Elegir y documentar una única autoridad portable para la asociación: declaraciones
  internas administradas en fuentes existentes o configuración privada portable ya
  existente. No duplicar autoridades ni añadir un fichero de edición obligatoria.
  Si hace falta estado generado auxiliar, debe ser regenerable a partir de esa
  autoridad y no quedar perdido por Clean, borrar outputs o copiar/clonar el proyecto.
- No derivar identidad del nombre mutable, autor, versión, descripción, ruta absoluta,
  posición de registro, contador de compilación, RTTI/mangling o hash del binario.
  No crear nuevas identidades en cada Build ni durante carga/ejecución.
- Añadir otro comando debe asignarle una identidad propia sin reescribir las de los
  existentes. Reordenar o retirar un componente conserva las identidades restantes.
  Duplicar una declaración con la misma identidad es error, no un renombre silencioso.
- Renombrar una clase/ancla interna no es cambiar su nombre visible. Esta primera
  unidad no ofrece refactor automático de identidades ni reconciliación por similitud.
  Documentar esa diferencia y diagnosticar asociaciones ambiguas; no adivinar una
  migración ni reutilizar una identidad para un componente distinto.
- Copiar un proyecto conserva sus identidades: se considera la misma extensión.
  Detectar su coexistencia como posible duplicado; crear un producto/fork independiente
  con identidades nuevas no forma parte de esta unidad.
- Los Test plans conservan referencias por ID. No sobrescribir Test plans editados
  para seguir nombres visibles ni sincronizar por sustitución global de strings.
  Preservar `MultipleInstruments.json` y el caso de overrides de la corrección aceptada.
- Si se necesita actualizar estado administrado portable, hacerlo fuera del runtime,
  con límites, contención, exclusión entre escritores y publicación recuperable.
  Un error no debe borrar asociaciones previas, fuentes ni outputs aceptados.
  Build ordinario sin cambios no debe reescribir entradas ni perder incrementalidad.

## 3. Contratos y registro

Reutilizar la definición nativa compartida entre DLL/generador y las declaraciones
Python existentes como base. El registro y la invocación deben consumir la misma
definición del contrato/operación mediante referencias simbólicas, sin copiar
strings en varios archivos. Los schemas siguen describiendo parámetros/configuración
reales; aprovechar sus IDs por defecto cuando sea compatible con nuevas plantillas.

No inferir contratos por nombres o similitud, ni seleccionar silenciosamente un
driver concreto. Command-only conserva el contrato compartido
`com.example.artest.contract.value-source.v1`, la operación
`com.example.artest.instrument.value-source.v1/read` y el acceso por broker.
No enlazar clases de un driver externo con el comando. Un contrato compartido no
se renombra al cambiar el nombre comercial del driver.
No convertir el ejemplo simulado en hardware por llamarlo PicoScope o Set_Trigger.

## 4. Description sin modificar Engine

Campo opcional de presentación por componente, editable una sola vez en código.
Admitir ausencia/cadena vacía (omitir el campo al serializar), Unicode y caracteres
que requieran escape JSON. Límite de esta ayuda: 512 puntos de código Unicode;
rechazar NUL, texto inválido o exceso con diagnóstico, sin truncar silenciosamente.
No afecta identidad, contrato, operación, validación de parámetros ni selección.

El catálogo actual ya admite `components[].description`
(`source/ARTestEngine/Extensions/ComponentSchema.cpp`). Usar ese campo opcional;
no crear una propiedad de manifest, versión o campo ABI nuevos ni confundirlo
con la descripción de un parámetro/schema o con el mensaje del resultado.

C++: extender la proyección de metadata y las ayudas de autoría de modo aditivo,
manteniendo las firmas/uso existentes. Python: keyword opcional compatible en
declaraciones y la misma proyección en packaging/describe. Probar la igualdad
del descriptor esperado y observado con el Engine actual. No cambiar protobuf,
versiones de wire ni la envoltura DESCRIBE para transportar la descripción.
Con Description ausente, conservar la salida del camino anterior.

La descripción debe conservarse en el paquete y mostrarse en el resumen/inspección
de ARTestDev junto al componente usando metadata ya disponible. No importar
Python ni cargar DLLs sólo para mostrarla. Si aún no hay metadata vigente, indicar
ese estado; no presentar la descripción de un build antiguo como actual.
Esta unidad no promete exponerla por un nuevo campo del Engine API/CLI report.

## 5. Duplicados y similitud: error frente a aviso

| Situación | Disposición |
| --- | --- |
| Dos declaraciones con un mismo ID en el paquete | ERROR antes de publicar; indicar ambos componentes |
| Mismo ID en otro proyecto conocido del workspace | Conservar rechazo/diagnóstico de colisión existente; no renombrar automáticamente |
| Rebuild/preparación del mismo proyecto/componente | No tratarlo como una colisión consigo mismo |
| Nombres equivalentes con IDs distintos | WARNING, no prohibición global de nombres |
| Nombres parecidos, como PicoScope2204A / PicoScope2205A | WARNING; pueden ser modelos diferentes |
| Metadatos ausentes/obsoletos de otro proyecto | Explicar cobertura incompleta; no ejecutar su código para comprobarlo |
| Instalación seleccionada y actualización de paquete | Se conecta en DEV-01.5; no implementarla aquí |

Política local determinista: comparar entre componentes del mismo tipo; normalizar
para comparación Unicode/case-fold y diacríticos, espacios, guiones y underscores,
conservando letras/dígitos. `Read_Wave_Form`, `Read Wave Form` y
`read-wave-form` producen el aviso de equivalencia. Como primera heurística
acotada, avisar también por distancia de edición <= 1 cuando ambas claves tengan
al menos seis caracteres. Probar la regla; no prometer comprensión semántica.
No alterar por ello la ortografía visible o la identidad. No usar IA/red.

Consultar sólo el proyecto actual y los proyectos conocidos del workspace, con
el límite existente de 1024 subdirectorios inmediatos, sin recorrido global.
Reutilizar config/metadata local verificada. Un índice es una caché, nunca fuente
de identidad ni prueba de ausencia de conflictos. Acotar lecturas, comparaciones
y resultados; agrupar avisos y mostrar nombre, tipo, proyecto y motivo.
Declarar cuándo no se pudo completar una comprobación; no devolver un falso
"sin colisiones". Mantener los errores/guards existentes de archivos corruptos,
reparse points y contención; una advertencia nueva no los degrada.

Generate compara sus entradas y los metadatos conocidos; tras Build/preparación,
ARTestDev usa los nombres reales publicados, incluso si se editaron en código.
No exige un comando shell ni ejecuta generación de otros proyectos para indexarlos.
Ni ausencia de avisos ni similitud prueban compatibilidad del instrumento.

## 6. Componentes e invariantes

Reutilizar:
- `source/ARTestDev/Authoring.*`, UI/inspección y recursos de plantillas.
- SDK nativo: `Definition.h`, `Metadata.h`, generación metadata y starter;
  funciones nuevas módulo-locales, preservando el adaptador ABI.
- SDK Python: `artest_sdk/api.py`, sus pruebas y proyección en `tools/package.py`;
  `tools/project.py`/preparación sólo para cableado y compatibilidad necesarios.
- Build/publicación incremental y staging privados existentes; no segundo
  empaquetador, preparador de entornos o validador de Engine.

Se permiten APIs de autoría aditivas y, si lo exige el modelo privado actual,
extensiones compatibles de configuración de autoría que la herramienta mantiene.
No modificar contratos de ejecución ni reinterpretar campos antiguos. Actualizar
autoridades/versionado y mínimos de SDK afectados según las reglas de distribución:
no publicar contenido nuevo bajo un artefacto histórico ni sustituir baselines.
Diagnosticar SDK insuficiente. Comandos low-level y proyectos anteriores siguen
funcionando, sin migración automática al abrir/Build ni reescritura de fuentes.

Preservar thin host/Core privado, Python fuera de proceso, compilación offline,
broker, registro explícito metadata-only sin construir instancias, integridad,
recibos, recuperación, selección anterior e incrementalidad/retención aceptadas.
Native-only no requiere Python, PowerShell, .NET ni CLI para compilar.
El SDK no incorpora Engine/CLI/intérprete Python. Generación confiable de metadatos
puede ejecutar definiciones en los procesos existentes; inspección de catálogos
no ejecuta código, no instala dependencias y nunca inicializa hardware.

## 7. Etapas pequeñas y verificables

| Paso | Objetivo / componentes | Criterio y prueba |
| --- | --- | --- |
| A — Declaraciones | Ayudas SDK, proyección metadata, identidad portable y Description | Un ejemplo C++ y Python con driver y dos comandos; nombres/Description editables una vez; IDs estables, descripción opcional y APIs antiguas compatibles |
| B — Flujo generado | Plantillas, Generate, inspección, Build/preparación existentes | Seis variantes; IDE Build C++ y preparación/reuse Python; copiar/limpiar/reordenar/añadir comando conserva identidades; sin coordinación manual de strings |
| C — Diagnósticos | ARTestDev y metadatos locales | Duplicados fallan, similares avisan, comparación consigo mismo no colisiona; cobertura parcial honesta y UI/procesos acotados |
| D — Gate | Tests, SDK extraído/staging, guía/evidencia | Matriz abajo, hashes de candidato y reporte para Architect; no iniciar DEV-01.5 |

Todos los pasos mantienen la sección 6; ninguno autoriza Engine, registro real,
Test plan/hardware o migraciones. Primera unidad técnica: A. No expandir B antes
de demostrar las declaraciones y persistencia de identidades de A.
No pedir una nueva arquitectura general; detenerse sólo si una restricción de
esta guía resulta incompatible con la solución viable y reportar evidencia.

## 8. PASS / FAIL y evidencia

PASS exige evidencia del mismo candidato:
1. Seis variantes Python/C++, incluido command-only con contrato/operación intactos;
   al menos un proyecto por lenguaje con un driver y dos comandos. No exigir
   modificar campos internos para añadir el segundo comando.
2. Cambiar cada campo editable; Description ausente/vacía/Unicode/comillas/escapes,
   límites/invalidos; IDs exactos y referencias iguales, metadata actualizada y
   campos vacíos omitidos. Version usa major.minor.patch existente.
3. Reordenar, añadir y retirar componentes, repetir Build/preparación, copiar a
   otro path/usuario y limpiar outputs sin cambiar identidades supervivientes.
   No reutilizar una identidad para otro componente; fallo/interrupción de cualquier
   escritura interna introducida preserva fuentes, asociaciones y selección previa.
4. Duplicados, similitud, nombres iguales de componentes distintos, exclusión propia,
   distintos proyectos, metadata corrupta/obsoleta/ausente y búsqueda acotada.
   Los avisos no cambian IDs ni seleccionan drivers.
5. Overrides y IDs históricos, las regresiones de sustitución en una pasada,
   MultipleInstruments, API low-level y consumidores congelados pertinentes.
   No actualizar su baseline para hacerlos pasar.
6. Build/metadata nativos reales Debug/Release con SDK externo independiente del
   checkout, SDK de sólo lectura y sin Python/PowerShell/runtime en compilación.
   Validación de descriptores con instalación CLI separada; no registro ni Test plan.
7. Preparación/reutilización Python real con intérprete compatible, SDK wheel
   candidato y metadatos con Description; validación/activación metadata-only con
   Engine actual sin hardware para detectar descriptor mismatch.
8. Tests de SDK, ARTestDev/UI y regresión Debug/Release de AGENTS; al modificar SDK
   Python ejecutar también sus gates pertinentes. Preservar Build sin cambios
   sin recompilar/generar nueva revisión y recuperación/retención previa; no
   reabrir el objetivo de rendimiento Debug ya dispuesto en DEV-01.4-A.
9. Incluir candidato/SDK/binaries y hashes, comandos exactos, exit codes, reportes,
   fallos intermedios y skips justificados. Evidencia local bajo
   `source/ARTestDev/build/dev014b-evidence/`; staging separado, sin sobrescribir
   inventarios/evidencia aceptados. `git diff --check` y whitespace de archivos nuevos.

FAIL si exige sincronizar IDs repetidos, cambia identidad al renombrar/limpiar,
pierde Description, confunde nombre parecido con incompatibilidad, rompe
command-only/legacy, requiere Engine nuevo, degrada integridad o amplía alcance.
Una prueba de compilación sola no demuestra descriptor ni identidad correctos.

## 9. Entrega y fuera de scope

Actualizar esta guía con sintaxis real breve, cómo añadir otro comando, qué cambia
al renombrar presentación frente a clase/ancla, diagnósticos, limitaciones y
evidencia. Entregar reporte compacto: archivos, comportamiento, tests/resultados,
fallos/skips, compatibilidad/versiones y hashes. Añadir prompt de revisión para
Architect que solicite `DEV-01.4-B ACCEPTED` o `DEV-01.4-B REQUIRES FIXES`.

Fuera: DEV-01.5–01.7, registro/integración y ejecución real de Test plans, hardware/
PicoSDK, GUI estética, asistentes adicionales, refactor general, migración automática
de identidades/proyectos, búsqueda de instalaciones, índices globales, IA/red,
Engine/Core/ABI/Engine API/IPC, nuevos manifests/recibos, Studio, .NET, C-03/C-04,
publicación/instaladores, commit y push. Preservar el working tree recibido.

## Implementación candidata — sintaxis y autoridad

La autoridad portable implementada es `IdentityNamespace` en las fuentes. Conserva
el namespace y los overrides iniciales de Generate. `Component(anchor)` en C++ y
`component(anchor)` en Python producen namespace + punto + ancla, salvo el override
inicial. No leen ni escriben cachés, rutas, configuración, nombres o contadores.
Las anclas son explícitas, permanentes, ASCII minúsculas/dígitos con separadores
punto/guion (máximo 64); el ID resultante conserva el límite de 160.
Copiar código conserva identidad. Cambiar una clase no cambia el ancla; cambiar
el ancla sí declara otra identidad. No se ofrece migración automática.

Ejemplo C++20 con clases de comportamiento `Scope`, `Read` y `Trigger` ya definidas:

```cpp
const artest::sdk::IdentityNamespace ids{"com.example.scope"};
artest::sdk::Extension extension{ids.Id(), "0.0.1", "Scope", "Example"};
const std::string contract = "com.example.contract.scope.v1";
extension.AddDriver<Scope>(ids.Driver("driver", "Scope", contract,
    artest::sdk::DriverMode::Simulated,
    {.schema = artest::sdk::Schema::Object(), .description = "Fuente simulada"}));
extension.AddCommand<Read>(ids.Command("read", "Read_Wave_Form",
    {.schema = artest::sdk::Schema::Object(), .requiredContracts = {contract}}));
extension.AddCommand<Trigger>(ids.Command("trigger", "Set_Trigger",
    {.schema = artest::sdk::Schema::Object(), .requiredContracts = {contract}}));
```

Equivalente Python, con clases y dataclasses de parámetros ya definidas:

```python
from artest_sdk import Extension, IdentityNamespace
ids = IdentityNamespace("com.example.scope")
contract = "com.example.contract.scope.v1"
extension = Extension(ids.identity, "0.0.1", "Scope", "Example")
extension.driver(ids.component("driver"), Scope, Configuration,
                 name="Scope", contract=contract, simulated=True,
                 description="Fuente simulada")
extension.command(ids.component("read"), Read, ReadParameters,
                  name="Read_Wave_Form", requires=(contract,))
extension.command(ids.component("trigger"), Trigger, TriggerParameters,
                  name="Set_Trigger", requires=(contract,))
```

Los ejemplos de prueba completos están en `tests/SdkMetadataTests.cpp` y
`source/ARTest.Python/tests/test_sdk.py`; sus componentes nunca adquieren hardware.
Para añadir otro comando se declara un ancla nueva y su comportamiento/schema,
sin copiar IDs largos ni añadir entradas a una tabla de overrides. El contrato
continúa siendo una declaración semántica compartida, no inferida del nombre.
Description vacío se omite. Se rechazan NUL, UTF-8/Unicode inválido y más de
512 puntos de código. La proyección Python existente del descriptor al manifiesto
preserva el campo, sin cambios de wire/Engine. SDK candidatos: nativo 0.4.1 y
Python 0.2.1; los binarios y artefactos históricos no se sustituyen.

## Flujo implementado y límites del candidato

En los proyectos nuevos del staging DEV-01.4-B, edite `Extension.cpp` (C++) o
`src/extension.py` (Python). Generate escribe allí el namespace y los overrides
iniciales para conservar exactamente los IDs legibles/avanzados aceptados.
`artest-sdk-project.json` sigue identificando el proyecto original; no es una
segunda tabla de nombres editables ni una tabla nueva que deba sincronizar el autor.
Los nombres de las clases, los anchors y los nombres visibles tienen funciones
separadas: puede cambiar una clase conservando su anchor; no cambie el anchor
para un simple cambio de presentación. Author es el argumento publisher de
`Extension`; Version usa el formato existente major.minor.patch.

Ejemplo real de cómo ampliar el proyecto combinado generado C++ (la clase
`ReadValueCommand` se reutiliza como comportamiento simulado en la prueba):

```cpp
extension.AddCommand<ReadValueCommand>(ids.Command("trigger", "Set_Trigger",
    {.schema = Schema::Object().Optional("factor", Schema::Number().Minimum(0).Maximum(10)),
     .requiredContracts = {SourceContract}, .description = "Lectura simulada adicional"}));
```

En el combinado Python, dentro de la función de definición existente:

```python
extension.command(IDENTITIES.component("trigger"), MeasureValue, MeasurementParameters,
                  name="Set_Trigger", requires=(CONTRACT,),
                  description="Lectura simulada adicional")
```

Ambos añaden sólo una declaración con anchor permanente. Los schemas nuevos usan
el ID por defecto del SDK; los schemas iniciales C++ conservan los IDs históricos.
Los contratos/operaciones se declaran una vez en el código de comportamiento y
se consumen simbólicamente por registro y broker. Command-only conserva su contrato
compartido y no incluye ni selecciona un driver externo.

Compile C++ en el IDE mediante el flujo existente; prepare Python mediante el
servicio de preparación ya existente. Abrir el proyecto o usar **Volver a comprobar**
lee la metadata publicada y muestra tipo, nombre, ID y Description. No regenera ni
restaura los nombres del formulario. Integrate continúa fuera de alcance: esta
unidad no habilita DEV-01.5 ni ejecución de Test plans.

- Mismo ID: error de registro antes de publicar; se conserva la salida/selección
  previa. Copias coexistentes en el workspace se diagnostican como la misma extensión.
- Nombre equivalente/parecido con otro ID del mismo tipo: aviso con nombres,
  proyecto y motivo. La comparación no elige un servicio ni acredita compatibilidad.
- Metadata ausente, obsoleta, corrupta, ambigua entre Debug/Release o con SDK fuente
  no verificable: cobertura incompleta. Se requiere Build/preparación explícita.
- La inspección valida fuentes, metadata e inventarios publicados; no certifica
  la disponibilidad de una instalación de destino ni todo el entorno de ejecución.
  Los servicios existentes siguen siendo autoridad de preparación y validación.
- Límites locales: 1024 subdirectorios inmediatos; 128 MiB compartidos por lectura,
  8192 entradas, profundidad 32, JSON 2 MiB/archivo, archivo de hash 64 MiB,
  inventario 4096 entradas, 256 componentes, 65536 comparaciones y 100 avisos.
  Se informa cobertura incompleta al excederlos. No hay índice persistente ni red.
- Proyectos anteriores no se migran. APIs low-level permanecen disponibles; las
  nuevas fuentes requieren SDK nativo 0.4.1/Python 0.2.1 y fallan con diagnóstico
  accionable al usar uno anterior. `tools/project.py` admite también Python 0.2.0.
- Los kits históricos all-in-one 0.4.0 mantienen su inventario y verificación
  explícita contra 0.4.0. No se vuelven a empaquetar con nuevas piezas bajo esa
  versión. El candidato se distribuye para revisión como staging independiente
  y SDK 0.4.1/wheel 0.2.1; no constituye publicación ni instalador nuevo.

El ejemplo A completo independiente está en
`tests/TestSupport/Fakes/ManagedAuthoringExample.cpp`; se compiló y ejecutó antes
de extender Generate. Las pruebas SDK de C++/Python comprueban tres componentes,
Unicode suplementario, límites, omisión del campo vacío, ausencia de construcción
de componentes y estabilidad de IDs. Las pruebas managedAuthoring de ARTestDev
editan presentación, añaden/reordenan/retiran comandos, rechazan duplicados,
reutilizan salidas y reconstruyen/preparan una copia que contiene sólo fuentes.
El probe de activación Python es un ejecutable de pruebas longPathAware que usa
la API pública existente y sólo refresca el catálogo; no crea una sesión.

El SDK de autoría DEV-01 es exclusivamente `dev014b-staging/<Configuration>`:
no contiene Engine, CLI ni intérprete Python. Los ZIP nativos 0.4.1 creados por
la regresión raíz ejercitan por separado el formato D3.3-C existente (incluido
su validador histórico y su Engine de build-time). Se conservan como evidencia
de compatibilidad de ese camino; no se proponen como kit/instalador DEV-01 ni
se incorporan a proyectos o al staging de autoría. El nuevo empaquetado público
sigue fuera de esta unidad.

## Resultado de verificación del candidato (2026-09-27)

| Gate | Resultado |
| --- | --- |
| Regresión raíz Debug / Release x64 | Exit 0 en ambas; 243 PASS y 31 deshabilitados por configuración; ABI, thin-host, SDK externo y XML/HTML correctos |
| ARTestDev Debug | Siete suites validadas. Primera CTest: 6/7; se corrigió una aserción de sintaxis antigua y se repitió Authoring completo: 50 PASS, 0 FAIL/SKIP. Producción no cambió por la corrección |
| ARTestDev Release | CTest 7/7, exit 0; Authoring 50 PASS |
| Preparación Python real | 17 PASS, 0 FAIL/SKIP por configuración, con CPython 3.13.15 y wheel 0.2.1; activación metadata-only contra el Engine correspondiente |
| SDK/worker instalados y packaging Python | 11 + 4 + 5 PASS; herramientas de proyecto: 68 casos, 2 skips por privilegio de symlink Windows (WinError 1314) |
| Consumidor congelado SDK 0.2.1 Release | 8/8 contra cada Engine; baseline sin reconstruir y hashes del Engine iguales a los de la validación |
| Kit histórico 0.4.0 y evidencia ACCEPTED | Inventario y gate de fuentes PASS; 317 hashes de evidencia aceptada intactos |

Las suites nativas completas pasaron en 2374.95 s (Debug) y 1496.44 s (Release).
Incluyen las tres variantes C++, overrides/IDs históricos, SDK externo de sólo
lectura, validación con CLI separado, auditoría de procesos, veinte builds sin
cambios, veinte ediciones, dependencias, Clean, retención e interrupciones.
Los gates Python y Generate completan las seis variantes; los 31 casos
opcionales deshabilitados de la raíz no se cuentan como cobertura Python real.
No se lanzó ningún Test plan del usuario/proyecto generado ni hardware; las
regresiones obligatorias usan sus fixtures simulados. No se inicia DEV-01.5.

Ejemplo real conservado: el proyecto nativo `managed-extension` mantiene
`managed-extension.simulated-source` al mostrar `PicoScope2204A` y
`managed-extension.measure-value` al mostrar `Read_Wave_Form`. El comando añadido
usa `managed-extension.trigger`. En Python se verifica la misma relación bajo
`managed-python`. Las copias positivas tienen tres componentes, publisher
`New author` y versión `2.3.4`; dos descripciones Unicode presentes y la del
comando adicional ausente. Véase `managed-components.json` en la evidencia.

La evidencia local está en `source/ARTestDev/build/dev014b-evidence/`:
`README.md`, `commands.md`, `final-results.json`, logs originales/corregidos,
`retained-fixtures.json`, paquetes/recibos compactos en `fixtures`, binarios y
ZIPs de regresión en `candidate`, y snapshot de fuentes. Los manifiestos
`candidate-source-hashes.json`, `candidate-staging-hashes.json` y
`evidence-hashes.json` identifican el candidato. El snapshot/diff incluye las
modificaciones preexistentes preservadas; no todo el diff contra HEAD pertenece
al trabajo nuevo. No se han hecho commit/push.

Se conservan los intentos iniciales fallidos (quoting, permisos FileTracker,
check de versión Python, uso no soportado del doctor CLI, manifiesto longPathAware
del probe y aserciones de sintaxis). Sus correcciones y repeticiones están
explicadas en README; no quedan fallos funcionales conocidos de esta matriz.
El recolector también conserva revisiones de prueba incompletas deliberadas;
una revisión no seleccionada sin paquete no equivale a corrupción del candidato.
La verificación corresponde a Windows x64 y al intérprete indicado, sin ensayo
con otro usuario Windows ni hardware. La similitud sigue siendo local/advisoria;
no se ofrecen migración de anchors, forks reidentificados ni un instalador nuevo.

### Prompt compacto para Architect

Revise exclusivamente DEV-01.4-B en `D:\GitHub\main\ARTestCLI`. Lea AGENTS, esta
guía y `source/ARTestDev/build/dev014b-evidence/README.md`, resultados y hashes.
Evalúe A–D, seis variantes, identidad portable, Description, colisiones/avisos,
compatibilidad, SDK externo, Python real e integridad/incrementalidad. Preserve
el working tree y las etapas ACCEPTED; no avance DEV-01.5 ni haga commit/push.
Emita **DEV-01.4-B ACCEPTED** o **DEV-01.4-B REQUIRES FIXES**, con hallazgos
priorizados y evidencia. El prompt copiable está también en `architect-review.txt`.

## Corrección acotada de los dos blockers (2026-09-27)

Esta revisión sustituye el candidato anterior sólo en dos puntos y fue aceptada
por el Architect. Las etapas previamente ACCEPTED no cambian.

- Generate inserta Description únicamente junto al terminador de la declaración
  Python. No vuelve a interpretar texto dentro de nombres. Los ejemplos reales
  `name="simulated=True,"` y `name="requires=(CONTRACT,),"` se conservan exactamente
  en driver-command y en sus variantes driver-only/command-only respectivas.
- `scripts/prepare-python-example.ps1` consume la ruta devuelta por la generación
  actual de `package.py sdk`, en un subdirectorio SDK nuevo. No busca por versión,
  fecha o glob entre outputs anteriores; tampoco sobrescribe wheels históricos.

La evidencia adicional está en `source/ARTestDev/build/dev014b-blockers-evidence/`:
logs Debug/Release de la suite completa de autoría, preparaciones reales con salida
limpia y con wheel 0.2.0 preexistente, recibos, versiones instaladas, hashes y
snapshot del candidato corregido. El parser real de Python comprueba nombres
exactos, namespace/IDs exactos, Description vacía por componente y ausencia del
driver en command-only. Se mantienen las pruebas anteriores de seis variantes e
identificadores legibles. `README.md` contiene resultados y límites del rerun.
La evidencia original de `dev014b-evidence` permanece intacta y no debe confundirse
con los nuevos binarios. No se repite aquí la matriz extensa ajena a estos blockers.

## Cierre del Architect (2026-09-28)

Decisión: **DEV-01.4-B ACCEPTED**. Revisión focalizada: 55 pruebas aprobadas por
configuración, sin fallos ni skips; 45 archivos fuente y 4373 archivos de evidencia
verificados sin discrepancias. Los recibos seleccionan SDK Python 0.2.1 tanto en
salida limpia como con 0.2.0 preexistente; los wheels históricos se conservan.
Antes del cierre documental se verificó de nuevo que los 45 archivos coincidían
con el candidato sellado. Las actualizaciones de estado de este cierre afectan
únicamente documentación; no reemplazan los manifiestos ni las evidencias selladas.
DEV-01.5 puede comenzar con su handoff acotado; no forma parte de esta aceptación.
