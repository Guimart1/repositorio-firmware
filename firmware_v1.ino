#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>

// ============================================================
// VERSAO E REPOSITORIO OTA
// ============================================================
const char* VERSAO_ATUAL = "1.0";

// TROCAR pela URL raw do version.json do repositorio do grupo
const char* URL_MANIFESTO =
  "https://raw.githubusercontent.com/Guimart1/repositorio-firmware/main/version.json";

// Quantas sessoes completas antes de consultar o repositorio (minimo exigido: 3)
const int SESSOES_ANTES_DA_OTA = 3;

int sessoesConcluidas = 0;
bool otaJaTentada = false;

// Configuracao de rede (simulador Wokwi)
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = ""; // rede aberta, sem senha

// Configuracao de hardware
const int PINO_LED_AZUL      = 2;  // LED azul indica FW 1.0 em execucao
const int PINO_LED_VERMELHO  = 25; // canal vermelho do RGB (apagado no FW 1.0)
const int PINO_LED_VERDE     = 26; // canal verde do RGB (apagado no FW 1.0)

// Configuracao das sessoes de leitura
const int NUM_LEITURAS = 5;
const unsigned long INTERVALO_LEITURA_MS = 2000;   // 2 segundos entre leituras
const unsigned long INTERVALO_SESSAO_MS  = 48000;  // 48 segundos entre inicios de sessao
const float ALTURA_MIN_CM = 10.0;
const float ALTURA_MAX_CM = 20.0;

// Estado da maquina de leituras (controlada via millis())
float leituras[NUM_LEITURAS];
int leituraAtual = 0;

unsigned long inicioSessaoMs = 0;   // marca o inicio da sessao atual
unsigned long ultimaLeituraMs = 0;  // marca a ultima leitura feita
bool sessaoEmAndamento = false;

// PROTOTIPOS DAS FUNCOES
void conectarWiFi();
void indicarFirmware1();
void iniciarNovaSessao();
void realizarLeitura();
float gerarLeituraSimulada();
float calcularMedia();
void finalizarSessao();
void verificarAtualizacao();
bool baixarManifesto(String& json);
String extrairCampoJson(const String& json, const String& campo);
void executarOTA(const String& urlFirmware);


// ============================================================
// setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PINO_LED_AZUL, OUTPUT);
  pinMode(PINO_LED_VERMELHO, OUTPUT);
  pinMode(PINO_LED_VERDE, OUTPUT);
  digitalWrite(PINO_LED_VERMELHO, LOW); // garante que so o azul acende no FW 1.0
  digitalWrite(PINO_LED_VERDE, LOW);
  indicarFirmware1();

  randomSeed(analogRead(0));

  conectarWiFi();

  Serial.println("========================================");
  Serial.println("MONITORAMENTO DE VEGETACAO - FW 1.0");
  Serial.println("========================================");

  iniciarNovaSessao();
}


// ============================================================
// loop()
// ============================================================
void loop() {
  unsigned long agora = millis();

  // Enquanto a sessao estiver em andamento, faz uma leitura a cada 2s
  if (sessaoEmAndamento && (agora - ultimaLeituraMs >= INTERVALO_LEITURA_MS)) {
    realizarLeitura();
    ultimaLeituraMs = agora;
  }

  // Quando as 5 leituras da sessao terminarem, calcula e exibe a media
  if (sessaoEmAndamento && leituraAtual >= NUM_LEITURAS) {
    finalizarSessao();
  }

  // Uma nova sessao comeca exatamente 48s apos o INICIO da sessao anterior
  if (!sessaoEmAndamento && (agora - inicioSessaoMs >= INTERVALO_SESSAO_MS)) {
    iniciarNovaSessao();
  }
}


// ============================================================
// REDE
// ============================================================

