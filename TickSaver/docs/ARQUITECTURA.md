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
        │
        ▼
 GJBaseGameLayer::update(dt)  — cada frame
        │  por cada paso de física (240/s; con click-between-steps un paso
        │  puede partirse en dos updates más cortos)
        ▼
 PlayerObject::update(dt) del jugador 1   ◄── hook: RELOJ de ticks + reinyección (antes del original)
        │
        ▼
 checkCollisions(player, dt, false)
        │  colisión con pinchos -> PlayLayer::destroyPlayer(player, obj)  ◄── hook: muerte
        ▼
```

Se intercepta `handleButton` (estable en todas las plataformas) y se reinyecta llamando al
`handleButton` original al **principio del paso** del jugador 1, que recorre la ruta normal
del juego (dual, 2 jugadores, plataformero). Es el mismo punto que usa Afterflow.

## 3. Flujo de un click en TickSaver

```
handleButton(press)
   └─► Engine::onButton ─► reglas de paso directo (orbe bajo el click, spam, práctica, plataformero)
                       └─► InputScheduler::onPhysicalPress(tick = T)
                              └─► GhostSimulator::simulatePress()  "¿si apreto en T, vivo?"
                                     ├─ vive y hace algo ......► se deja pasar (Clean, racha +1)
                                     ├─ vive y no hace nada ...► buffer de orbes (espera un orbe)
                                     └─ muere .................► se RETIENE (el juego no se entera)

PlayerObject::update del jugador 1 (cada tick T+1, T+2, ...; antes del update original)
   └─► Engine::onRealPlayerUpdate ─► InputScheduler::onTick
          ├─ simulateInjectedPress() vive ....► reinyectar   (Saved "+N t")
          ├─ tocás un orbe (buffer de orbes) .► reinyectar   (Saved "ORBE +N t")
          ├─ muere y esperar 1 tick también ..► reinyectar   (Unsalvageable)
          ├─ se llegó al máximo de ticks .....► reinyectar   (Unsalvageable)
          └─ si no, seguir reteniendo
```

**Reloj:** `m_tick` avanza al empezar cada paso del jugador 1. El dt del paso es un
promedio (EMA, como Afterflow) que ignora pasos partidos y adopta un dt nuevo si se repite 4
veces (time warp). Si un click llega en medio de un paso (click-between-steps), el primer
paso del fantasma dura sólo lo que falta del paso real.

**Reglas de paso directo** (el click pasa sin evaluarse, `maxDelay = 0`):
* estás tocando un orbe al clickear (es justo lo que querías hacer);
* spam: en wave, nave, UFO, bola y swing, 3 inputs separados por menos de 16 ticks (6 en
  nave) pasan directo (*Dejar pasar el spam*);
* *Solo en modo práctica* fuera de práctica; plataformero sin *Ayudar en plataformero*.

**Buffer de orbes:** un click en el aire que no hace nada espera hasta `orbBufferTicks`
ticks (24 por defecto) a que el jugador real toque un orbe y se reinyecta ahí. Si seguís
apretando y el click empieza a hacer algo (por ejemplo aterrizás), se entrega igual. Si no
aparece ningún orbe, un tap se descarta sin contar; un botón mantenido se entrega.

**Por qué evaluar tick a tick** (y no calcular de una el offset ideal):
cada evaluación usa el estado real del nivel en ese tick (objetos que se movieron, triggers
ya disparados), el costo se reparte en el tiempo, y los clicks buenos nunca suman latencia.

**Margen de seguridad:** el primer tick en que apretar sobrevive es el *borde* de la ventana
segura (el cubo pasa el pincho por un pelo). Si el tick siguiente también es seguro, el mod
espera hasta `safetyMargin` ticks para disparar adentro de la ventana y no en el límite.

**Autocalibración de latencia:** antes de reinyectar un press del cubo, el simulador predice
la posición K=6 ticks después con 0, 1 y 2 ticks de retraso; luego la compara con el jugador
real y vota. Con 3 votos consistentes ajusta la latencia de todas las predicciones de
reinyección. Si ninguna coincide, lo avisa en el log (el problema es otro: orden del paso,
campos sin copiar u objetos móviles).

**Predicción fallida:** si moriste dentro de la ventana que el fantasma daba por segura tras
una salvada, se cuenta en `failedSaves`, se avisa en el HUD/resumen y se loguea con el tick,
el modo, el % y la latencia calibrada.

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
├── mod.json                 settings (presets, ticks, tramo, ayudas, HUD, 8 colores, avanzado, teclas)
├── CMakeLists.txt           build estándar de Geode (setup_geode_mod)
├── src/
│   ├── main.cpp             $on_mod(Loaded): settings, modelo adaptativo, teclas rápidas
│   ├── core/                C++23 puro, SIN Geode -> testeable en cualquier PC
│   │   ├── Types.hpp            Tick, Mode, Slot, ClickKind, paleta
│   │   ├── Config.*             Config + tabla de presets + ticks efectivos
│   │   ├── ConfigCode.*         exportar/importar "TS2-..." (Base32 + CRC-8)
│   │   ├── InputScheduler.*     máquina de estados retener/reinyectar + buffer de orbes
│   │   ├── AdaptiveModel.*      EMA de earliness y de hold por modo
│   │   └── Stats.*              Session (racha) y LevelStats (histogramas)
│   ├── game/                puente con GD
│   │   ├── Engine.*             orquestador, uno por PlayLayer (reloj, reglas, reinyección)
│   │   ├── GhostSimulator.*     IPredictor real: fantasmas PlayerObject en una capa aparte
│   │   ├── SimGuard.*           estado "simulando" + fotos de objetos + usados/apagados
│   │   ├── GameMode.*           detección de modo, ventana por modo, reglas de spam
│   │   ├── LiveLog.*            últimos 64 eventos (pestaña Log del menú)
│   │   ├── Settings.*           settings de Geode <-> Config, presets sin bucles
│   │   └── StatsStore.*         JSON por nivel + modelo adaptativo
│   ├── ui/
│   │   ├── HudNode.*            racha, barra de timing, "esperando", "SALVADO +Nt", "ORBE +Nt"
│   │   ├── DeathSummaryNode.*   resumen al morir
│   │   ├── ModMenuPopup.*       menú: General/Modos/Tramo/Ayudas/Tecnico/HUD/Log
│   │   ├── FloatingButton.*     botón "TS" arrastrable dentro del nivel
│   │   ├── StatsPopup.*         stats + histograma + borrar
│   │   └── Palette.hpp          RGBA -> cocos
│   └── hooks/
│       ├── InputHooks.cpp       handleButton y PlayerObject::update (reloj)
│       ├── PlayLayerHooks.cpp   init, resetLevel, destroyPlayer, levelComplete, onQuit
│       ├── GhostGuardHooks.cpp  aislamiento del fantasma (ver §6)
│       └── PauseLayerHooks.cpp  botón "TS" en pausa y en el menú principal
└── tests/                   tests de core/ (CMake independiente, sin Geode)
```

