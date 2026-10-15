# FastIoT V821B & Open-IPCam: Firmware Reverse Engineering & Clean Local Stack

Transformação completa de câmeras IP baseadas no SoC **Allwinner/Cvitek V821B (RISC-V 32-bit RV32GC)** rodando firmware **FastIoT / Tina Linux** em dispositivos **100% locais, open-source e integrados nativamente ao Home Assistant**, sem dependência de nuvem chinesa, aplicativo mobile ou assinaturas pagas.

---

## 🌟 Funcionalidades e Engenharia Reversa Realizada

A partir da desmontagem do firmware original e análise estática via **Ghidra 12.1.4** e **REA (Reverse Engineer Anything)**, foram mapeados todos os subsistemas de hardware:

1. **Controle Motor PTZ (`/dev/motor`)**:
   - Dispositivo de caractere padrão do kernel Linux (`motor.ko`).
   - Ioctls decodificados:
     - `0x80044d01` (`_IOW('M', 1, int)`): Movimentação por passos.
     - `0x80044d02` (`_IOR('M', 2, int)`): Leitura de posição.
     - `0x80044d03` (`_IOW('M', 3, int)`): Velocidade de movimentação (1 a 10).
     - `0x80044d0f` / `0x80044d10`: Início de movimento horizontal/vertical.
     - `0x80044d11` (`_IOW('M', 17, int)`): Parada do motor.
   - Posição persistida em `/userdata/Config/MotorPos.cfg`.

2. **GPIOs, Visao Noturna & Iluminação**:
   - **IRCUT Filter (Solenóide biestável)**: GPIO `118` (ENA) e `119` (ENB).
     - Modo Dia: Filtro de corte infravermelho posicionado (bloqueia IR).
     - Modo Noite: Filtro recolhido (permite luz IR no sensor).
   - **LED Infravermelho (IR LED)**: GPIO `103` (iluminador noturno).
   - **Holofote / Luz Branca (White Spotlight LED)**: GPIO `104` (PWM/digital).
   - **Sirene / Amplificador de Áudio (`SPK_EN`)**: GPIO `79` (habilita amplificador do alto-falante).
   - **Botão de Reset**: GPIO `102`.

3. **Provisionamento Wi-Fi Automático (Modo AP -> Modo Estação)**:
   - **Primeiro Boot / Sem Wi-Fi**: A câmera cria seu próprio ponto de acesso Wi-Fi (**`OpenIPCam-Setup`** com IP `192.168.4.1`) servindo um portal web responsivo.
   - **Busca e Conexão de Redes**: Permite escanear as redes Wi-Fi 2.4GHz locais e selecionar a rede desejada diretamente pela interface web.
   - **Desativação Automática do AP**: Assim que a câmera se conecta com sucesso à rede do usuário e adquire um IP via DHCP, a rede `OpenIPCam-Setup` é **completamente desligada**, tornando a câmera acessível única e exclusivamente pela rede local do usuário.
   - **Reset de Emergência**: Caso o Wi-Fi mude ou a senha esteja incorreta, segurar o botão físico de reset (GPIO `102`) por 3 segundos ou acessar `/api/wifi/reset` restaura instantaneamente o ponto de acesso `OpenIPCam-Setup`.

4. **Novo App Substituto Open Source (`open-ipcam`)**:
   - Binário C nativo e ultraleve (~46 KB dinâmico / 122 KB estático) compilado com `riscv32-linux-musl-gcc`.
   - **Zero Nuvem**: Não faz conexões externas para `sightcloud.cn` ou servidores desconhecidos.
   - **Servidor RTSP Integrado (RFC 2326)** na porta `554` (suporta RTP UDP e TCP Interleaved para Home Assistant/go2rtc).
   - **Painel Web Responsivo** na porta `8080` com configuração de Wi-Fi, teclado direcional PTZ, alternador de visão noturna (Dia / Noite IR / Noite Colorida), gatilho de sirene e métricas do sistema.
   - **REST API Completa**: Endpoints JSON para Home Assistant (`/api/status`, `/api/ptz`, `/api/nightvision`, `/api/siren`, `/api/wifi/scan`, `/api/wifi/setup`, `/api/wifi/reset`).
   - **Home Assistant MQTT Auto-Discovery**: Descoberta automática de câmera, botões PTZ, interruptores de iluminação e sirene no Home Assistant via MQTT.

---

## 🏗️ Estrutura do Repositório

