# TickSaver: arquitectura técnica

Versión objetivo: **Geode v6.0.0, Geometry Dash 2.2091**, en Windows y Android (32 y 64 bits).
Todas las firmas de este documento se verificaron contra `geode-sdk/bindings` (`bindings/2.2091/GeometryDash.bro`).

---

## 1. Qué problema resuelve

GD simula la física a **240 ticks por segundo**. Un click que llega 1–3 ticks antes de lo
necesario suele matar: el cubo salta antes del borde, la wave sube un píxel antes de tiempo.
TickSaver mira hacia adelante: **si el click en su tick original mata, lo retiene** y lo
reinyecta en el primer tick posterior en el que sí sobrevive, siempre dentro de un máximo
configurable (hasta 255t ≈ 1,06 s).

## 2. Cómo GD procesa un click (2.2091)

```
 input del SO (teclado/táctil/control)
        │
        ▼
 GJBaseGameLayer::handleButton(down, button, isPlayer1)   ◄── hook: punto de entrada
        │  (internamente: queueButton -> m_queuedButtons, con timestamp)
        ▼
 GJBaseGameLayer::update(dt)  — cada frame
        │  por cada paso de física (240/s; con click-between-steps
        │  puede haber "medios pasos" hasta el timestamp del input)
        ▼
 GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick)  ◄── hook: reloj de ticks
        │  procesa la cola -> PlayerObject::pushButton / releaseButton
        ▼
 PlayerObject::update(dt) + checkCollisions(player, dt, false)
        │  colisión con pinchos -> PlayLayer::destroyPlayer(player, obj)  ◄── hook: muerte
        ▼
```

`queueButton` es *inline* en Windows, por eso **no** se engancha la cola: se intercepta
`handleButton` (estable en todas las plataformas) y se reinyecta llamando al `handleButton`
original, que recorre la ruta normal del juego (dual, 2 jugadores, grabación de replays).

## 3. Flujo de un click en TickSaver

```
handleButton(press)
   └─► Engine::onButton ─► InputScheduler::onPhysicalPress(tick = T)
                              └─► GhostSimulator::simulatePress()  "¿si apreto en T, vivo?"
                                     ├─ vive ──► se deja pasar (Clean, racha +1)
                                     └─ muere ─► se RETIENE (el juego no se entera)

processCommands (cada tick completo T+1, T+2, ...)
   └─► Engine::beforeStep ─► InputScheduler::onTick
          ├─ simulatePress() vive ............► reinyectar   (Saved "+N t")
          ├─ muere y esperar 1 tick también ..► reinyectar   (Unsalvageable)
          ├─ se llegó al máximo de ticks .....► reinyectar   (Unsalvageable)
          └─ si no, seguir reteniendo
```

**Por qué evaluar tick a tick** (y no calcular de una el offset ideal):
cada evaluación usa el estado real del nivel en ese tick (objetos que se movieron, triggers
ya disparados), el costo se reparte en el tiempo, y los clicks buenos nunca suman latencia.

**Soltar temprano (nave/wave):** si soltar ahora te mata, el soltar se retiene y se
reinyecta en el primer tick en que soltar es seguro (`IPredictor::simulateRelease`).

**Clicks innecesarios (modos de toque):** si un click retenido sobrevive pero *no apretar*
también sobrevive (`simulateNoPress`, con una ventana más larga), el click no hacía falta y
se descarta en vez de dispararse tarde. Los clicks que no cambian la trayectoria
(`pressHasEffect`, 8 pasos con y sin apretar) no cuentan para la racha ni las stats.

**Release durante la retención:** si soltás antes de que el press se aplique, el release
también se retiene; con *Conservar duración del hold* se reinyecta `hold` ticks después del
press, así nave/wave/robot mantienen exactamente lo que mantuviste.

## 4. Capas y archivos