La regla de dependencias es `hooks → game/ui → core`. `core/` no incluye nada de Geode, por
eso la lógica más delicada (cuándo retener, cuándo soltar, el buffer de orbes, presets, el
código TS2) se prueba sin abrir el juego: `tests/core_tests.cpp` (131 checks).

## 5. Hooks

| Hook | Para qué | Archivo |
|---|---|---|
| `GJBaseGameLayer::handleButton(bool, int, bool)` | interceptar y retener el click de salto | InputHooks |
| `PlayerObject::update(float)` (jugador 1) | reloj de ticks; reevaluar y reinyectar antes del paso | InputHooks |
| `PlayLayer::init(GJGameLevel*, bool)` | crear el `Engine` (en `m_fields`), el HUD y el botón flotante | PlayLayerHooks |
| `PlayLayer::resetLevel()` | nuevo intento: racha, scheduler, modo seguro | PlayLayerHooks |
| `PlayLayer::destroyPlayer(PlayerObject*, GameObject*)` | muerte del fantasma (flag) o real (stats) | PlayLayerHooks |
| `PlayLayer::levelComplete()` / `onQuit()` | guardar stats, liberar fantasmas | PlayLayerHooks |
| `PauseLayer::customSetup()` / `MenuLayer::init()` | botón "TS" que abre el menú del mod | PauseLayerHooks |
| ~50 hooks de aislamiento | ver §6 | GhostGuardHooks |

`destroyPlayer` se registra con `Priority::First`: si un noclip de otro mod corta la cadena
antes que nosotros, nunca veríamos morir al fantasma y cualquier click "salvaría".
`handleButton` y `PlayerObject::update` usan `Priority::Early`.

## 6. El fantasma

**Creación** (al terminar de cargar el nivel, nunca en medio de un paso de física):
`PlayerObject::create(1, 1, layer, ghostLayer, true)` en una **capa propia que nunca se
agrega a la escena**, con `unscheduleUpdate()`, invisible y sin efectos. Lo que el juego le
cuelga al fantasma (estelas, partículas, círculos) se barre después de cada simulación. Hay
dos fantasmas, uno por jugador.

**Sincronización** antes de cada simulación (aislada con `sim::SyncScope`):
1. `resetTouchedRings(true)` + `copyAttributes(real)`;
2. copia extendida de ~110 campos (la lista de Afterflow con los nombres de 2.2091):
   modo, velocidad, gravedad, mini, pendientes, suelo, colisiones, dash, fuerzas;
3. estado de **orbes**: `m_touchingRings` (orbes que estás tocando ahora), `m_usedRings`,
   `m_touchedRings`, `m_jumpBuffered`, `m_statePreJump`...;
4. **tiempos** internos: margen tras un portal, último salto, flips de gravedad/araña;
5. flags de física por versión del nivel (`m_enable22Changes`, `m_enableImpulseFix`...).

**Un paso** (el orden del juego, como Afterflow): `update(dt)` → `updateRotation(dt)` →
vaciar los 4 logs de colisión → `checkCollisions(ghost, dt, false)`. Hasta v0.3 el orden era
al revés (colisiones primero), y esa era la causa más probable de "dice salvado y me mata".

