// https://github.com/gmag11/painlessMesh/blob/master/examples/startHere/startHere.ino
// ************************************************************
// 1. Led brilhara uma vez, por cada Nó existente
// 2. Ciclo de brilho dura o tempo de BLINK_PERIOD
// 3. Envia uma msg para cada nó da rede mesh, randomicamente, entre 1 e 5 segundos,
//    com dados também de temperatura, pressão e altitude, através do sensor BMP-180
// 4. Printa qualquer msg recebida no Monitor Serial
// ************************************************************
// IP: 192.168.0.114 - MAC: E8-DB-84-E0-CA-BD
// IP: 192.168.0.115 - MAC: C4-5B-BE-6D-87-B2
#include <WiFiManager.h>  // WiFiManager
#include <ESP8266WiFi.h>  // OTA
#include <ESP8266mDNS.h>  // OTA
#include <WiFiUdp.h>      // OTA
#include <ArduinoOTA.h>   // OTA
#include <SFE_BMP180.h>   // BMP
#include <Wire.h>         // BMP
#include <painlessMesh.h> // MESH

#define   LED             2    // número GPIO do LED conectado
#define   BLINK_PERIOD    3000 // millisegundos para repetir o ciclo
#define   BLINK_DURATION  100  // millisegundos com o LED aceso

#define   MESH_SSID       "MESH-IoTCefet" // MESH
#define   MESH_PASSWORD   "MESH-IoTCefet" // MESH
#define   MESH_PORT       5555            // MESH

#define ALTITUDE 6.0 // Altitude local em metros, no RJ, para o sensor BMP

///////////////////////////
// Prototipos de funcoes //
///////////////////////////
void initSerial();  // 1
void initWiFi();    // 2
void initOTA();     // 3
void initBMP();     // 4
void sendMessage(); // 5
void receivedCallback(uint32_t from, String & msg);       // A
void newConnectionCallback(uint32_t nodeId);              // B
void changedConnectionCallback();                         // C
void nodeTimeAdjustedCallback(int32_t offset);            // D
void delayReceivedCallback(uint32_t from, int32_t delay); // E

/////////////
// Objetos //
/////////////
WiFiManager  wifimanager; // criar um objeto WiFiManager
Scheduler    userScheduler; // controlar agendamentos do usuario
painlessMesh mesh; // criar um objeto Mesh
SFE_BMP180   pressure;// criar um objeto SFE_BMP180, chamado "pressure"

///////////////
// Variaveis //
///////////////
String TransformDoubleString = "";
bool calc_delay = false;
bool onFlag = false;
SimpleList<uint32_t> nodes;

Task taskSendMessage(TASK_SECOND * 1, TASK_FOREVER, &sendMessage); // Começa com 1 segundo de intervalo
Task blinkNoNodes; // Agenda para fazer brilhar o numero de Nos

//////////////////////////
// Funções propriamente //
//////////////////////////
// 1
void initSerial() {
  Serial.begin(115200);
  while (!Serial)
    ;
  delay(500); // velocidade de conexao com o terminal
}
// 2
void initWiFi() {
  WiFi.mode(WIFI_STA); // explicitar claramente o modo q se deseja, ou STA ou AP

  // Resetar configuracoes de WiFi ja gravadas - comentar se não quiser resetar
  wifimanager.resetSettings();

  // -> Escolher 1 dessas 3 opcoes abaixo:
  // bool res = wifimanager.autoConnect(); // Nome do AP gerado automaticamente
  bool res = wifimanager.autoConnect("AutoConnectAP"); // AP anonimo
  //bool res = wifimanager.autoConnect("AutoConnectAP", "password"); // AP protegido por senha

  if (!res) {
    Serial.println("Falhou ao conectar");
    ESP.restart();
  }
  else {
    // se chegou até aqui, você conseguiu conectar ao WiFi
    Serial.print(" => Conectado com sucesso na rede via WifiManager na rede: ");
    Serial.println(WiFi.SSID());
    Serial.println();
    Serial.print(" (1) IP obtido: ");
    Serial.print(WiFi.localIP());  // mostra o endereço IP obtido via DHCP
    Serial.println();
    Serial.print(" (2) Endereço MAC: ");
    Serial.print(WiFi.macAddress()); // mostra o endereço MAC do esp8266
    Serial.println();
  }
}
// 3
void initOTA() {
  Serial.println();
  Serial.println(" -> Iniciando OTA....");

  // Porta padrao: 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("ESP8266-2");  // nome da host da placa, via OTA

  // Por padrao, sem autenticacao
  //ArduinoOTA.setPassword("admin");

  // Senhas podem ser setadas com valores criptografados de md5
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    Serial.println("## Inicio do OTA ##");
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n## Fim OTA ##");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progresso: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println(" - Programa carregado através de OTA!");
}
// 4
void initBMP() {
  if (pressure.begin())
    Serial.println("Inicializacao do sensor BMP180 com successo!");
  else
  {
    Serial.println("Falha na inicializacao do sensor BMP180 !\n\n");
    while (1); // Pausa eterna
  }
}
// 5
void sendMessage() {
  double T, P, p0, a;
  pressure.startTemperature();
  pressure.getTemperature(T);
  pressure.startPressure(3);
  pressure.getPressure(P, T);
  String msg = "Ola, do noh : ";
  msg += mesh.getNodeId();
  msg += " -> Memoria Livre: " + String(ESP.getFreeHeap());
  TransformDoubleString = String(T, 2);
  msg += ", TempC: " + TransformDoubleString + "ºC";
  TransformDoubleString = String((9.0 / 5.0) * T + 32.0, 2);
  msg += ", TempF: " + TransformDoubleString + "ºF";
  TransformDoubleString = String(P, 2);
  msg += ", PressMb: " + TransformDoubleString + " mb";
  p0 = pressure.sealevel(P, ALTITUDE);
  a = pressure.altitude(P, p0);
  TransformDoubleString = String(a, 2);
  msg += ", Altit: " + TransformDoubleString + " m.";
  mesh.sendBroadcast(msg);

  if (calc_delay) {
    SimpleList<uint32_t>::iterator node = nodes.begin();
    while (node != nodes.end()) {
      mesh.startDelayMeas(*node); // Envia a um nó um pacote para medir o atraso de viagem da rede para esse nó
      node++;
    }
    calc_delay = false;
  }
  Serial.printf("- Enviando mensagem: %s\n", msg.c_str());
  taskSendMessage.setInterval(random(TASK_SECOND * 1, TASK_SECOND * 5));  // entre 1 e 5 segundos
}