```
TickSaver/
├── mod.json                 settings (presets, ticks, tramo, HUD, 8 colores, avanzado)
├── CMakeLists.txt           build estándar de Geode (setup_geode_mod)
├── src/
│   ├── main.cpp             $on_mod(Loaded): settings + modelo adaptativo
│   ├── core/                C++23 puro, SIN Geode -> testeable en cualquier PC
│   │   ├── Types.hpp            Tick, Mode, Slot, ClickKind, paleta
│   │   ├── Config.*             Config + tabla de presets + ticks efectivos
│   │   ├── ConfigCode.*         exportar/importar "TS2-..." (Base32 + CRC-8)
│   │   ├── InputScheduler.*     máquina de estados retener/reinyectar (IPredictor)
│   │   ├── AdaptiveModel.*      EMA de earliness y de hold por modo
│   │   └── Stats.*              Session (racha) y LevelStats (histogramas)
│   ├── game/                puente con GD
│   │   ├── Engine.*             orquestador, uno por PlayLayer
│   │   ├── GhostSimulator.*     IPredictor real: fantasmas PlayerObject
│   │   ├── SimGuard.*           estado "simulando" + libro de activaciones
│   │   ├── GameMode.*           detección de modo + ventana de simulación por modo
│   │   ├── Settings.*           settings de Geode <-> Config, presets sin bucles
│   │   └── StatsStore.*         JSON por nivel + modelo adaptativo
│   ├── ui/
│   │   ├── HudNode.*            racha, barra de timing, "esperando", "SALVADO +Nt"
│   │   ├── DeathSummaryNode.*   resumen al morir
│   │   ├── ModMenuPopup.*       menu in-game: pestanas General/Modos/Tramo/Opciones/HUD
│   │   ├── StatsPopup.*         stats + histograma + borrar
│   │   └── Palette.hpp          RGBA -> cocos
│   └── hooks/
│       ├── InputHooks.cpp       handleButton, processCommands, PlayerObject::update
│       ├── PlayLayerHooks.cpp   init, resetLevel, destroyPlayer, levelComplete, onQuit
│       ├── GhostGuardHooks.cpp  aislamiento del fantasma (ver §6)
│       └── PauseLayerHooks.cpp  botón "TS" en pausa y en el menú principal
└── tests/                   tests de core/ (CMake independiente, sin Geode)
```

La regla de dependencias es `hooks → game/ui → core`. `core/` no incluye nada de Geode, por
eso la lógica más delicada (cuándo retener, cuándo soltar, presets, el código TS2) se prueba
sin abrir el juego: `tests/core_tests.cpp` (75 checks).

## 5. Hooks

| Hook | Para qué | Archivo |
|---|---|---|
| `GJBaseGameLayer::handleButton(bool, int, bool)` | interceptar y retener el click de salto | InputHooks |
| `GJBaseGameLayer::processCommands(float, bool, bool)` | reloj de ticks; reevaluar y reinyectar | InputHooks |
| `PlayerObject::update(float)` | leer el dt real del paso para el fantasma | InputHooks |
| `PlayLayer::init(GJGameLevel*, bool)` | crear el `Engine` (en `m_fields`) y el HUD | PlayLayerHooks |
| `PlayLayer::resetLevel()` | nuevo intento: racha, scheduler, modo seguro | PlayLayerHooks |
| `PlayLayer::destroyPlayer(PlayerObject*, GameObject*)` | muerte del fantasma (flag) o real (stats) | PlayLayerHooks |
| `PlayLayer::levelComplete()` / `onQuit()` | guardar stats | PlayLayerHooks |
| `PauseLayer::customSetup()` / `MenuLayer::init()` | botón "TS" que abre el menú del mod | PauseLayerHooks |
| 20 hooks de aislamiento | ver §6 | GhostGuardHooks |

`destroyPlayer` se registra con `Priority::First`: si un noclip de otro mod corta la cadena
antes que nosotros, nunca veríamos morir al fantasma y cualquier click "salvaría".
`handleButton` usa `Priority::Early` para ver el click antes que otros mods de input.

## 6. El fantasma

**Creación** (al terminar de cargar el nivel, nunca en medio de un paso de física):
`PlayerObject::create(1, 1, layer, layer->m_objectLayer, true)`, invisible, sin efectos,
sin estelas ni partículas, registrado en `SimGuard`. Hay dos: uno por jugador.

**Sincronización** antes de cada simulación (aislada con `sim::SyncScope`):
1. `copyAttributes(real)`, lo que usa GD para clonar jugadores en el portal dual;
2. `real->saveToCheckpoint(cp)` + `ghost->loadFromCheckpoint(cp)`, la **copia del modo
   práctica**: velocidad, gravedad, mini, dash de orbes, pendientes, suelo y colisiones;
3. estado de **orbes** que el checkpoint no guarda: `m_touchingRings` (orbes que estás
   tocando ahora), `m_usedRings`, `m_jumpBuffered`, `m_statePreJump`...;
4. **tiempos** internos: margen tras un portal, último salto, flips de gravedad/araña;
5. flags de física por versión del nivel (`m_enable22Changes`, `m_enableImpulseFix`...).

Sin el punto 3 el fantasma no podía activar el orbe que estabas tocando al clickear,
"moría", y el mod retenía un click correcto. La copia completa se puede apagar en
*Opciones → Copia completa del jugador*.

