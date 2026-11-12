# Reglas de trabajo en este repositorio

Este repo es un **fork personal** de LICEcap (`justinfrankel/licecap`).
Estas reglas aplican a cualquier persona/agente que haga commits aquí.

## 1. Solo se trabaja en el fork

| Remoto | URL | Uso |
|---|---|---|
| `origin` | `https://github.com/artjmv/licecap.git` | **único destino de push** |
| `upstream` | `https://github.com/justinfrankel/licecap.git` | solo lectura (`pushurl = no_push`) |

- Todo el trabajo se publica en `origin` (el fork). Nada va al repo original:
  ni push, ni PR, ni force-push. El `no_push` de `upstream` está para que un
  push accidental falle en seco.
- No cambiar los remotos ni el `pushurl` sin pedirlo explícitamente.
- Nunca reescribir historia ya publicada en `origin/main` (`--force`,
  `filter-branch`, `rebase` de commits pusheados).
- Después de cada push, verificar de verdad:
  `git rev-parse HEAD` == `git ls-remote --heads origin main`.
- `upstream` se puede usar solo para traer novedades (`git fetch upstream`).

## 2. Antes de commitear: nada crítico

Revisar siempre `git status` y `git diff` antes de preparar el commit.

- **Stage explícito por ruta.** No usar `git add -A`, `git add .` ni
  `git commit -a`: son la vía típica por la que se cuela basura o secretos.
- **Nunca** commitear: credenciales, tokens, API keys, `.env`, certificados
  (`.pfx`, `.pem`, `.key`), keystores, `id_rsa`, cookies, volcados de config
  con datos privados.
- Tampoco datos personales: rutas con nombre de usuario (`C:\Users\<usuario>`,
  `OneDrive`), correos, IPs/hostnames internos, nombres de clientes, capturas
  de pantalla con información privada.
- Chequeo rápido sobre lo que está *staged* antes de dar el commit por bueno:

  ```
  git diff --cached | grep -InE 'passw|secret|token|api[_-]?key|BEGIN .*PRIVATE KEY|C:\\\\Users|OneDrive|Authorization:'
  ```

  Si aparece algo, quitarlo del stage (`git restore --staged <archivo>`), no
  "arreglarlo" en el mensaje del commit.
- Si el dato ya está publicado y hay que sacarlo, avisar antes de reescribir
  historia: en un fork público el contenido puede estar cacheado.

## 3. Basura: fuera del repo, y el `.gitignore` se mantiene

- Ya ignorado: `dist/`, `_uicheck/` (capturas), `licecap/licecap/{x64,Win32,Debug_new,Release_new,licecap}/`,
  `*.user`, `*.suo`. Ver `.gitignore`.
- Si aparece un artefacto nuevo (build, intermedios, logs, capturas), se
  **agrega a `.gitignore`** en vez de commitearlo.
- Sospechosos habituales: `*.exe`, `*.dll`, `*.pdb`, `*.obj`, `*.lib`,
  `*.ilk`, `*.tlog`, `*.log`, `.vs/`, `ipch/`, `build/`, `*.zip`.
- Revisar tamaño antes de commitear (evitar binarios y paquetes grandes):

  ```
  git ls-files -z | xargs -0 du -b | sort -rn | head
  ```
- Los scripts de verificación (`_uicheck*.ps1`, `run-licecap.bat`) **sí** se
  versionan; sus salidas (`_uicheck/*.png`) **no**.
- Commits en unidades lógicas y con mensaje que explique el porqué; el código
  vendido en `WDL/` es de terceros, no se reformatea.

## 4. Estado del proyecto (para orientarse)

- Solo la GUI de Windows está adaptada: `licecap/licecap_ui.cpp`,
  `licecap/licecap.rc`, `licecap/resource.h`, `licecap/licecap/licecap.vcxproj`.
- La UI estilo GifCam está partida en módulos (interfaz en `licecap/licecap_ui.h`):
  `ui_theme.cpp` (paleta, DPI, modo oscuro), `ui_draw.cpp` (geometría, región,
  pintado de la ventana), `ui_bookmarks.cpp` (carpetas favoritas + panel),
  `ui_presets.cpp` (tamaños predefinidos + diálogo de Settings),
  `ui_gifedit.cpp` (lee el GIF grabado, compone frames con transparencia y
  disposal, y escribe un rango o la concatenación de varios) y `ui_editor.cpp`
  (editor de timeline: preview, play/pause, varios tramos A/B y Apply).
  `licecap_ui.cpp` se queda con el motor de captura, el flujo grabar/guardar,
  `liceCapMainProc` y `WinMain`. Si se añade un `.cpp` nuevo hay que darlo de
  alta en `licecap.vcxproj` **y** en `licecap.vcxproj.filters`.