void setup() {
  initSerial();
  pinMode(LED, OUTPUT);
  initWiFi(); // WiFiManager
  initOTA(); // ArduinoOTA
  initBMP();

  //mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE ); // todos tipos possiveis
  mesh.setDebugMsgTypes(ERROR | DEBUG);  // set before init() so that you can see error messages

  mesh.init(MESH_SSID, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
  mesh.onNodeDelayReceived(&delayReceivedCallback);

  userScheduler.addTask( taskSendMessage );
  taskSendMessage.enable();

  blinkNoNodes.set(BLINK_PERIOD, (mesh.getNodeList().size() + 1) * 2, []() {
    // If on, switch off, else switch on
    if (onFlag)
      onFlag = false;
    else
      onFlag = true;
    blinkNoNodes.delay(BLINK_DURATION);

    if (blinkNoNodes.isLastIteration()) {
      // Finished blinking. Reset task for next run
      // blink number of nodes (including this node) times
      blinkNoNodes.setIterations((mesh.getNodeList().size() + 1) * 2);
      // Calculate delay based on current mesh time and BLINK_PERIOD
      // This results in blinks between nodes being synced
      blinkNoNodes.enableDelayed(BLINK_PERIOD -
                                 (mesh.getNodeTime() % (BLINK_PERIOD * 1000)) / 1000);
    }
  });
  userScheduler.addTask(blinkNoNodes);
  blinkNoNodes.enable();

  randomSeed(analogRead(A0));
}

void loop() {
  ArduinoOTA.handle();
  mesh.update();
  ArduinoOTA.handle();
  digitalWrite(LED, !onFlag);
}
// A
void receivedCallback(uint32_t from, String & msg) {
  Serial.printf("startHere: Recebido de %u msg=%s\n", from, msg.c_str());
}
// B
void newConnectionCallback(uint32_t nodeId) {
  // Reseta agendamento do brilhar
  onFlag = false;
  blinkNoNodes.setIterations((mesh.getNodeList().size() + 1) * 2);
  blinkNoNodes.enableDelayed(BLINK_PERIOD - (mesh.getNodeTime() % (BLINK_PERIOD * 1000)) / 1000);

  Serial.printf("--> startHere: Nova Conexao, nodeId = %u\n", nodeId);
  Serial.printf("--> startHere: Nova Conexao, %s\n", mesh.subConnectionJson(true).c_str());
}
// C
void changedConnectionCallback() {
  Serial.printf("Conexoes alteradas \n");
  // Reset blink task
  onFlag = false;
  blinkNoNodes.setIterations((mesh.getNodeList().size() + 1) * 2);
  blinkNoNodes.enableDelayed(BLINK_PERIOD - (mesh.getNodeTime() % (BLINK_PERIOD * 1000)) / 1000);

  nodes = mesh.getNodeList();

  Serial.printf("Numero de nós : %d\n", nodes.size());
  Serial.printf("Connection list:");

  SimpleList<uint32_t>::iterator node = nodes.begin();
  while (node != nodes.end()) {
    Serial.printf(" %u", *node);
    node++;
  }
  Serial.println();
  calc_delay = true;
}
// D
void nodeTimeAdjustedCallback(int32_t offset) {
  Serial.printf("Tempo ajustado: %u. Offset = %d\n", mesh.getNodeTime(), offset);
}
// E
void delayReceivedCallback(uint32_t from, int32_t delay) {
  Serial.printf("Delay to node %u is %d us\n", from, delay);
}