**Un paso:** `checkCollisions(ghost, dt, false)` → `ghost->update(dt)` → `updateRotation(dt)`.

**Ventana por modo** (`game/GameMode.cpp`):

| Modo | Ventana | Fin anticipado | Hold por defecto |
|---|---|---|---|
| Cubo, Robot | 120t | al aterrizar + 6t | 8t / 12t |
| Bola | 60t | al aterrizar + 6t | 4t |
| Araña | 40t | al aterrizar + 4t | 4t |
| UFO, Swing | 20t | — | 6t |
| Nave | hold + 6t (máx. 40t) | — | 14t |
| Wave | hold + 4t (máx. 30t) | — | 10t |

En nave y wave la ventana termina poco después del soltar: lo que pase después depende
de tu próximo click. Mirar más lejos hacía que el fantasma "muriera" por inputs futuros y
el mod retuviera clicks correctos.

**Presupuesto:** cada paso del fantasma es un `checkCollisions` completo, caro en niveles
con muchos objetos. Hay un límite de **tiempo de CPU por frame** (*CPU por frame*, 2 ms por
defecto). Si se agota, la simulación devuelve `Skipped` y no se retiene nada nuevo. El
chequeo del click recién apretado tiene su propio presupuesto.

**Reinyección:** por defecto el click retrasado se aplica con `PlayerObject::pushButton`
sobre los jugadores reales, lo mismo que hizo el fantasma, en el mismo paso. En modo
2 jugadores (o con *Reinyección directa* apagada) se usa el `handleButton` original.

En modos de salto el click se da por bueno cuando el fantasma vuelve a apoyarse vivo: el
obstáculo siguiente depende del próximo click, no de este.

**Aislamiento** (`hooks/GhostGuardHooks.cpp`). `checkCollisions` no sólo mueve al jugador,
también toca el nivel. Con el fantasma:

| Efecto | Tratamiento |
|---|---|
| orbes, pads (`ringJump`, `bumpPlayer`, `playerTouchedRing`, `activatedByPlayer`) | se ejecutan; los flags `m_activatedByPlayer1/2` se guardan y **se restauran** al terminar |
| gravedad (`flipGravity`) | se ejecuta sin efectos visuales |
| portal de modo, dual, teleport, final del nivel | se corta la simulación como **inconclusa** (= no retener) |
| triggers por contacto, monedas/items, `destroyObject`, eventos de juego, checkpoints | bloqueados |
| partículas, estelas, círculo de portal, dash de araña, contador de saltos | bloqueados |
| muerte (`PlayLayer::destroyPlayer`) | se anota como resultado; el jugador real no se entera |

**Limitación conocida:** durante la simulación el nivel queda "congelado" (no corren move
triggers). Con ventanas cortas y la reevaluación tick a tick el error es chico, pero en
partes con muchos objetos móviles la predicción puede equivocarse.

## 7. Dual y 2 jugadores

* **1 jugador, sin dual:** cualquier botón → P1.
* **Dual de 1 jugador:** el click mueve a los dos; sólo es bueno si **sobreviven ambos**
  fantasmas (configurable con *Corregir a los dos jugadores*).
* **Modo 2 jugadores** (`m_level->m_twoPlayerMode`): cada botón tiene su propio slot en el
  scheduler, su propio fantasma y su propia cola de retención. Se reinyecta con el mismo
  `isPlayer1` que tenía el click original.

## 8. Modo adaptativo ("aprende de vos")

`core/AdaptiveModel` mantiene, por modo, medias móviles exponenciales (α = 0,15) de:

1. **cuán temprano clickeás** cuando te salva → el máximo base del modo se achica a
   `media + 2,5·desvío + 1`, para no "salvar" clicks que en realidad fueron errores gruesos;
2. **cuánto mantenés** el botón → el fantasma simula tu hold real (clave en nave/wave).

Necesita 8 muestras antes de cambiar algo, nunca supera el máximo configurado, y el extra del
tramo difícil se suma **después** (es una decisión explícita tuya). Se guarda en `saved.json`
y es global: tu forma de jugar no cambia de nivel a nivel.

## 9. Presets y tramo difícil

| Preset | Global | Override por modo | Adaptativo |
|---|---|---|---|
| Suave | 3t | Swing 2t | no |
| Normal | 6t | Wave 5t, Swing 4t | no |
| Fuerte | 12t | Wave 10t, Swing 8t | no |
| Maximo | 255t | — | no |
| Wave | 6t | Nave 12t, Wave 24t, Swing 12t | no |
| Entrenar | 4t | Wave 3t, Swing 3t | sí |