// Conecta o ESP32 a rede virtual Wokwi-GUEST
void conectarWiFi() {
  Serial.print("Conectando a rede ");
  Serial.print(WIFI_SSID);
  Serial.print("...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long inicioTentativa = millis();
  const unsigned long TIMEOUT_MS = 15000;

  while (WiFi.status() != WL_CONNECTED && (millis() - inicioTentativa < TIMEOUT_MS)) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi conectado! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    // SITUACAO 1: nao ha conexao Wi-Fi
    Serial.println("ERRO: nao foi possivel conectar ao Wi-Fi (Wokwi-GUEST).");
    Serial.println("O firmware continuara operando localmente (sem OTA).");
  }
}


// ============================================================
// SESSOES DE LEITURA
// ============================================================

// Indica visualmente que o Firmware 1.0 esta em execucao (LED azul aceso)
void indicarFirmware1() {
  digitalWrite(PINO_LED_AZUL, HIGH);
}

// Inicia uma nova sessao de leituras
void iniciarNovaSessao() {
  inicioSessaoMs = millis();
  ultimaLeituraMs = inicioSessaoMs - INTERVALO_LEITURA_MS; // permite leitura imediata
  leituraAtual = 0;
  sessaoEmAndamento = true;
}

// Gera e registra uma leitura pseudoaleatoria (10 a 20 cm)
void realizarLeitura() {
  float valor = gerarLeituraSimulada();
  leituras[leituraAtual] = valor;
  leituraAtual++;

  Serial.print("Leitura ");
  Serial.print(leituraAtual);
  Serial.print(": ");
  Serial.print(valor, 1);
  Serial.println(" cm");
}

// Simulador de leituras da vegetacao entre 10 e 20 cm
float gerarLeituraSimulada() {
  long valorInteiro = random((long)(ALTURA_MIN_CM * 10), (long)(ALTURA_MAX_CM * 10) + 1);
  return valorInteiro / 10.0;
}

// Calcula a media aritmetica das leituras
float calcularMedia() {
  float soma = 0;
  for (int i = 0; i < NUM_LEITURAS; i++) {
    soma += leituras[i];
  }
  return soma / NUM_LEITURAS;
}

// Finaliza a sessao: exibe a media e, apos 3 ciclos, dispara a verificacao OTA
void finalizarSessao() {
  float media = calcularMedia();

  Serial.print("Media da sessao: ");
  Serial.print(media, 1);
  Serial.println(" cm");
  Serial.println("Proxima sessao em 48 segundos.");
  Serial.println();

  sessaoEmAndamento = false;
  sessoesConcluidas++;

  if (!otaJaTentada && sessoesConcluidas >= SESSOES_ANTES_DA_OTA) {
    verificarAtualizacao();
  }
}


// ============================================================
// ATUALIZACAO REMOTA (OTA)
// ============================================================

// Orquestra o fluxo: consultar -> comparar -> baixar -> gravar -> reiniciar
void verificarAtualizacao() {
  otaJaTentada = true;

  Serial.println("========================================");
  Serial.println("VERIFICACAO DE ATUALIZACAO REMOTA (OTA)");
  Serial.println("========================================");

  // SITUACAO 1: nao ha conexao Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERRO: sem conexao Wi-Fi. Nao foi possivel consultar o manifesto.");
    Serial.println("O Firmware 1.0 continua operando normalmente.");
    Serial.println();
    return;
  }

  // SITUACAO 2: o manifesto nao pode ser acessado
  String json;
  if (!baixarManifesto(json)) {
    Serial.println("O Firmware 1.0 continua operando normalmente.");
    Serial.println();
    return;
  }

  String versaoDisponivel = extrairCampoJson(json, "version");
  String urlFirmware      = extrairCampoJson(json, "url");

  if (versaoDisponivel == "" || urlFirmware == "") {
    Serial.println("ERRO: manifesto invalido. Campos 'version' ou 'url' ausentes.");
    Serial.println();
    return;
  }

  Serial.print("Versao instalada: ");
  Serial.print(VERSAO_ATUAL);
  Serial.print(" | Versao disponivel: ");
  Serial.println(versaoDisponivel);

  // SITUACAO 3: a versao instalada ja e a mais recente
  if (versaoDisponivel.toFloat() <= String(VERSAO_ATUAL).toFloat()) {
    Serial.println("A versao instalada ja e a mais recente. Nenhuma atualizacao necessaria.");
    Serial.println();
    return;
  }

  Serial.print("Nova versao disponivel. Baixando de: ");
  Serial.println(urlFirmware);

  executarOTA(urlFirmware);
}

