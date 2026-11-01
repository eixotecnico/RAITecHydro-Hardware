#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <time.h>
#include <WiFiClientSecure.h>

// =====================================================
// CONFIGURAÇÃO DO SENSOR DE UMIDADE DO SOLO
// =====================================================

#define SENSOR_UMIDADE 32

// =====================================================
// CONFIGURAÇÕES DO FIREBASE
// =====================================================

const char* servidor =
  "https://regador-automatico-40b2c-default-rtdb.firebaseio.com";

// Envio a cada 1 minutos
const unsigned long INTERVALO_ENVIO = 1UL * 60UL * 1000UL;

// Tempo máximo de tentativa de conexão Wi-Fi
const unsigned long TEMPO_ESPERA_WIFI = 90UL * 1000UL;

// =====================================================
// OBJETOS E VARIÁVEIS
// =====================================================

Preferences preferences;
WebServer server(80);

String espId = "";

bool modoConfiguracao = false;
bool servidorConfigurado = false;

unsigned long ultimoEnvio = 0;

// =====================================================
// PROTÓTIPOS
// =====================================================

void carregarWiFiSalvo();
void salvarWiFi(String ssid, String senha);
void conectarWiFi();

void iniciarModoConfiguracao();
void configurarServidorHTTP();

void inicializarNTP();

bool enviarHTTP(String payload);
void enviarFilaOffline();
void salvarOffline(String dado);

String obterDataHora();
String criarPayload(int umidadeSolo);

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("   REGADOR AUTOMATICO - ESP32");
  Serial.println("   SENSOR DE UMIDADE DO SOLO");
  Serial.println("========================================");

  // ---------------------------------------------------
  // PINO DO SENSOR
  // ---------------------------------------------------

  pinMode(SENSOR_UMIDADE, INPUT);

  Serial.println("Sensor de umidade do solo inicializado.");

  // ---------------------------------------------------
  // LITTLEFS
  // ---------------------------------------------------

  if (!LittleFS.begin(true)) {

    Serial.println("Falha ao iniciar LittleFS!");

  } else {

    Serial.println("LittleFS iniciado!");
  }

  // ---------------------------------------------------
  // WI-FI
  // ---------------------------------------------------

  carregarWiFiSalvo();
  conectarWiFi();

  // ---------------------------------------------------
  // IDENTIFICAÇÃO DA ESP32
  // ---------------------------------------------------

  espId = WiFi.macAddress();

  Serial.print("\nESP ID: ");
  Serial.println(espId);

  // ---------------------------------------------------
  // NTP
  // ---------------------------------------------------

  if (WiFi.status() == WL_CONNECTED) {

    inicializarNTP();

    enviarFilaOffline();
  }

  ultimoEnvio = millis();

  Serial.println();
  Serial.println("========================================");
  Serial.println("ESP32 pronta.");
  Serial.println("Sensor ativo: Umidade do solo.");
  Serial.println("Proximo envio em 15 minutos.");
  Serial.println("========================================");
}

// =====================================================
// LOOP PRINCIPAL
// =====================================================

