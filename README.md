# FastIoT V821B IP Camera Local Hack & PTZ Control

Transformação completa de câmeras IP chinesas baseadas no SoC **Allwinner/Cvitek V821B (RISC-V 32-bit)** rodando firmware **FastIoT** em dispositivos **100% locais, privados e integrados ao Home Assistant**, sem dependência de nuvem chinesa, aplicativo de terceiros ou assinaturas pagas.

---

## 🌟 Funcionalidades Desbloqueadas

- **Acesso Root Total (Telnet):** Shell `/bin/ash` direto na porta `23` sem senha.
- **Painel Web Gráfico Local:** Interface responsiva na porta `8080` com teclado direcional PTZ.
- **Controle Motor PTZ via API HTTP:** Endpoints locais para movimentação (Cima, Baixo, Esquerda, Direita, Parar).
- **Streaming de Vídeo & Áudio (RTSP):** Fluxo H.264 + áudio G.711A na porta `554` compatível com VLC, go2rtc, Frigate, Scrypted e Home Assistant.
- **Bloqueio Total de Nuvem (Privacidade 100% Local):** Isolamento automático de rede que impede envio de dados para servidores externos (China, AWS, DDNS), mantendo funcionamento total na rede local (LAN).
- **Zero Modificações na Memória Flash:** Todo o sistema roda a partir de um cartão MicroSD (FAT32), preservando o firmware original intacto.

---

## 🏗️ Estrutura do Repositório

```text
├── sdcard/
│   └── FastIoT_factoryTest/      <-- Pasta pronta para copiar para a raiz do MicroSD
│       ├── factory.sh            # Script de inicialização automática (root hook)
│       ├── block_wan.sh          # Script de bloqueio de saída para a Internet (LAN-only)
│       ├── ipcam_launcher.sh     # Wrapper com injeção LD_PRELOAD
│       ├── ptz_bridge.so         # Biblioteca injetada no ipcam (RISC-V 32-bit)
│       ├── cam_server            # Servidor Web HTTP 8080 + Telnet backup 2323
│       └── busybox               # BusyBox estático RISC-V 32-bit com todas as ferramentas
├── src/
│   ├── ptz_bridge.c              # Código fonte da ponte PTZ (LD_PRELOAD)
│   ├── cam_server.c              # Código fonte do servidor Web e Telnet
│   ├── Makefile                  # Script de compilação
│   └── build.sh                  # Script de automação de build com riscv32-musl
├── home-assistant/
│   ├── configuration.yaml        # Configuração dos comandos REST no Home Assistant
│   ├── dashboard-cards.yaml      # Exemplos de cards Lovelace (Nativo e WebRTC)
│   └── go2rtc.yaml               # Exemplo de configuração do go2rtc
├── tools/
│   ├── camera_cmd.py             # Utilitário para executar comandos via Telnet
│   └── transfer_nc.py            # Utilitário ultrarrápido de transferência via Netcat
└── README.md
```

---

## 🚀 Como Instalar (Passo a Passo Rápido)

### 1. Preparar o Cartão MicroSD
1. Formate um cartão MicroSD (até 32GB ou 64GB) em **FAT32**.
2. Copie a pasta `FastIoT_factoryTest` (localizada dentro de `sdcard/`) diretamente para a **raiz do cartão MicroSD**:
   ```text
   E:\FastIoT_factoryTest\
       ├── factory.sh
       ├── block_wan.sh
       ├── ipcam_launcher.sh
       ├── ptz_bridge.so
       ├── cam_server
       └── busybox
   ```

### 2. Iniciar a Câmera
1. Com a câmera desligada da tomada, insira o cartão MicroSD.
2. Ligue a câmera na tomada.
3. Aguarde cerca de 15 a 20 segundos para a inicialização completa.

---

## 🕹️ Portas e Serviços Disponíveis

| Serviço | Porta / Protocolo | URL / Como Usar |
| :--- | :--- | :--- |
| **Painel Web PTZ** | TCP `8080` (HTTP) | `http://<IP_DA_CAMERA>:8080` |
| **Stream RTSP** | TCP `554` (RTSP) | `rtsp://admin:Ztac@2021@<IP_DA_CAMERA>:554/live/camid_0/main` |
| **Terminal Root** | TCP `23` (Telnet) | `telnet <IP_DA_CAMERA> 23` |
| **API HTTP PTZ** | TCP `8080` (HTTP) | `http://<IP_DA_CAMERA>:8080/cmd?dir=<u\|d\|l\|r\|s>` |

### Comandos da API HTTP:
- **Cima (Up):** `GET /cmd?dir=u`
- **Baixo (Down):** `GET /cmd?dir=d`
- **Esquerda (Left):** `GET /cmd?dir=l`
- **Direita (Right):** `GET /cmd?dir=r`
- **Parar (Stop):** `GET /cmd?dir=s`

---

## 🏠 Integração com Home Assistant

### 1. Comandos de Movimento (`configuration.yaml`)
Adicione no seu `configuration.yaml`:

