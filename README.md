# Linux System Manager

Monitor de sistema, hardware e processos para Linux, escrito em C puro. Lê `/proc`,
`/sys` e syscalls diretamente do kernel — não interpreta a saída de `top`, `free`,
`lsblk` ou `sensors`.

Entrega três coisas: uma interface de terminal (TUI), um snapshot único via CLI e um
daemon opcional que outros clientes consultam por socket Unix.

## Finalidade

Acompanhar o estado da máquina — CPU, memória, discos, rede, sensores, GPU e
processos — sem shell out para outras ferramentas e sem depender delas para nada.
Útil para diagnóstico rápido, monitoramento contínuo em segundo plano (via daemon)
e gerenciamento de processos direto da linha de comando.

## Como funciona

- **System info** — hostname, kernel, distro, CPU, RAM, uptime
- **CPU** — uso total e por núcleo, load average, frequência por núcleo
- **Memória** — RAM e swap com divisão correta usada/disponível (não o "total menos
  free" ingênuo)
- **Processos** — listar, ordenar por CPU/memória, `kill`/`SIGKILL`/renice pela TUI
- **Disco** — capacidade/uso dos filesystems montados e vazão read/write por dispositivo
- **Rede** — tráfego e estado por interface
- **Sensores** — temperatura, fan, voltagem e potência onde o kernel expõe
- **GPU** — NVIDIA (via NVML), AMD (via sysfs amdgpu) e identificação básica de outros
  fabricantes
- **Daemon + IPC** — `linuxmngd` amostra tudo em segundo plano e serve por socket Unix,
  sem re-escaneamento de `/proc` a cada pedido

Coletores, núcleo e UI são separados: a TUI não sabe como `/proc/stat` é formatado, e
o módulo de CPU não sabe que a ncurses existe. Dado ausente vira "N/A" em vez de
crashar.

## Como rodar

```sh
make               # builda bin/linux-system-manager e bin/linux-system-managerd
make install       # instala em ~/.local/bin/linuxmng e linuxmngd (sem root)
```

Depois de instalado:

```sh
linuxmng           # abre a TUI
linuxmng --cli     # snapshot único em texto
linuxmngd &        # inicia o daemon em segundo plano
linuxmng --daemon  # consulta o daemon via IPC
```

`make install` usa `~/.local/bin` por padrão; `make install PREFIX=/usr/local` faz
instalação de sistema (precisa `sudo`). Saída redirecionada/pipeada
(`linuxmng | less`, cron, CI) cai automaticamente no snapshot de texto.

## Requisitos

Linux x86_64, GCC ou Clang + GNU Make, `ncursesw` (para a TUI). Nada mais é preciso
para buildar: o backend NVIDIA carrega `libnvidia-ml.so` em runtime se existir —
nenhuma dependência de build.

## Controles da TUI

`1`-`9` ou `Tab` trocam de visão, `+`/`-` mudam a taxa de atualização, `q` sai. Na
visão de Processos: setas selecionam, `c`/`m` ordenam por CPU/memória, `k`/`x` enviam
SIGTERM/SIGKILL (com confirmação), `[`/`]` fazem renice.

## Daemon como serviço

```sh
mkdir -p ~/.config/systemd/user
cp config/linux-system-managerd.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now linux-system-managerd
```

Roda como o próprio usuário (sem root) e com sandbox (`ProtectSystem=strict`, sem rede).

## Documentação

- `docs/ARCHITECTURE.md` — layout de módulos, fluxo de dados, decisões de design
- `docs/BUILD.md` — modos de build e flags do compilador
- `docs/DEVELOPMENT.md` — como adicionar um novo módulo coletor
- `docs/SECURITY.md` — modelo de privilégio e sandboxing
- `docs/TROUBLESHOOTING.md` — problemas comuns

## Licença

MIT — ver `LICENSE`.