void loop() {

  // ---------------------------------------------------
  // MODO CONFIGURAÇÃO
  // ---------------------------------------------------

  if (modoConfiguracao) {
    server.handleClient();
  }

  // ---------------------------------------------------
  // AGUARDA INTERVALO DE ENVIO
  // ---------------------------------------------------

  unsigned long agora = millis();

  if (agora - ultimoEnvio < INTERVALO_ENVIO) {

    delay(100);
    return;
  }

  ultimoEnvio = agora;

  Serial.println();
  Serial.println("========================================");
  Serial.println("Intervalo de 15 minutos atingido.");
  Serial.println("========================================");

  // ---------------------------------------------------
  // RECONECTA AO WI-FI
  // ---------------------------------------------------

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("Desconectado. Tentando reconexao...");

    conectarWiFi();
  }

  // ---------------------------------------------------
  // ATUALIZA NTP E ENVIA FILA OFFLINE
  // ---------------------------------------------------

  if (WiFi.status() == WL_CONNECTED) {

    modoConfiguracao = false;

    if (obterDataHora() == "sem_horario") {
      inicializarNTP();
    }

    enviarFilaOffline();
  }

  // ---------------------------------------------------
  // LEITURA DO SENSOR DE UMIDADE DO SOLO
  // ---------------------------------------------------

  int leituraSolo = analogRead(SENSOR_UMIDADE);

  // Mapeamento:
  // 4095 = seco
  // 1 = molhado

  int umidadeSolo = map(
    leituraSolo,
    4095,
    1,
    0,
    100
  );

  umidadeSolo = constrain(
    umidadeSolo,
    0,
    100
  );

  // ---------------------------------------------------
  // EXIBIÇÃO DAS LEITURAS
  // ---------------------------------------------------

  Serial.println();
  Serial.println("===== LEITURA DO SENSOR =====");

  Serial.printf(
    "ADC do Sensor de Solo: %d\n",
    leituraSolo
  );

  Serial.printf(
    "Umidade do Solo: %d%%\n",
    umidadeSolo
  );

  Serial.printf(
    "Data/Hora: %s\n",
    obterDataHora().c_str()
  );

  // ---------------------------------------------------
  // CRIA PAYLOAD
  // ---------------------------------------------------

  String payload = criarPayload(umidadeSolo);

  Serial.println();
  Serial.println("Payload:");
  Serial.println(payload);

  // ---------------------------------------------------
  // ENVIO PARA O FIREBASE
  // ---------------------------------------------------

  if (WiFi.status() == WL_CONNECTED) {

    if (enviarHTTP(payload)) {

      Serial.println("Leitura enviada com sucesso!");

    } else {

      Serial.println("Falha no envio HTTP.");
      Serial.println("Salvando leitura offline...");

      salvarOffline(payload);
    }

  } else {

    Serial.println("Sem Wi-Fi.");
    Serial.println("Salvando leitura na fila offline...");

    salvarOffline(payload);
  }

  Serial.println();
  Serial.println("Proximo envio em 15 minutos.");
}

// =====================================================
// SUPORTE NTP
// =====================================================

void inicializarNTP() {

  configTime(
    -3 * 3600,
    0,
    "pool.ntp.org",
    "a.st1.ntp.br",
    "b.st1.ntp.br"
  );

  Serial.println();
  Serial.println("Sincronizando horario NTP...");

  struct tm timeinfo;

  int tentativas = 0;

  while (!getLocalTime(&timeinfo) && tentativas < 30) {

    Serial.print(".");
    delay(500);

    tentativas++;
  }

  Serial.println();

  if (tentativas < 30) {

    Serial.println("Horario sincronizado!");
    Serial.println(obterDataHora());

  } else {

    Serial.println("Falha ao obter horario via NTP.");
  }
}

// =====================================================
// GERENCIAMENTO WI-FI E PREFERENCES
// =====================================================

void carregarWiFiSalvo() {

  preferences.begin("wifi", true);

  String ssidSalvo = preferences.getString("ssid", "");

  preferences.end();

  if (ssidSalvo.length() > 0) {

    Serial.printf(
      "\nWi-Fi salvo encontrado: %s\n",
      ssidSalvo.c_str()
    );

  } else {

    Serial.println("\nNenhuma rede Wi-Fi salva.");
  }
}

// -----------------------------------------------------

void salvarWiFi(String ssid, String senha) {

  preferences.begin("wifi", false);

  preferences.putString("ssid", ssid);
  preferences.putString("password", senha);

  preferences.end();

  Serial.println("Credenciais Wi-Fi salvas.");
}

// -----------------------------------------------------

void conectarWiFi() {

  preferences.begin("wifi", true);

  String ssid = preferences.getString("ssid", "");
  String senha = preferences.getString("password", "");

  preferences.end();

  if (ssid.length() == 0) {

    Serial.println("Nenhum Wi-Fi configurado.");

    iniciarModoConfiguracao();
    return;
  }

  Serial.println();
  Serial.println("========================================");

  Serial.printf(
    "Conectando ao Wi-Fi: %s\n",
    ssid.c_str()
  );

  Serial.println("Tempo maximo: 1 minuto e 30 segundos");

  Serial.println("========================================");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    ssid.c_str(),
    senha.c_str()
  );

  unsigned long inicio = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - inicio < TEMPO_ESPERA_WIFI
  ) {

    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    servidorConfigurado = true;

    Serial.println("Wi-Fi conectado!");

    Serial.printf(
      "IP: %s | RSSI: %d dBm\n",
      WiFi.localIP().toString().c_str(),
      WiFi.RSSI()
    );

  } else {

    Serial.println("Falha ao conectar ao Wi-Fi.");

    iniciarModoConfiguracao();
  }
}