```yaml
rest_command:
  camera_ptz:
    url: "http://<IP_DA_CAMERA>:8080/cmd?dir={{ direction }}"
    method: GET

  camera_ptz_up:
    url: "http://<IP_DA_CAMERA>:8080/cmd?dir=u"
    method: GET

  camera_ptz_down:
    url: "http://<IP_DA_CAMERA>:8080/cmd?dir=d"
    method: GET

  camera_ptz_left:
    url: "http://<IP_DA_CAMERA>:8080/cmd?dir=l"
    method: GET

  camera_ptz_right:
    url: "http://<IP_DA_CAMERA>:8080/cmd?dir=r"
    method: GET

  camera_ptz_stop:
    url: "http://<IP_DA_CAMERA>:8080/cmd?dir=s"
    method: GET
```

### 2. Vídeo RTSP
Vá em **Configurações** ➔ **Dispositivos e Serviços** ➔ **Adicionar Integração** ➔ **Generic Camera** e use:
```text
rtsp://admin:Ztac@2021@<IP_DA_CAMERA>:554/live/camid_0/main
```

### 3. Card do Dashboard (Lovelace Nativo)
Adicione um card do tipo **Manual** no seu painel:

```yaml
type: vertical-stack
cards:
  - type: picture-entity
    entity: camera.generic_camera
    name: Câmera IP
    camera_view: live

  - type: grid
    columns: 3
    square: false
    cards:
      - type: button
        icon: mdi:arrow-top-left
        show_name: false
        tap_action: { action: none }

      - type: button
        icon: mdi:arrow-up-bold
        show_name: false
        tap_action:
          action: call-service
          service: rest_command.camera_ptz
          data: { direction: u }

      - type: button
        icon: mdi:arrow-top-right
        show_name: false
        tap_action: { action: none }

      - type: button
        icon: mdi:arrow-left-bold
        show_name: false
        tap_action:
          action: call-service
          service: rest_command.camera_ptz
          data: { direction: l }

      - type: button
        icon: mdi:stop-circle-outline
        show_name: false
        tap_action:
          action: call-service
          service: rest_command.camera_ptz
          data: { direction: s }

      - type: button
        icon: mdi:arrow-right-bold
        show_name: false
        tap_action:
          action: call-service
          service: rest_command.camera_ptz
          data: { direction: r }

      - type: button
        icon: mdi:arrow-bottom-left
        show_name: false
        tap_action: { action: none }

      - type: button
        icon: mdi:arrow-down-bold
        show_name: false
        tap_action:
          action: call-service
          service: rest_command.camera_ptz
          data: { direction: d }

      - type: button
        icon: mdi:arrow-bottom-right
        show_name: false
        tap_action: { action: none }
```

---

## 🔬 Detalhes Técnicos da Engenharia Reversa

### 1. Vetor de Execução Inicial (Root Hook)
O script `/system/start.sh` original do firmware possui o seguinte trecho:
```sh
if [ -f /media/FastIoT_factoryTest/factory.sh ]; then
    /media/FastIoT_factoryTest/factory.sh &
fi
```
Ao inserir o MicroSD contendo `/media/FastIoT_factoryTest/factory.sh`, o script é executado automaticamente com privilégios de `root`.

### 2. O Desafio do Driver do Motor (`/dev/motor`)
O módulo de kernel `/system/ko/motor.ko` impõe acesso exclusivo (`open()` retorna `-EBUSY`). Como o binário proprietário `/system/bin/ipcam` mantém o descritor de arquivo aberto o tempo todo, qualquer utilitário externo (`motor_test`) falha ao tentar abrir o dispositivo.

### 3. A Solução: Injeção Dinâmica via `LD_PRELOAD`
Para controlar o motor sem conflito de driver:
1. Usamos um bind mount temporário em `/system/bin/ipcam` durante o boot para forçar o carregamento de `ptz_bridge.so` via `LD_PRELOAD`.
2. A biblioteca `ptz_bridge.so` sobe uma thread que ouve comandos UDP na porta `127.0.0.1:8888`.
3. Ao receber um comando, ela chama diretamente a função interna do processo:
   ```c
   int FastIot_PTZControl(int ptzid, int type);
   ```
4. **Ponto de entrada exato:** A função está localizada no endereço virtual **`0x3aa0e`**. Ela utiliza o frame saver do GCC para RISC-V (`__riscv_save_3`). Chamar o corpo interno sem executar esse prólogo causava corrupção de pilha e `SIGSEGV`; ao apontar para `0x3aa0e`, a chamada é 100% estável e idêntica aos comandos do aplicativo original.

### 4. Mapeamento de Direções PTZ
- `1`: Esquerda (Left)
- `2`: Direita (Right)
- `3`: Baixo (Down)
- `4`: Cima (Up)
- `0`: Parar (Stop)

---

## 🛠️ Como Recompilar os Binários

Para compilar a partir do código fonte em ambiente Linux / WSL2:

1. Baixe o toolchain Musl para RISC-V 32-bit:
   - Toolchain: `riscv32-linux-musl-cross` (GCC 11.2+)
2. Execute o script de build:
   ```bash
   cd src/
   chmod +x build.sh
   ./build.sh
   ```

---

## 🛡️ Isenção de Responsabilidade
Este projeto foi desenvolvido estritamente para fins educacionais, de pesquisa em segurança da informação e interoperabilidade de hardware próprio sob as diretrizes de direito de reparo e posse do consumidor.
