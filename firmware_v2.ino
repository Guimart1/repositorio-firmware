#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Versao deste firmware
const char* VERSAO_ATUAL = "2.0";

// Manifesto de versao -> TROCAR pela URL raw do version.json no repositorio do grupo
const char* URL_MANIFESTO = "https://raw.githubusercontent.com/USUARIO/REPOSITORIO/main/version.json";

// Configuracao de rede (simulador Wokwi)
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = ""; // rede aberta, sem senha

// LED RGB (catodo comum). O azul continua no pino 2, o mesmo do FW 1.0
const int PINO_LED_VERMELHO = 25;
const int PINO_LED_VERDE    = 26;
const int PINO_LED_AZUL     = 2;

// Configuracao das sessoes de leitura (igual ao FW 1.0)
const int NUM_LEITURAS = 5;
const unsigned long INTERVALO_LEITURA_MS = 2000;   // 2 segundos entre leituras
const unsigned long INTERVALO_SESSAO_MS  = 48000;  // 48 segundos entre inicios de sessao
const float ALTURA_MIN_CM = 10.0;
const float ALTURA_MAX_CM = 20.0;

// Limites da histerese (decisao feita pela mediana)
const float LIMITE_ALERTA_CM = 16.0;  // mediana >= 16 -> ALERTA
const float LIMITE_NORMAL_CM = 14.0;  // mediana <= 14 -> NORMAL
                                      // entre os dois -> mantem o estado anterior

enum EstadoSistema { NORMAL, ALERTA };
EstadoSistema estadoAtual = NORMAL;   // o sistema comeca em NORMAL

// Estado da maquina de leituras (controlada via millis())
float leituras[NUM_LEITURAS];
int leituraAtual = 0;

unsigned long inicioSessaoMs = 0;   // marca o inicio da sessao atual
unsigned long ultimaLeituraMs = 0;  // marca a ultima leitura feita
bool sessaoEmAndamento = false;

// PROTOTIPOS DAS FUNCOES
void conectarWiFi();
void verificarAtualizacao();
String extrairCampoJson(const String& json, const String& campo);
void iniciarNovaSessao();
void realizarLeitura();
float gerarLeituraSimulada();
float calcularMedia();
void ordenarCopia(float ordenadas[]);
float calcularMediana(float ordenadas[]);
void atualizarEstado(float mediana);
void atualizarLed();
void imprimirVetor(const char* rotulo, float vetor[]);
void finalizarSessao();


//__________________________________________________________________
// setup()
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PINO_LED_VERMELHO, OUTPUT);
  pinMode(PINO_LED_VERDE, OUTPUT);
  pinMode(PINO_LED_AZUL, OUTPUT);
  atualizarLed();  // comeca em NORMAL -> LED verde

  randomSeed(analogRead(0));

  conectarWiFi();

  Serial.println("========================================");
  Serial.println("MONITORAMENTO DE VEGETACAO - FW 2.0");
  Serial.println("========================================");

  verificarAtualizacao();  // no FW 2.0 deve mostrar que ja e a versao mais recente

  iniciarNovaSessao();
}


// loop()
void loop() {
  unsigned long agora = millis();

  // Enquanto a sessao estiver em andamento, faz uma leitura a cada 2s
  if (sessaoEmAndamento && (agora - ultimaLeituraMs >= INTERVALO_LEITURA_MS)) {
    realizarLeitura();
    ultimaLeituraMs = agora;
  }

  // Quando as 5 leituras terminarem, processa os dados da sessao
  if (sessaoEmAndamento && leituraAtual >= NUM_LEITURAS) {
    finalizarSessao();
  }

  // Uma nova sessao comeca exatamente 48s apos o INICIO da sessao anterior
  if (!sessaoEmAndamento && (agora - inicioSessaoMs >= INTERVALO_SESSAO_MS)) {
    iniciarNovaSessao();
  }
}


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
    Serial.println("ERRO: nao foi possivel conectar ao Wi-Fi (Wokwi-GUEST).");
    Serial.println("O firmware continuara operando localmente.");
  }
}

// Consulta o version.json e compara com a versao instalada
void verificarAtualizacao() {
  Serial.println("Verificando atualizacoes...");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERRO: sem Wi-Fi, nao foi possivel verificar atualizacoes.");
    return;
  }

  WiFiClientSecure cliente;
  cliente.setInsecure();  // nao valida o certificado HTTPS (suficiente pro laboratorio)

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(cliente, URL_MANIFESTO);
  int codigo = http.GET();

  if (codigo != HTTP_CODE_OK) {
    Serial.print("ERRO: nao foi possivel acessar o manifesto (codigo HTTP ");
    Serial.print(codigo);
    Serial.println(").");
    http.end();
    return;
  }

  String json = http.getString();
  http.end();

  String versaoDisponivel = extrairCampoJson(json, "version");
  if (versaoDisponivel == "") {
    Serial.println("ERRO: manifesto sem o campo \"version\".");
    return;
  }

  Serial.print("Versao instalada: ");
  Serial.print(VERSAO_ATUAL);
  Serial.print(" | Versao disponivel: ");
  Serial.println(versaoDisponivel);

  if (versaoDisponivel.toFloat() > String(VERSAO_ATUAL).toFloat()) {
    Serial.println("Existe uma versao mais nova disponivel no repositorio.");
  } else {
    Serial.println("A versao instalada ja e a mais recente. Nenhuma atualizacao necessaria.");
  }
  Serial.println();
}

