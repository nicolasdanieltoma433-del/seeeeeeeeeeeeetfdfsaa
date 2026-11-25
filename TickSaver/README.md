# TickSaver

Mod de **Geode** para **Geometry Dash 2.2091** (Windows y Android), pensado para practicar.
Si clickeás unos ticks antes de tiempo y ese click te iba a matar, TickSaver simula el futuro
con un "fantasma" invisible y retrasa el click hasta el primer tick en el que sobrevivís.

> Es un cheat. Con el **modo seguro** (activado por defecto) los intentos asistidos no
> guardan progreso. No es para récords ni listas.

## Funciones

- Predicción y corrección de clicks tempranos, hasta 240 ticks, para cubo, nave, bola, UFO,
  wave, robot, araña y swing (con sus propios ticks); dual y modo 2 jugadores
- Modo adaptativo: aprende cuán temprano clickeás y cuánto mantenés el botón
- HUD: racha `x12`, barra de timing (temprano ↔ justo), aviso `SALVADO +3t`
- Resumen al morir y estadísticas por nivel con histograma 0–100 %
- Presets Suave, Normal, Fuerte, Máximo, Wave y Entrenar; ticks extra en un tramo del nivel
- Paleta de 8 colores; exportar/importar config con un código `TS2-...`
- Menú in-game (botón **TS** en pausa y en el menú principal) para configurar todo

## Estructura

```
src/core    lógica pura (sin Geode), con tests en tests/
src/game    motor, simulador fantasma, settings, guardado
src/ui      HUD, resumen al morir, popup de estadísticas
src/hooks   $modify sobre GJBaseGameLayer, PlayLayer, PlayerObject, PauseLayer
```

La arquitectura completa (flujo de un click, hooks, aislamiento del fantasma, formato TS2,
puntos a calibrar) está en [docs/ARQUITECTURA.md](docs/ARQUITECTURA.md).

## Compilar

**En la nube:** cada push que toca `TickSaver/` dispara el workflow
`.github/workflows/ticksaver.yml`; el `.geode` queda en *Actions → Build Output*.

**Local:**

```sh
geode sdk install
geode sdk install-binaries
cd TickSaver
geode build
```

**Tests de la lógica** (no necesitan Geode):

```sh
cmake -S tests -B build-tests
cmake --build build-tests
./build-tests/core_tests
```

Antes de publicar cambiá `id` y `developer` en `mod.json`.
