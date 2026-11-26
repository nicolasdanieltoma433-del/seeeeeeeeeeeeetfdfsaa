# v0.4.1
- "Ignorar clicks innecesarios" (pestana Ayudas): activado, los clicks que no hacian falta se descartan y los que no hacen nada no cuentan; desactivado, todos los clicks pasan y cuentan como normales

# v0.4.0
- Fantasma rehecho con las tecnicas de Afterflow (MIT, prsevend): ahora sigue el orden real del paso (moverse y despues chocar), vive en una capa aparte y copia unos 110 datos del jugador. Arregla "dice salvado y me mata igual"
- Portales de velocidad y de tamano: el fantasma los respeta. Los portales de modo, dual y teleport cortan la prediccion sin tocar el nivel
- Orbes y pads: cada uno se usa una vez por prediccion y nunca uno que ya usaste; los objetos que un orbe apaga dejan de chocar; los pinchos que ya estas rozando no matan al fantasma
- El reloj y la reinyeccion se movieron al paso del jugador (como Afterflow): el click entra justo al principio del tick que predijo el fantasma. Soporta pasos partidos (click-between-steps) y time warp
- Nuevo: buffer de orbes. Un click en el aire un poco antes de un orbe espera y se aplica al tocarlo ("ORBE +Nt")
- Nuevo: un click sobre un orbe que ya estas tocando nunca se retrasa
- Nuevo: dejar pasar el spam en wave, nave, UFO, bola y swing (3 inputs muy seguidos pasan directo)
- Nuevo: solo en modo practica, ayudar en plataformero (experimental)
- Nuevo: sonido al salvar, mostrar la trayectoria salvada y boton flotante "TS" arrastrable dentro del nivel
- Nuevo: pestana Log con los ultimos eventos (copiar y limpiar) y pestana Tecnico con la latencia calibrada
- Nuevo (PC): teclas rapidas F7 menu, F8 activar/desactivar y siguiente preset
- Menu en 7 pestanas: General, Modos, Tramo, Ayudas, Tecnico, HUD, Log
- Codigo TS2: guarda las ayudas nuevas sin cambiar de formato (los codigos viejos siguen sirviendo)
- "Reinyeccion directa" ahora viene apagada (lo recomendado es la ruta normal del juego)

# v0.3.1
- "Dice salvado y me mata igual": el click salvado ya no se dispara en el borde de la ventana segura; espera un tick de margen si el siguiente tambien es seguro (opcion "Margen de seguridad", 0 a 3)
- Autocalibracion: despues de cada salvada del cubo compara tu trayectoria real con la prediccion (0, 1 o 2 ticks de retraso) y ajusta sola la latencia
- Si dice salvado y moris igual enseguida, ahora lo avisa: "PREDICCION FALLIDA" en el HUD, en el resumen, en las estadisticas y en el log

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