// Le o valor de um campo de texto do JSON (ex: "version": "2.0" -> 2.0)
// Feito "na mao" pra nao depender de biblioteca externa
String extrairCampoJson(const String& json, const String& campo) {
  int posCampo = json.indexOf("\"" + campo + "\"");
  if (posCampo < 0) return "";

  int doisPontos = json.indexOf(':', posCampo);
  int aspasInicio = json.indexOf('"', doisPontos + 1);
  int aspasFim = json.indexOf('"', aspasInicio + 1);
  if (doisPontos < 0 || aspasInicio < 0 || aspasFim < 0) return "";

  return json.substring(aspasInicio + 1, aspasFim);
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

// Simulador de leituras da vegetacao entre 10 e 20 cm (com uma casa decimal)
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

// Copia as leituras e ordena a COPIA em ordem crescente (bubble sort)
// O vetor original fica intacto pra poder ser exibido na ordem das leituras
void ordenarCopia(float ordenadas[]) {
  for (int i = 0; i < NUM_LEITURAS; i++) {
    ordenadas[i] = leituras[i];
  }

  for (int i = 0; i < NUM_LEITURAS - 1; i++) {
    for (int j = 0; j < NUM_LEITURAS - 1 - i; j++) {
      if (ordenadas[j] > ordenadas[j + 1]) {
        float temp = ordenadas[j];
        ordenadas[j] = ordenadas[j + 1];
        ordenadas[j + 1] = temp;
      }
    }
  }
}

// Com 5 valores ordenados, a mediana e o do meio (3o elemento, indice 2)
float calcularMediana(float ordenadas[]) {
  return ordenadas[NUM_LEITURAS / 2];
}

// Histerese: so muda de estado quando a mediana passa dos limites
void atualizarEstado(float mediana) {
  EstadoSistema estadoAnterior = estadoAtual;

  if (mediana >= LIMITE_ALERTA_CM) {
    estadoAtual = ALERTA;
  } else if (mediana <= LIMITE_NORMAL_CM) {
    estadoAtual = NORMAL;
  }
  // entre 14 e 16 cm: nao faz nada, o estado anterior e mantido

  Serial.print("Estado: ");
  Serial.print(estadoAtual == ALERTA ? "ALERTA" : "NORMAL");

  if (mediana > LIMITE_NORMAL_CM && mediana < LIMITE_ALERTA_CM) {
    Serial.println(" (mantido - mediana entre 14 e 16 cm)");
  } else if (estadoAtual != estadoAnterior) {
    Serial.println(" (mudou de estado)");
  } else {
    Serial.println();
  }

  atualizarLed();
}

// LED do FW 2.0: verde = NORMAL, vermelho = ALERTA (azul fica apagado)
void atualizarLed() {
  digitalWrite(PINO_LED_AZUL, LOW);
  digitalWrite(PINO_LED_VERDE, estadoAtual == NORMAL ? HIGH : LOW);
  digitalWrite(PINO_LED_VERMELHO, estadoAtual == ALERTA ? HIGH : LOW);
}

// Imprime um vetor de leituras numa linha so
void imprimirVetor(const char* rotulo, float vetor[]) {
  Serial.print(rotulo);
  for (int i = 0; i < NUM_LEITURAS; i++) {
    Serial.print(vetor[i], 1);
    Serial.print(" ");
  }
  Serial.println();
}

// Finaliza a sessao: ordena, calcula media e mediana, aplica a histerese
void finalizarSessao() {
  float ordenadas[NUM_LEITURAS];
  ordenarCopia(ordenadas);

  float media = calcularMedia();
  float mediana = calcularMediana(ordenadas);

  Serial.println("----------------------------------------");
  imprimirVetor("Valores originais: ", leituras);
  imprimirVetor("Valores ordenados: ", ordenadas);

  Serial.print("Media da sessao: ");
  Serial.print(media, 1);
  Serial.println(" cm");

  Serial.print("Mediana da sessao: ");
  Serial.print(mediana, 1);
  Serial.println(" cm");

  atualizarEstado(mediana);

  Serial.println("Proxima sessao em 48 segundos.");
  Serial.println();

  sessaoEmAndamento = false;
  // inicioSessaoMs continua marcando o INICIO desta sessao -> proxima comeca 48s depois dele
}