```text
├── sdcard/
│   └── FastIoT_factoryTest/      <-- Pasta pronta para copiar para a raiz do MicroSD
│       ├── factory.sh            # Script de inicialização automática no boot (root hook)
│       ├── open-ipcam            # Novo daemon open-source (RISC-V 32-bit dinâmico)
│       ├── open-ipcam-static     # Versão estática autônoma (RISC-V 32-bit estático)
│       ├── open-ipcam.conf       # Arquivo de configuração (Portas, MQTT, PTZ, Credenciais)
│       ├── block_wan.sh          # Script de bloqueio de saída para a Internet (LAN-only)
│       ├── ipcam_launcher.sh     # Wrapper de execução alternativa
│       ├── ptz_bridge.so         # Hook de interoperabilidade com o app antigo
│       └── busybox               # BusyBox estático RISC-V 32-bit com todas as ferramentas
├── src/
│   ├── open-ipcam/               # Código-fonte completo do novo daemon open-source
│   │   ├── include/
│   │   │   ├── common.h          # Tipos e estruturas compartilhadas
│   │   │   ├── motor_ctrl.h      # Driver e ioctls do motor PTZ
│   │   │   ├── gpio_ctrl.h       # Controle dos GPIOs (IRCUT, IR LED, Spotlight, Sirene)
│   │   │   ├── rtsp_server.h     # Servidor RTSP 1.0 (RFC 2326)
│   │   │   ├── http_server.h     # Servidor Web e REST API
│   │   │   └── ha_mqtt.h         # Cliente MQTT 3.1.1 para Auto-Discovery no Home Assistant
│   │   ├── motor_ctrl.c          # Implementação do controle do motor
│   │   ├── gpio_ctrl.c           # Implementação dos GPIOs e filtros
│   │   ├── rtsp_server.c         # Implementação do streaming RTSP
│   │   ├── http_server.c         # Implementação da Web UI e REST API
│   │   ├── ha_mqtt.c             # Implementação do protocolo MQTT
│   │   ├── main.c                # Orquestrador principal
│   │   ├── build.sh              # Script de compilação cruzada (WSL / Linux)
│   │   └── Makefile              # Makefile padrão
│   ├── ptz_bridge.c              # Código fonte do bridge antigo
│   └── cam_server.c              # Código fonte do servidor Web antigo
├── home-assistant/
│   ├── configuration.yaml        # Configuração dos comandos REST e sensores no HA
│   ├── dashboard-cards.yaml      # Cards Lovelace (Nativo e WebRTC) com D-pad PTZ
│   └── go2rtc.yaml               # Configuração do go2rtc (WebRTC Ultra-Low Latency)
├── tools/
│   ├── camera_cmd.py             # Utilitário para executar comandos via Telnet
│   └── transfer_nc.py            # Utilitário de transferência via Netcat
└── README.md
```

---

## 🚀 Como Fazer o Deploy (Passo a Passo)

### 1. Preparar o Cartão MicroSD
1. Formate um cartão MicroSD (até 64GB) em **FAT32**.
2. Copie a pasta `FastIoT_factoryTest` (localizada em `sdcard/`) diretamente para a **raiz do cartão MicroSD**:
   ```text
   E:\FastIoT_factoryTest\
       ├── factory.sh
       ├── open-ipcam-static
       ├── open-ipcam.conf
       ├── block_wan.sh
       └── busybox
   ```
3. *(Opcional)* Edite o arquivo `open-ipcam.conf` para habilitar o MQTT do seu Home Assistant:
   ```ini
   mqtt_enabled=true
   mqtt_broker=192.168.15.100
   mqtt_port=1883
   ```

### 2. Iniciar a Câmera
1. Com a câmera desligada, insira o cartão MicroSD.
2. Ligue a câmera na tomada.
3. A câmera executará automaticamente o script `factory.sh` da raiz do SD Card no boot.

---

## 🕹️ Portas e Serviços Disponíveis

| Serviço | Porta / Protocolo | URL / Como Usar |
| :--- | :--- | :--- |
| **Painel Web Open-IPCam** | TCP `8080` (HTTP) | `http://<IP_DA_CAMERA>:8080` |
| **Stream RTSP Principal (1080P)** | TCP `554` (RTSP) | `rtsp://<IP_DA_CAMERA>:554/live/main` |
| **Stream RTSP Secundário (360P)** | TCP `554` (RTSP) | `rtsp://<IP_DA_CAMERA>:554/live/sub` |
| **Terminal Root** | TCP `23` (Telnet) | `telnet <IP_DA_CAMERA> 23` |
| **API REST de Status** | TCP `8080` (HTTP) | `GET http://<IP_DA_CAMERA>:8080/api/status` |
| **API REST PTZ** | TCP `8080` (HTTP) | `POST http://<IP_DA_CAMERA>:8080/api/ptz?dir=<u\|d\|l\|r\|s\|h>` |
| **API REST Visão Noturna** | TCP `8080` (HTTP) | `POST http://<IP_DA_CAMERA>:8080/api/nightvision?mode=<day\|ir\|color\|auto>` |
| **API REST Sirene** | TCP `8080` (HTTP) | `POST http://<IP_DA_CAMERA>:8080/api/siren?state=<on\|off>` |
| **API Busca Wi-Fi** | TCP `8080` (HTTP) | `GET http://<IP_DA_CAMERA>:8080/api/wifi/scan` |
| **API Setup Wi-Fi** | TCP `8080` (HTTP) | `POST http://<IP_DA_CAMERA>:8080/api/wifi/setup?ssid=<SSID>&password=<PASS>` |
| **API Reset Wi-Fi** | TCP `8080` (HTTP) | `POST http://<IP_DA_CAMERA>:8080/api/wifi/reset` |

---

## 🏠 Integração com o Home Assistant

### 1. Comandos REST e Entidades (`configuration.yaml`)
Adicione no seu `configuration.yaml` do Home Assistant os blocos contidos em [`home-assistant/configuration.yaml`](home-assistant/configuration.yaml).

### 2. Auto-Discovery via MQTT
Se o parâmetro `mqtt_enabled=true` for ativado no `open-ipcam.conf`, o Home Assistant detectará automaticamente:
- Entidade de Câmera (`camera.open_ipcam`)
- Botões PTZ (`button.open_ipcam_ptz_up`, `down`, `left`, `right`, `home`)
- Interruptores de Luz / IR (`switch.open_ipcam_ir_led`, `light.open_ipcam_spotlight`)
- Sirene de Alarme (`siren.open_ipcam_siren`)

---

## 🔨 Como Compilar a Partir do Código Fonte

Para compilar o binário RISC-V 32-bit no WSL2 ou Linux com o cross-compiler `riscv32-linux-musl-gcc`:

```bash
cd src/open-ipcam
chmod +x build.sh
./build.sh
```

Os binários compilados (`open-ipcam` dinâmico e `open-ipcam-static` estático) serão gerados e copiados automaticamente para a pasta `sdcard/FastIoT_factoryTest/`.
