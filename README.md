# Fingerbot — Máquina de Lavar

Automação residencial que torna uma máquina de lavar comum acionável
remotamente. Um ESP32 com servo motor atua como *Fingerbot*: pressiona
fisicamente o botão de ligar da máquina sob comando, sem qualquer modificação
invasiva no eletrodoméstico.

O dispositivo se comunica por MQTT com o Home Assistant, que oferece acionamento
manual, agendamento por horário e monitoramento de disponibilidade e feedback em
tempo real.

## Arquitetura

O firmware roda em um ESP32-S3 e publica/assina tópicos em um broker Mosquitto.
O Home Assistant consome esses tópicos e expõe as entidades de controle e
monitoramento. A stack de servidor roda em um servidor local Linux.

- **Firmware:** ESP32-S3, PlatformIO, framework Arduino
- **Atuador:** servo motor (biblioteca `s3servo`)
- **Broker MQTT:** Mosquitto em Docker
- **Orquestração:** Home Assistant Container (sem Supervisor, configurado via YAML)
- **Host:** servidor local Linux

## Hardware

O servo é controlado pelo GPIO 1 e o LED RGB on-board pelo GPIO 21. Os
parâmetros de atuação estão definidos em [src/main.cpp](src/main.cpp):

- Ângulo de repouso: 125°
- Ângulo de pressão: 75°
- Tempo de atuação: 250 ms (pressiona e solta)

Estes ângulos são específicos da montagem e do botão da máquina usada no
projeto. Eles **devem ser calibrados** para o seu eletrodoméstico e para o
posicionamento físico do servo, de modo que o ângulo de pressão acione o botão
de forma confiável e o ângulo de repouso o libere por completo. Ajuste os
valores `MIN_ANGLE` e `MAX_ANGLE` em [src/main.cpp](src/main.cpp) conforme
necessário.

## Estados do LED RGB

O LED on-board sinaliza o estado do dispositivo:

- **Vermelho:** iniciando ou sem WiFi
- **Amarelo (piscando):** conectando ao WiFi
- **Ciano:** tentando conectar ao MQTT
- **Azul:** servo acionando
- **Verde:** operacional (WiFi e MQTT conectados)

## Tópicos MQTT

| Tópico | Direção | Payload | Descrição |
| --- | --- | --- | --- |
| `casa/maquinaLavar/activate` | HA → ESP | `ON` | Comando para acionar o servo |
| `casa/maquinaLavar/feedback` | ESP → HA | `pressed` | Confirmação de que o botão foi pressionado |
| `casa/maquinaLavar/heartbeat` | ESP → HA | `alive` | Sinal de vida a cada 10 s, sem retain |

## Home Assistant

### Entidades MQTT

- `button` — aciona a máquina via `activate`
- `binary_sensor` (feedback) — recebe `feedback` com `off_delay: 5`
- `binary_sensor` (disponibilidade) — online/offline via `heartbeat` com
  `expire_after: 30`, funcionando como watchdog por timeout (sem LWT/retain)

### Helpers

- `input_datetime` — horário agendado e último acionamento
- `input_boolean` — liga e desliga o agendamento

### Automações

- **Disparo no horário agendado.** Quando o horário definido em
  `maquina_horario_agendado` é atingido e o agendamento está ativo, a automação
  publica `ON` em `activate`, desliga o `input_boolean` (disparo único) e cria
  uma notificação de confirmação.
- **Registro do último acionamento.** Quando o `binary_sensor` de feedback vai
  para `on`, a automação grava o horário atual em `maquina_ultimo_acionamento`.

### Exemplo de configuração

```yaml
mqtt:
  button:
    - name: "Acionar Máquina de Lavar"
      command_topic: "casa/maquinaLavar/activate"
      payload_press: "ON"

  binary_sensor:
    - name: "Máquina Feedback"
      state_topic: "casa/maquinaLavar/feedback"
      payload_on: "pressed"
      payload_off: "idle"
      off_delay: 5

    # Disponibilidade via watchdog: sem heartbeat por 30s, fica offline
    - name: "Máquina ESP Online"
      state_topic: "casa/maquinaLavar/heartbeat"
      payload_on: "alive"
      device_class: connectivity
      expire_after: 30
```

Arquivos de referência do Home Assistant estão em [.outros/yaml/](.outros/yaml/).

### Dashboard

O dashboard (Lovelace, via HACS) reúne em um card único — *Máquina de Lavar* — o
botão de acionamento, o indicador de feedback, o status de conexão, o seletor de
horário e o toggle de agendamento. A iluminação fica em seção separada.

## Decisões de arquitetura

- **Disponibilidade por watchdog.** A presença do dispositivo é detectada pelo
  `heartbeat` periódico combinado com `expire_after` no Home Assistant, e não por
  uma mensagem LWT retida. O sinal de vida é a única fonte da verdade.
- **Reconexão MQTT não-bloqueante.** O `loop()` nunca trava em `while`; as
  tentativas de reconexão ocorrem no máximo a cada 2 s, mantendo o dispositivo
  responsivo.
- **`WiFi.setSleep(false)`.** Desativa o power-save do WiFi para evitar quedas
  silenciosas da conexão com o broker.
- **Home Assistant Container.** Sem Supervisor e sem Add-ons; toda a configuração
  é feita via YAML.

## Compilação e gravação

Pré-requisito: [PlatformIO](https://platformio.org/) (CLI ou extensão do VS Code).

1. Copie o modelo de credenciais e preencha com seus dados:

   ```bash
   cp include/secrets.example.h include/secrets.h
   ```

   ```cpp
   #define WIFI_SSID     "SEU_SSID"
   #define WIFI_PASSWORD "SUA_SENHA_WIFI"
   #define MQTT_SERVER   "192.168.1.50"   // IP do broker
   #define MQTT_USER     "SEU_USUARIO_MQTT"
   #define MQTT_PASS     "SUA_SENHA_MQTT"
   ```

   O arquivo `include/secrets.h` está no `.gitignore` e não é versionado.

2. Compile e grave no ESP32-S3:

   ```bash
   pio run --target upload
   ```

3. Acompanhe o monitor serial (230400 baud):

   ```bash
   pio device monitor
   ```

## Estrutura do projeto

- `src/main.cpp` — firmware do Fingerbot
- `include/secrets.example.h` — modelo de credenciais (versionado)
- `include/secrets.h` — credenciais reais (ignorado pelo git)
- `lib/ESP32S3servo-main/` — biblioteca do servo para ESP32-S3
- `platformio.ini` — configuração do PlatformIO
- `.outros/yaml/` — configurações de referência do Home Assistant

## Dependências

- [PubSubClient](https://github.com/knolleary/pubsubclient) `@^2.8` — cliente MQTT
- `s3servo` — controle de servo no ESP32-S3 (incluída em `lib/`)