- Build: MSBuild VS2022 (toolset `v143`), ver `BUILDING-WINDOWS.md`.
- UI estilo GifCam: Rec graba a un GIF temporal, Save As lo mueve al destino.
  La flechita de `Save` es la vía a las carpetas favoritas: con una grabación
  pendiente y algún bookmark abre la lista como desplegable debajo del botón y
  un clic en una fila guarda ahí (`ui_bookmarks.cpp`); sin nada pendiente abre
  el menú de comandos.
  Debajo de `Save` hay además una fila desplegable fija con el bookmark
  **último usado** (`IDC_BOOKMARKDROP`): al pulsarla abre esa misma lista con
  esa fila ya seleccionada y visible; con grabación pendiente un clic guarda
  ahí, y sin pendiente sólo fija el destino (el ini lo recuerda en
  `[bookmarks] last`).
  El botón Edit abre el editor de timeline sobre esa grabación pendiente; su
  flecha despliega el menú (`Edit Last Recording` / `Open GIF to Edit...`,
  también Ctrl+O) y, sin nada grabado, el propio botón abre directamente el
  selector de archivos. Abrir un GIF del disco (o pasarle la ruta como
  argumento: `licecap.exe anim.gif`) lo edita **a través de una copia**
  temporal en `%TEMP%\LICEcap_edit_*.gif`, que pasa a ser la grabación
  pendiente: Apply reescribe la copia y el original nunca se toca; `Save As`
  propone `<nombre>-edit.gif` en la carpeta del original. Cuando lo pendiente
  viene de un GIF abierto, el título dice `<archivo> (opened) - not saved` y el
  bloque de estado dice `OPENED`.
  Admite varios tramos conservados, que Apply escribe concatenados en orden
  de timeline (`[A,B] + [C,D] + ...`, como mucho 12 tramos):
  Space = play/pausa (reproduce sólo lo conservado), flechas = paso de frame,
  A/B = fijar inicio/fin del tramo seleccionado en el playhead,
  N o Insert = "+" (tramo nuevo en los frames descartados a partir del
  playhead, o partir el tramo actual si no queda hueco), Supr/Backspace o "-"
  = quitar el tramo seleccionado (con un solo tramo, vuelve a "conservar
  todo"), Tab = pasar al siguiente tramo, Enter = Apply, Esc = cerrar sin
  tocar el archivo. Los handles se arrastran con el ratón; al chocar con un
  tramo vecino lo empujan y un tramo que se queda sin frames desaparece.
  Sólo se puede editar mientras el GIF siga pendiente (grabación recién hecha
  o GIF abierto sin guardar);
  `_uicheck8.ps1` verifica el caso de un tramo y `_uicheck9.ps1` el de varios
  (frames y orden del GIF resultante, comparados contra el original), y
  `_uicheck13.ps1` el de abrir un GIF ajeno (CLI, menú, Ctrl+O, clic real en la
  etiqueta y en la flecha del botón): recorte sobre la copia, `Save As`, el
  original intacto por md5, y los avisos de "no es un GIF" y de ruta
  inexistente sin dejar nada pendiente.
- Abrir Edit, Apply y guardar usan `Busy_Run`: trabajo en `_beginthreadex` y
  ventana modal central con barra (porcentaje/frames o indeterminada y tiempo).
  La UI despacha mensajes normalmente; no se eliminan mensajes de la cola.
  El propietario está deshabilitado y sus timers/comandos/cierre se ignoran
  durante la tarea. El callback del worker sólo escribe contadores atómicos.
  Apply carga su propio modelo GIF: nunca comparte el canvas mutable con el
  preview que sigue pintándose. No hay cancelación ni cierre a mitad de tarea.
  Si falla reemplazar el GIF se conserva el original. `_uicheck12.ps1` prueba
  más de mil frames y respuesta WM_NULL del diálogo mientras avanza el progreso;
  `_uicheck8.ps1` verifica recorte.
- El target de macOS (`licecap.xcodeproj`) no está mantenido en este fork: si se
  reactiva, hay que añadirle los seis `ui_*.cpp` y `licecap_ui.h`.