**Filtro de colisiones** (`collisionCheckObjects`): el fantasma sólo revisa sólidos,
pinchos, pendientes, pads, orbes, dash y portales de gravedad. Los portales de
**velocidad** se aplican a mano (`m_playerSpeed`: 0.7 / 0.9 / 1.1 / 1.3 / 1.6), los de
**tamaño** con `togglePlayerScale`, y los de modo, dual, teleport o el final del nivel
terminan la simulación: llegar vivo hasta ahí cuenta como que el click sirvió (pero no
alcanza para declarar un click "innecesario"). Los pinchos que el jugador real ya está
tocando al empezar se ignoran, y los objetos que un orbe apaga dejan de chocar.

**Orbes y pads:** se ejecutan con la física real. Antes de que el juego modifique un objeto
se guarda una foto (`m_isActivated`, `m_activated`, `m_activatedByPlayer1/2`) que se
restaura al terminar. Un pad se usa una vez por simulación; un orbe, hasta que realmente
cambia la velocidad, la gravedad o el dash. Si el jugador real ya lo usó, el fantasma
tampoco puede (se le pregunta al juego con el jugador real).

**Ventana por modo** (`game/GameMode.cpp`):

| Modo | Ventana | Fin anticipado | Hold por defecto |
|---|---|---|---|
| Cubo, Robot | 120t | al aterrizar + 6t | 8t / 12t |
| Bola | 60t | al aterrizar + 6t | 4t |
| Araña | 40t | al aterrizar + 4t | 4t |
| UFO, Swing | 24t | — | 6t |
| Nave | hold + 10t (máx. 48t) | — | 14t |
| Wave | hold + 10t (máx. 40t) | — | 10t |

**Presupuesto:** cada paso del fantasma es un `checkCollisions`, caro en niveles con muchos
objetos. Hay un límite de **tiempo de CPU por frame** (*CPU por frame*, 2 ms por defecto).
Si se agota, la simulación devuelve `Skipped` y no se retiene nada nuevo.

**Reinyección:** por defecto con el `handleButton` original al principio del paso del
jugador 1 (como Afterflow). *Reinyección directa* (opcional) usa `PlayerObject::pushButton`.

**Aislamiento** (`hooks/GhostGuardHooks.cpp`):

| Efecto | Tratamiento |
|---|---|
| orbes, pads, dash (`ringJump`, `bumpPlayer`, `playerTouchedRing`, `startDashing`, `activatedByPlayer`, `canBeActivatedByPlayer`) | se ejecutan con foto y restauración; una vez por simulación |
| gravedad (`flipGravity`) | `PlayerObject::flipGravity` del fantasma, sin tocar cámara ni suelo |
| grupos que se apagan (`toggleGroupTriggered`) | el fantasma deja de chocar esos objetos |
| portal de modo, dual, teleport, final del nivel | fin de la simulación |
| triggers (`triggerObject`, `spawnGroup`, `toggleGroup`, contadores, items), monedas, eventos, checkpoints | bloqueados |
| partículas, estelas, círculos, rotaciones, efectos de muerte, contador de saltos | bloqueados |
| muerte (`destroyPlayer`, `playerDestroyed`) | se anota como resultado; el jugador real no se entera |

**Limitación conocida:** durante la simulación el nivel queda "congelado" (no corren move
triggers). Con ventanas cortas y la reevaluación tick a tick el error es chico.

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
| General | mod activado, preset (< >), ticks máximos, modo seguro, margen de seguridad |
| Modos | un botón por personaje (Cubo, Nave, Bola, UFO, Wave, Robot, Araña, Swing) y slider de ticks propios; 0 = usa el general |
| Tramo | activar, desde %, hasta %, ticks extra |
| Ayudas | ignorar clicks innecesarios, corregir el soltar, buffer de orbes (+ ventana), dejar pasar el spam, adaptativo, 2 jugadores, conservar hold, sólo práctica, plataformero |
| Tecnico | copia extendida, reinyección directa, log de depuración, validar fantasma, CPU por frame, latencia calibrada |
| HUD | HUD/barra/resumen, sonido al salvar, trayectoria, botón flotante, escala y los 8 colores |
| Log | los últimos eventos del mod, con "Copiar log" y "Limpiar" |

También se abre con el **botón flotante** dentro del nivel (opcional, arrastrable) y en PC
con las teclas rápidas **F7** (menú), **F8** (activar/desactivar) y una tecla configurable
para pasar al siguiente preset.

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

## 16. Créditos

Varias técnicas de la v0.4 están adaptadas de **Afterflow** (licencia MIT, (c) 2026 prsevend,
https://github.com/afterflow-gd/afterflow): el reloj y la reinyección en
`PlayerObject::update`, la capa aparte del fantasma, el orden del paso, la lista de campos
copiados, el filtro de `collisionCheckObjects` (portales de velocidad y tamaño, objetos
apagados, pinchos que ya tocás), las fotos de objetos, el buffer de orbes, la regla del
spam, el botón flotante y el log en vivo. No se copió su auto-actualizador ni su terminal.
