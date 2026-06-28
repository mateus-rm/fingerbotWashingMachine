#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "s3servo.h"
#include <PubSubClient.h>
#include "secrets.h"

#define LED_PIN 21

// --- Servo ---
const int MIN_ANGLE = 75;
const int MAX_ANGLE = 125;
const int SERVO_ACTUATION_TIME_MS = 250;

// --- WiFi / MQTT ---
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
const char *mqtt_server = MQTT_SERVER;
const char *mqtt_user = MQTT_USER;
const char *mqtt_pass = MQTT_PASS;

// --- Topicos ---
const char *TOPIC_ACTIVATE = "casa/maquinaLavar/activate";
const char *TOPIC_FEEDBACK = "casa/maquinaLavar/feedback";
const char *TOPIC_HEARTBEAT = "casa/maquinaLavar/heartbeat";
// OBS: removido o "casa/maquinaLavar/status" com LWT/retain.
// A disponibilidade agora vem 100% do heartbeat + expire_after no HA.
// Se um dia quiser o LWT de volta (offline instantaneo), da pra somar
// os dois — mas ai escolha UM como fonte da verdade no Home Assistant.

// --- Heartbeat ---
const unsigned long HEARTBEAT_INTERVAL = 10000; // 10 s
unsigned long lastHeartbeat = 0;

// --- Reconexao MQTT (nao-bloqueante) ---
const unsigned long MQTT_RETRY_INTERVAL = 2000; // tenta a cada 2 s
unsigned long lastMqttAttempt = 0;

s3servo servo;
WiFiClient espClient;
PubSubClient client(espClient);

// -------------------------------------------------------

void ServoActuate()
{
  servo.write(MIN_ANGLE);
  delay(SERVO_ACTUATION_TIME_MS);
  servo.write(MAX_ANGLE);
}

void callback(char *topic, byte *message, unsigned int length)
{
  String msg;
  for (unsigned int i = 0; i < length; i++)
    msg += (char)message[i];

  Serial.printf("Mensagem recebida [%s]: %s\n", topic, msg.c_str());

  if (String(topic) == TOPIC_ACTIVATE && msg == "ON")
  {
    neopixelWrite(LED_PIN, 0, 0, 255); // Azul = acionando
    ServoActuate();
    client.publish(TOPIC_FEEDBACK, "pressed");
    Serial.println("Servo acionado, feedback enviado.");
    delay(500);
    neopixelWrite(LED_PIN, 0, 5, 0); // Verde = conectado
  }
}

// Tenta conectar UMA vez. Retorna true/false, sem travar.
bool tryConnectMQTT()
{
  neopixelWrite(LED_PIN, 0, 50, 50); // Ciano = tentando MQTT
  Serial.println("Tentando conexao MQTT...");

  bool ok = client.connect("ESP32_Fingerbot", mqtt_user, mqtt_pass);

  if (ok)
  {
    Serial.println("MQTT conectado!");
    client.subscribe(TOPIC_ACTIVATE);
    neopixelWrite(LED_PIN, 0, 5, 0); // Verde = tudo ok
  }
  else
  {
    Serial.printf("Falha MQTT, rc=%d.\n", client.state());
  }
  return ok;
}

void setup()
{
  Serial.begin(230400);
  pinMode(LED_PIN, OUTPUT);
  neopixelWrite(LED_PIN, 50, 0, 0); // Vermelho = iniciando

  pinMode(1, OUTPUT);
  servo.attach(1);
  servo.write(MAX_ANGLE); // posicao de repouso

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // evita quedas de MQTT por power-save do WiFi
  WiFi.begin(ssid, password);

  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    neopixelWrite(LED_PIN, 30, 30, 0); // Amarelo piscando = aguardando WiFi
    delay(250);
    neopixelWrite(LED_PIN, 0, 0, 0);
    delay(250);
    Serial.print(".");
  }
  Serial.printf("\nWiFi conectado! IP: %s\n", WiFi.localIP().toString().c_str());

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop()
{
  // 1) WiFi primeiro, sem bloquear o loop
  if (WiFi.status() != WL_CONNECTED)
  {
    neopixelWrite(LED_PIN, 100, 0, 0); // Vermelho = sem WiFi
    WiFi.reconnect();
    delay(500);
    return;
  }

  // 2) MQTT desconectado: tenta no maximo a cada 2 s, sem while travado
  if (!client.connected())
  {
    unsigned long now = millis();
    if (now - lastMqttAttempt >= MQTT_RETRY_INTERVAL)
    {
      lastMqttAttempt = now;
      tryConnectMQTT();
    }
    return; // sem broker, nao processa o resto deste ciclo
  }

  // 3) Conectado: processa MQTT e manda heartbeat
  client.loop();

  unsigned long now = millis();
  if (now - lastHeartbeat >= HEARTBEAT_INTERVAL)
  {
    lastHeartbeat = now;
    client.publish(TOPIC_HEARTBEAT, "alive"); // sem retain (publish de 2 args)
    Serial.println("Heartbeat enviado.");
  }
}