// =====================================================
// MODO CONFIGURAÇÃO ACCESS POINT
// =====================================================

void iniciarModoConfiguracao() {

  modoConfiguracao = true;

  WiFi.mode(WIFI_AP_STA);

  if (!WiFi.softAP("Regador_Config", "00000123")) {

    Serial.println("Falha ao iniciar Access Point.");

    return;
  }

  Serial.println();
  Serial.println("========================================");
  Serial.println("MODO CONFIGURACAO ATIVO");
  Serial.println("SSID: Regador_Config");
  Serial.println("Senha: 00000123");

  Serial.printf(
    "IP AP: %s\n",
    WiFi.softAPIP().toString().c_str()
  );

  Serial.println("========================================");

  configurarServidorHTTP();

  server.begin();
}

// =====================================================
// SERVIDOR HTTP DE CONFIGURAÇÃO
// =====================================================

void configurarServidorHTTP() {

  server.on("/", HTTP_GET, []() {

    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">

  <title>Configurar Regador</title>

  <style>
    body {
      font-family: Arial, sans-serif;
      background: #f2f2f2;
      padding: 20px;
    }

    .container {
      max-width: 420px;
      margin: 30px auto;
      background: white;
      padding: 25px;
      border-radius: 12px;
    }

    h2 {
      text-align: center;
      color: #333;
    }

    label {
      display: block;
      margin-top: 15px;
      font-weight: bold;
    }

    input {
      width: 100%;
      padding: 12px;
      margin-top: 5px;
      box-sizing: border-box;
      border: 1px solid #ccc;
      border-radius: 6px;
    }

    button {
      width: 100%;
      padding: 13px;
      margin-top: 20px;
      border: none;
      border-radius: 6px;
      background: #2196F3;
      color: white;
      font-size: 16px;
      cursor: pointer;
    }

    button:hover {
      background: #1976D2;
    }
  </style>
</head>

<body>

  <div class="container">

    <h2>Configurar Wi-Fi</h2>

    <form action="/config" method="POST">

      <label for="ssid">Rede Wi-Fi</label>

      <input
        type="text"
        id="ssid"
        name="ssid"
        required
      >

      <label for="password">Senha</label>

      <input
        type="password"
        id="password"
        name="password"
      >

      <button type="submit">Salvar</button>

    </form>

  </div>

</body>
</html>
)rawliteral";

    server.send(200, "text/html", html);
  });

  // ---------------------------------------------------
  // RECEBE CONFIGURAÇÃO WI-FI
  // ---------------------------------------------------

  server.on("/config", HTTP_POST, []() {

    if (
      !server.hasArg("ssid") ||
      !server.hasArg("password")
    ) {

      server.send(
        400,
        "application/json",
        "{\"erro\":\"Dados incompletos\"}"
      );

      return;
    }

    String ssid = server.arg("ssid");
    String senha = server.arg("password");

    ssid.trim();

    salvarWiFi(ssid, senha);

    server.send(
      200,
      "application/json",
      "{\"sucesso\":true,\"mensagem\":\"Salvo! Reiniciando...\"}"
    );

    delay(2000);

    ESP.restart();
  });
}

// =====================================================
// ENVIO FIREBASE HTTPS REST
// =====================================================

