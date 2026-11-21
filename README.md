# 🏎️ Velocista RC con mando Xbox directo

Firmware C para convertir el **velocista campeón** (Pico 2 W + DRV8833) en un carro RC
manejado con el **mando de Xbox directo por Bluetooth**. Sin navegador, sin celu, sin PC
de por medio: el mando se empareja directo con la Pico 2 W usando **Bluepad32**.

**No tienes que compilar nada en tu PC**: GitHub Actions compila este repo y te deja
el `.uf2` listo en la pestaña **Actions** y en **Releases**.

---

## 🎮 Cómo se usa

| Palanca/botón | Acción |
|---|---|
| Stick izquierdo | Dirección y aceleración |
| **RT** (gatillo derecho) | Acelerar — analógico: suave o a fondo |
| **LT** | Reversa |
| **A** | TURBO (100% siempre + el mando vibra 🔥) |
| **B** | Freno inmediato |
| Cruceta (D-pad) | Manejo a velocidad fija |
| Botón Xbox | Saludo (anillo de LED + rumble) |

Velocidad normal tope: **70%** (para el 100% hay que presionar A/TURBO).

## 🚦 LED de la Pico

- Parpadeo lento: buscando el mando
- Fijo: mando conectado, listo
- Parpadeo rápido: está manejando

## ✅ Pasos para dejarlo rodando

1. **RESPALDA TU FIRMWARE DE COMPETENCIA ANTES DE NADA.** Flashear el .uf2 borra el
   filesystem de la Pico (tu `main.py` del PID se pierde). Abre Thonny con el firmware
   MicroPython actual, abre el `main.py` de la Pico y guárdalo en tu PC como `pid.py`.
   Hazlo AHORA, es 1 minuto y evita un llanto.
2. Ve a la pestaña **Actions** de este repo → última corrida verde → descarga el
   artefacto `velocista-rc-pico2w` (o ve a **Releases** y baja `velocista_rc_pico2w.uf2`).
3. Conecta la Pico 2 W al PC con **BOOTSEL presionado**, suelta, y arrastra el `.uf2`.
4. Conecta la batería del velocista y enciende.
5. Prende el mando con el **botón Xbox**. Si no conecta solo, mantén presionado el
   **botón de emparejar** del mando (el chiquito, al lado del puerto USB) hasta que el
   LED del mando parpadee rápido: la Pico lo agarra sola.
6. RT y a correr. 🏁

## 🔌 Cableado (ya lo tienes así, no hay que cambiar NADA)

- Motor izquierdo: GP14 (avanza) / GP15 (retrocede)
- Motor derecho: GP13 (avanza) / GP12 (retrocede) — volteado, como en tu firmware PID
- Si una rueda girara al revés: en `src/my_platform.c` cambia `INVERTIR_IZQ` o
  `INVERTIR_DER` a 1... pero no debería pasar, es la misma convención del PID.

## 🛡️ Seguridad (ya metida en el firmware)

- **Vigía**: si el mando deja de mandar datos por 600 ms (se apagó, se cayó el
  Bluetooth, se fue del alcance) → **frena solo**.
- `parar()` al arrancar y al desconectar el mando (tu lección del PWM huérfano).
- Suavizado de arranque: nunca de 0 a 100 de golpe (protege los N20).
- Batería con carga (por el sag de siempre).

## 🔧 Configurar / retocar

Todo lo importante está al principio de `src/my_platform.c` (`LIMITE_VEL`,
`STICK_DZ`, `VIGIA_MS`, etc.). Después de editar: commit & push, o ve a
**Actions → Compilar Velocista RC → Run workflow**. En unos minutos tienes
el nuevo `.uf2` en Releases.

## 🏁 Volver al velocista de competencia (1 año de diversion, regreso digno)

1. Descarga el MicroPython_NORMAL para Pico 2 W: <https://micropython.org/download/RPI_PICO2_W/>
2. Flashealo igual (BOOTSEL + arrastrar el `.uf2`).
3. Sube tu `pid.py` con Thonny y renómbralo a `main.py` (o como lo tengas organizado).
4. Ajusta `kp`/`kd`: el oro del Pascual Bravo sigue ahí. 🥇

## 🧱 Qué hay adentro (para curiosos)

- **Bluepad32** (licencia Apache 2, de Ricardo Quesada — la misma librería del robot del
  Robojam) hace todo el trabajo sucio: reverse-engineering del protocolo BT del Xbox,
  emparejamiento, HID parsing. Tu código solo recibe un `uni_gamepad_t` con ejes y
  botones ya limpios.
- **BTstack** corre sobre el chip CYW43439 de la Pico 2 W (el mismo chip del WiFi).
- Este repo solo contiene `src/main.c` y `src/my_platform.c`: el workflow copia esos
  dos archivos encima del ejemplo oficial de Bluepad32 y compila con `-DPICO_BOARD=pico2_w`.

## ⚠️ Nota de compatibilidad

Funciona con mandos **Xbox One S y Xbox Series X|S** (los que tienen Bluetooth).
El Xbox One "gordo" original NO trae Bluetooth y no sirve para esto.
