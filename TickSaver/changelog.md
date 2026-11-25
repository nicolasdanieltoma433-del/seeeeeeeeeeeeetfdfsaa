# v0.3.0
- Maximo de ticks: de 240 a 255. Sliders con escala cuadratica para ser precisos en valores chicos
- Clicks innecesarios: si un click te iba a matar y no hacerlo tambien sobrevive, se descarta ("CLICK IGNORADO") en vez de dispararse tarde
- Los clicks que no hacen nada (por ejemplo cubo en el aire) ya no cuentan para la racha ni las estadisticas
- Nave y wave: ahora tambien se corrige el SOLTAR temprano ("SALVADO +Nt (soltar)")
- Ventanas de nave/wave ajustadas
- Codigo TS2 en formato v3 (ticks de 16 bits y opciones nuevas); los codigos viejos se siguen importando
- Opciones en dos columnas con "Descartar clicks innecesarios" y "Corregir el soltar"

# v0.2.2
- Velocidad, gravedad y orbes: el fantasma copia el estado completo del jugador con el sistema de checkpoints del modo practica (velocidad, gravedad, mini, dash, pendientes, suelo)
- Orbes: el fantasma ahora sabe que orbes estas tocando y cuales ya usaste en este click, asi un click en un orbe ya no se retiene por error
- Tiempos internos copiados (margen tras portales, ultimo salto, flip de gravedad y de arana)
- Opcion "Copia completa del jugador" en el menu (activada por defecto)

# v0.2.1
- Crash al morir (a partir del tercer intento): el resumen al morir usaba un puntero colgado. Corregido
- Rendimiento: el fantasma tiene un limite de CPU por frame (ms), para que niveles pesados como Slaughterhouse no se traben
- Clicks: el click retrasado se aplica directo al jugador (igual que en la simulacion); opcion para volver al metodo anterior
- Nave y wave: la simulacion ya no mira mas alla de tu hold (antes retenia clicks que estaban bien)
- Dos taps rapidos ya no se funden en un hold largo
- El fantasma copia los flags de fisica del nivel (2.1/2.2)
- Estabilidad: fantasmas creados al cargar el nivel, resumen y guardado fuera del paso de fisica, checkpoints de practica bloqueados para el fantasma
- Modo seguro: apagarlo libera tambien el intento actual; el HUD muestra INTENTO ASISTIDO: NO GUARDA

# v0.2.0
- Menu in-game con pestanas (General, Modos, Tramo, Opciones, HUD)
- Ticks por personaje, modo seguro con confirmacion y aviso de responsabilidad
- Boton TS tambien en el menu principal

# v0.1.0
- Base del mod: retencion de clicks tempranos con simulacion fantasma
- HUD de racha y barra de timing, resumen al morir
- Estadisticas por nivel con histograma
- Presets, tramo dificil, modo adaptativo, paleta de 8 colores
- Codigo de configuracion TS2