Elegir un preset escribe esos valores en los settings; tocar uno a mano pasa el preset a
*Personalizado* (`game/Settings.cpp` evita el bucle de eventos con un flag de escritura).

Ticks efectivos = `min(255, base_del_modo [ajustado por adaptativo] + extra_del_tramo)`,
con el tramo activo sólo entre *Desde %* y *Hasta %*.

## 10. Persistencia

* `<save dir>/levels/id-<levelID>.json` (o `local-<hash>.json` para niveles sin ID):
  intentos, muertes, clicks, salvados, sin salvación, mejor racha, suma/máximo de ticks
  salvados, mejor %, y dos histogramas de 100 celdas (1 %): `savesByPct` y `deathsByPct`.
  Se escribe con `file::writeStringSafe` (temporal + rename), como mucho cada 3 s
  en práctica, y siempre al completar o salir.
* `saved.json` → `"adaptive"`: el modelo adaptativo.

## 11. Código de configuración `TS2-...`

Formato v3: 28 bytes de datos (ticks de 16 bits; hoy el tope es 255) + 32 si la paleta no es la de
fábrica + CRC-8, en Base32 Crockford agrupado de a 5 caracteres (60 caracteres sin paleta).
Los códigos v2 (ticks de 1 byte), como `TS2-08ZG8-1G01G-00060-00062-GF0C0-WYCP`, se
siguen importando. Al importar se aceptan minúsculas, sin guiones,
y `I/L→1`, `O→0`. Detalle byte a byte en `src/core/ConfigCode.hpp`.

## 12. Menú in-game

`ui/ModMenuPopup` (botón **TS** en pausa y en el menú principal) edita todo sin salir del
juego, escribiendo en los mismos settings de Geode con `settings::update`:

| Pestaña | Qué tiene |
|---|---|
| General | mod activado, preset (< >), ticks máximos, modo seguro |
| Modos | un botón por personaje (Cubo, Nave, Bola, UFO, Wave, Robot, Araña, Swing) y slider de ticks propios; 0 = usa el general |
| Tramo | activar, desde %, hasta %, ticks extra |
| Opciones | adaptativo, 2 jugadores, conservar hold, log, validar fantasma, presupuesto |
| HUD | mostrar HUD/barra/resumen, escala y los 8 colores (selector de color de Geode) |

Abajo: Estadísticas, Exportar, Importar y Restablecer, y el aviso fijo de responsabilidad.
Desactivar el modo seguro pide confirmación con una advertencia de baneo.

## 13. Modo seguro

Cuando un intento recibe ayuda (un *Saved* o *Unsalvageable*), el motor pone
`m_isTestMode = true` (lo mismo que hace una start position): GD no guarda progreso,
récords ni completados de ese intento. Se restaura antes del `resetLevel` original.

## 14. Puntos a calibrar en el juego

Esto no se puede verificar sin correr GD. Activar **Log de depuración** y **Validar fantasma**:

1. **Orden del paso del fantasma** (`GhostSimulator::step`). Si *Validar fantasma* reporta
   deriva constante en un modo, probar `update()` antes de `checkCollisions()`.
2. **Medios ticks** (`Engine::beforeStep`). Confirmar en el log que `tick` avanza ~240 por
   segundo. Si avanza ~480, los medios ticks no se están filtrando bien.
3. **Latencia de reinyección.** El `handleButton` reinyectado debe aplicarse en el mismo
   paso. Si el log muestra saves de "+1t" de más, compensar en `Engine::inject`.
4. **Campos de `sync`.** Si el fantasma deriva sólo en mini, con portales de velocidad o en
   plataformero, falta copiar algún campo en `GhostSimulator::sync`.
5. **Orbes.** Probar orbes amarillos, azules, de dash y de araña: si el juego marca otro
   flag además de `m_activatedByPlayer1/2`, el orbe real podría quedar "usado".
6. **Ventanas por modo** (`GameMode.cpp`): ajustar si hay falsos "muere" (ventana larga) o
   saves que no salvan (ventana corta).

## 15. Compilar

* **Nube (sin instalar nada):** el workflow `.github/workflows/ticksaver.yml` del repo
  compila Windows, Android32 y Android64 con `geode-sdk/build-geode-mod` y sube el `.geode`
  como artifact.
* **Local:** instalar la Geode CLI, `geode sdk install`, `geode sdk install-binaries`, y en
  `TickSaver/`: `geode build` (Windows: Visual Studio 2022; Android: NDK r27+).
* **Tests de core:** `cmake -S tests -B build-tests && cmake --build build-tests && ./build-tests/core_tests`.

Antes de publicar: cambiar `id` y `developer` en `mod.json`.
