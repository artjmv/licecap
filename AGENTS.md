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
- Build: MSBuild VS2022 (toolset `v143`), ver `BUILDING-WINDOWS.md`.
- UI estilo GifCam: Rec graba a un GIF temporal, Save As lo mueve al destino.