bool enviarHTTP(String payload) {

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String url =
    String(servidor) +
    "/esps/" +
    espId +
    "/leituras.json";

  bool sucesso = false;

  Serial.println();
  Serial.println("Enviando leitura para o Firebase...");

  for (int tentativa = 1; tentativa <= 3; tentativa++) {

    Serial.printf(
      "Tentativa HTTPS %d/3\n",
      tentativa
    );

    WiFiClientSecure client;

    // Para teste/prototipo.
    // Em producao, o certificado deve ser validado.
    client.setInsecure();

    client.setTimeout(15000);

    HTTPClient http;

    if (http.begin(client, url)) {

      http.setTimeout(15000);

      http.addHeader(
        "Content-Type",
        "application/json"
      );

      int codigo = http.POST(payload);

      Serial.printf(
        "HTTP Code: %d\n",
        codigo
      );

      if (codigo == 200 || codigo == 201) {

        String resposta = http.getString();

        Serial.println("Resposta Firebase:");
        Serial.println(resposta);

        sucesso = true;

        http.end();

        break;

      } else {

        if (codigo > 0) {

          String resposta = http.getString();

          Serial.println("Resposta do servidor:");
          Serial.println(resposta);

        } else {

          Serial.printf(
            "Erro HTTPS: %s\n",
            http.errorToString(codigo).c_str()
          );
        }
      }

      http.end();

    } else {

      Serial.println("Falha ao iniciar conexao HTTPS.");
    }

    delay(3000);
  }

  return sucesso;
}

// =====================================================
// GESTÃO DA FILA OFFLINE
// =====================================================

void salvarOffline(String dado) {

  File arquivo = LittleFS.open(
    "/fila.txt",
    FILE_APPEND
  );

  if (!arquivo) {

    Serial.println(
      "Erro ao abrir /fila.txt para escrita."
    );

    return;
  }

  arquivo.println(dado);

  arquivo.close();

  Serial.println(
    "Leitura mantida localmente em LittleFS."
  );
}

// -----------------------------------------------------

void enviarFilaOffline() {

  if (
    WiFi.status() != WL_CONNECTED ||
    !LittleFS.exists("/fila.txt")
  ) {
    return;
  }

  File arquivo = LittleFS.open(
    "/fila.txt",
    FILE_READ
  );

  if (!arquivo || arquivo.size() == 0) {

    if (arquivo) {
      arquivo.close();
    }

    LittleFS.remove("/fila.txt");

    return;
  }

  File temporario = LittleFS.open(
    "/fila_temp.txt",
    FILE_WRITE
  );

  if (!temporario) {

    arquivo.close();

    Serial.println(
      "Erro ao criar arquivo temporario da fila."
    );

    return;
  }

  int total = 0;
  int enviados = 0;
  int falhas = 0;

  Serial.println();
  Serial.println("Processando fila de leituras offline...");

  while (arquivo.available()) {

    String linha = arquivo.readStringUntil('\n');

    linha.trim();

    if (linha.length() == 0) {
      continue;
    }

    total++;

    if (enviarHTTP(linha)) {

      enviados++;

    } else {

      falhas++;

      temporario.println(linha);
    }
  }

  arquivo.close();
  temporario.close();

  LittleFS.remove("/fila.txt");

  if (falhas > 0) {

    LittleFS.rename(
      "/fila_temp.txt",
      "/fila.txt"
    );

  } else {

    LittleFS.remove("/fila_temp.txt");
  }

  Serial.printf(
    "Fila Offline: Total: %d | Enviados: %d | Falhas: %d\n",
    total,
    enviados,
    falhas
  );
}

// =====================================================
// FUNÇÕES AUXILIARES
// =====================================================

String obterDataHora() {

  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    return "sem_horario";
  }

  char buffer[25];

  strftime(
    buffer,
    sizeof(buffer),
    "%Y-%m-%d %H:%M:%S",
    &timeinfo
  );

  return String(buffer);
}

// =====================================================
// CRIA PAYLOAD JSON
// =====================================================

String criarPayload(int umidadeSolo) {

  String json = "{";

  json += "\"espId\":\"" + espId + "\",";
  json += "\"dataHora\":\"" + obterDataHora() + "\",";

  // Timestamp Unix real
  time_t agora = time(nullptr);

  if (agora > 100000) {

    json += "\"timestamp\":";
    json += String((unsigned long)agora);
    json += ",";

  } else {

    json += "\"timestamp\":0,";
  }

  json += "\"umidadeSolo\":";
  json += String(umidadeSolo);

  json += "}";

  return json;
}