// Consulta o version.json por HTTPS e devolve o conteudo em 'json'
bool baixarManifesto(String& json) {
  WiFiClientSecure cliente;
  cliente.setInsecure(); // nao valida o certificado, suficiente para o laboratorio

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(cliente, URL_MANIFESTO)) {
    Serial.println("ERRO: URL do manifesto invalida.");
    return false;
  }

  int codigo = http.GET();

  if (codigo != HTTP_CODE_OK) {
    Serial.print("ERRO: manifesto inacessivel (codigo HTTP ");
    Serial.print(codigo);
    Serial.println(").");
    http.end();
    return false;
  }

  json = http.getString();
  http.end();
  return true;
}

// Le o valor de um campo de texto do JSON, ex: "version": "2.0" devolve 2.0
// Implementado manualmente para nao depender de biblioteca externa
String extrairCampoJson(const String& json, const String& campo) {
  int posCampo = json.indexOf("\"" + campo + "\"");
  if (posCampo < 0) return "";

  int doisPontos  = json.indexOf(':', posCampo);
  if (doisPontos < 0) return "";

  int aspasInicio = json.indexOf('"', doisPontos + 1);
  int aspasFim    = json.indexOf('"', aspasInicio + 1);
  if (aspasInicio < 0 || aspasFim < 0) return "";

  return json.substring(aspasInicio + 1, aspasFim);
}

// Baixa o .bin, grava na particao OTA e reinicia o ESP32
void executarOTA(const String& urlFirmware) {
  WiFiClientSecure cliente;
  cliente.setInsecure();

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(cliente, urlFirmware);

  int codigo = http.GET();

  // SITUACAO 4: o arquivo de firmware nao pode ser baixado
  if (codigo != HTTP_CODE_OK) {
    Serial.print("ERRO: falha no download do firmware (codigo HTTP ");
    Serial.print(codigo);
    Serial.println("). Atualizacao cancelada.");
    http.end();
    return;
  }

  int tamanho = http.getSize();
  if (tamanho <= 0) {
    Serial.println("ERRO: tamanho do arquivo de firmware desconhecido. Atualizacao cancelada.");
    http.end();
    return;
  }

  Serial.print("Download iniciado. Tamanho: ");
  Serial.print(tamanho);
  Serial.println(" bytes");

  // SITUACAO 5: o processo de atualizacao retornou erro
  if (!Update.begin(tamanho)) {
    Serial.print("ERRO ao iniciar a gravacao OTA: ");
    Serial.println(Update.errorString());
    http.end();
    return;
  }

  Serial.println("Gravando novo firmware na particao OTA...");
  size_t gravado = Update.writeStream(*http.getStreamPtr());

  if (gravado != (size_t)tamanho) {
    Serial.print("ERRO na gravacao: ");
    Serial.print(gravado);
    Serial.print(" de ");
    Serial.print(tamanho);
    Serial.println(" bytes gravados. Atualizacao abortada.");
    Update.abort();
    http.end();
    return;
  }

  if (!Update.end(true)) {
    Serial.print("ERRO ao finalizar a gravacao OTA: ");
    Serial.println(Update.errorString());
    http.end();
    return;
  }

  http.end();

  Serial.println("Atualizacao concluida com sucesso.");
  Serial.println("Reiniciando o ESP32 para executar o Firmware 2.0...");
  Serial.println();
  delay(1000);
  ESP.restart();
}
