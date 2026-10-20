# EXP Meter para OBS

## ¿Qué es?

**EXP Meter** es una extensión para **OBS Studio** que mide cuánta experiencia (EXP) gana un jugador por minuto en un MMORPG 2D llamado MapleStory, y muestra ese número en el stream.

## ¿Cómo funciona?

1. El streamer arma su escena en OBS como siempre, con la captura del juego.
2. La extensión toma esa misma captura y lee el número de EXP directamente del HUD del juego (por ejemplo `EXP. 74.660 [28.00%]`). Como el HUD siempre está en el mismo lugar, sabe dónde buscar.
3. Con las lecturas en el tiempo calcula la **EXP por minuto** y la muestra en un pequeño recuadro dentro de la escena, visible para la audiencia.

## Principio clave

La extensión **no toca el juego**: no lee su memoria, no se inyecta en el proceso y no interactúa con su ventana. Solo usa la imagen que OBS ya está capturando para el stream.

Es importante que sea simple de usar para el usuario promedio que no suele ser muy interiorizado dentro del juego.
