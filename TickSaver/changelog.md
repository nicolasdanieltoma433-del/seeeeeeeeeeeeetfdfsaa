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
