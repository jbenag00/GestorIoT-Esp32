/***************************************
************** LIBRERIAS ***************
****************************************/
#include <Arduino.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <Separador.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h> 
#include <DHT.h>
#include "ESP32_MailClient.h"


/***************************************
************* CONFIG WIFI **************
****************************************/

#define WIFI_PIN 17 //pin de reseteo del dispositivo


/***************************************
************* CONFIG DHT11 *************
****************************************/
#define DHT_PIN 27
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

float temp = 0;
float lastTemp;
int hum = 0;
int lastHum;
String temperaturaString = "";
String humedadString = "";

/***************************************
************* CONFIG FAN ************
****************************************/
const int FAN_PIN = 33; // Gobierna el ventilador


/***************************************
*********** CONFIG HC-SR501 ************
****************************************/

#define timeSeconds 10
#define MOTION_PIN 12

unsigned long lastTrigger = 0;
boolean startTimer = false;
bool mov=false;

/***************************************
************* CONFIG SMTP **************
****************************************/
SMTPData datosSMTP;
bool alarma=false;
bool mensaje=false;


/***************************************
************ CONFIG SERVER *************
****************************************/

//estos datos deben estar configurador también en las constantes del panel
const String serial_number = "001";
const String insert_password = "javier00";
const String get_data_password = "javier00";
const char*  server = "gestoriot.000webhostapp.com";



/***************************************
************* CONFIG MQTT **************
****************************************/
/* Datos Broker: 
Host: ioticos.org
Usuario: 3qsXwJBgJyXTBbd
Contraseña: SMUHLV8aY73co4g
Topico Raíz: 1MrsadXvvDK78pK
TCP: 1883
TCP SSL: 8883
WS: 8093
WSS: 8094
*/

const char *mqtt_server = "ioticos.org";
const int mqtt_port = 8883;

//El dispositivo se encargará de averiguar qué usuario y qué contraseña mqtt debe usar.
char mqtt_user[20] = "";
char mqtt_pass[20] = "";

WiFiManager wifiManager;
WiFiClientSecure client;
WiFiClientSecure client2;
PubSubClient mqttclient(client);
Separador s;


char device_topic_publish[40];    //Tópico para el panel
char device_topic_publish_t[40];  //Tópico solo temperatura
char device_topic_publish_h[40];  //Tópico solo humedad
char device_topic_subscribe[40];  //Tópico al que suscribirse

const int expected_topic_length = 26; //numero de caracteres esperados para el tópico
bool topic_obteined = false;  //comprobación de tópico
char msg[25]; //Caracteres del mensaje

//Botones del panel(modo auto, switch1, switch2, switch3, barra de refresco y tiempo de refresco)
byte autoAlarm = 0;
byte autoPanel = 0;
byte sw1 = 0;
byte sw2 = 0;
byte sw3 = 0;
int slider = 3;


/***************************************
************** FUNCIONES ***************
****************************************/

void setupWifi();
void setupDHT11();
float getTemperatura();
void publishTemperatura();
float getHumedad();
void publishHumedad();
void setupFan();
void setupPIR();
void IRAM_ATTR detectsMovement();
void setupMQTT();
void callbackMQTT(char* topic, byte* payload, unsigned int length);
void connectMQTT();
bool get_topic(int length);
void send_to_database();
void send_to_gmail(bool alarma, bool mensaje);



/***************************************
**************** SETUP *****************
****************************************/

void setup() {
  randomSeed(analogRead(0));
  Serial.begin(115200);
  setupWifi();
  setupDHT11();
  setupFan();
  setupPIR();
  
  while(!topic_obteined){
    topic_obteined = get_topic(expected_topic_length);
    delay(1000);
  }

  setupMQTT();
}



/***************************************
**************** LOOP ******************
****************************************/

void loop() {

 //si el pulsador wifi esta en low, activamos el acces point de configuración
  if (digitalRead(WIFI_PIN) == LOW ) {
    wifiManager.startConfigPortal("Panel de conexión.\n Gestor IoT WIFI-MANAGER ");
    Serial.println("Conectados a WiFi!!! :)");
  }


  if (!client.connected()) {
		connectMQTT();
	}

  if(startTimer && (millis() - lastTrigger > (timeSeconds*1000))) {
    Serial.println("\nMotion stopped...");
    startTimer = false;
  }
  
  if(mqttclient.connected()){
    temp = getTemperatura();
    hum = getHumedad();
    if(alarma){
      if (mov){
        mensaje=true;
        send_to_gmail(alarma,mensaje);
        mensaje=false;
      }
    }
    if(autoPanel==1){
      lastTemp=temp;
      lastHum=hum;
      if(sw3==0){
        Serial.println("Activando sensor AUTO");
        sw3=1;
      }
      if (mov){
        mov=false;
        Serial.println("\nEnviando a base de datos por movimiento detectado.\n");
        send_to_database();
      }
      if (temp < 20){
        Serial.println("\nAVISO: Temperatura BAJA.");
        if(sw1==0){
          Serial.println("\nActivando aire caliente AUTO.\n");
          sw1=1;
        }
        if(sw2==1){
          Serial.println("\nDesactivando aire frío AUTO.\n"); 
          sw2=0;
          digitalWrite(FAN_PIN, LOW);
        }
        Serial.println("\nEnviando a base de datos por temperatura inadecuada.\n");
        send_to_database();
      }else if (temp>25){     
        Serial.println("\nAVISO: Temperatura ALTA.");
        if(sw2==0){
          Serial.println("\nActivando aire frío AUTO.\n"); 
          sw2=1;
          digitalWrite(FAN_PIN, HIGH);
        }
        if(sw1==1){
            Serial.println("\nDesactivando aire caliente AUTO.\n");
            sw1=0;
        }
        Serial.println("\nEnviando a base de datos por temperatura inadecuada.\n");
        send_to_database();
      }else{
        Serial.println("\nTemperatura adecuada.");
        if(sw1==1){
          Serial.println("\nDesactivando aire caliente AUTO.\n");
          sw1=0;
        }else if(sw2==1){
          Serial.println("\nDesactivando aire frío AUTO.\n");
          sw2=0;
          digitalWrite(FAN_PIN, LOW);
        }else{
          Serial.println("\nSin acciones.\n");
        }
      }
    }else{
      if(mov){
        lastTemp=temp;
        lastHum=hum;
        if(sw3==0){
          Serial.println("Activando sensor AUTO por movimiento");
          sw3=1;
        }
        Serial.print("\nEnviando a base de datos por ");

        if(temp > 25){
          Serial.print("temperatura inadecuada.\n");
          Serial.println("\nAVISO: Temperatura ALTA.");
        }else if(temp<20){
          Serial.print("temperatura inadecuada.\n");
          Serial.println("\nAVISO: Temperatura BAJA.");
        }else{
          Serial.print("movimiento detectado.\n");
          Serial.println("\nTemperatura adecuada.");
        }
        send_to_database();
        mov=false;
      }else{
        if(sw3 == 1){
          lastTemp=temp;
          lastHum=hum;
        }
        if(sw1 == 1){
          //el aire caliente está encendido
          lastTemp=temp;
          lastHum=hum;
          if(sw3==0){
            Serial.println("Activando sensor AUTO");
            sw3=1;
          }
          Serial.println("\nEnviando a base de datos por calor encendido.\n");
          send_to_database();
        }
        if(sw2 == (int)1 ){
          //el aire frio está encendido
          lastTemp=temp;
          lastHum=hum;
          Serial.println("\nEnviando a base de datos por frío encendido.\n");
          send_to_database();
        }
      }
    }
    Serial.println("");
    publishTemperatura();
    publishHumedad();
    String to_send = String(lastTemp) + "," + String(lastHum) + "," + String(sw1)+","+ String(sw2)+","+String(sw3)+","+String(autoPanel)+","+String(autoAlarm);
    to_send.toCharArray(msg,20);
    mqttclient.publish(device_topic_publish,msg);
    
    Serial.println("");
    Serial.print("Actualizando Página en: ");
    for(int i=slider; i>0; i--){
      Serial.print(i);
      Serial.print(" ");
      delay(1000);
    }
    Serial.println("");
  }
  mqttclient.loop();
}



//************************************
//*********** FUNCIONES **************
//************************************

void setupWifi(){
  Serial.println("\n\nConfigurando WIFI");
  pinMode(WIFI_PIN,INPUT_PULLUP);  
  //set custom ip for portal
  //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0)); 

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  //wifiManager.resetSettings(); //En caso de querer que recuerde el ultimo AP, comentar esta linea
  wifiManager.autoConnect("ESP_WIFI_MANAGER");
  //or use this for auto generated name ESP + ChipID
  //wifiManager.autoConnect();
  
  //if you get here you have connected to the WiFi
  Serial.println("Conexión a WiFi exitosa!");
}

void setupDHT11(){
  Serial.println("\n\nConfigurando DHT11...");
  dht.begin();
  Serial.println("   Probando funcionamiento DHT11");
  getTemperatura();
  getHumedad();
  Serial.println("DHT11 Configurado");
}

float getTemperatura(){
  float t = dht.readTemperature();
  Serial.println("\n   Calculando Temperatura..");
  if(isnan(t)){
    Serial.println("   Fallo al leer la temperatura");
  }else{
    temperaturaString = "   Temperatura = ";
    temperaturaString+= t;
    temperaturaString+=" C";
    Serial.println(temperaturaString);
  }
  return t;
}

void publishTemperatura(){
  if(mqttclient.publish(device_topic_publish_t ,(char*) temperaturaString.c_str())){
    Serial.println("   Publicacion temperatura OK");
  }else{
    Serial.println("   Publicacion FALLIDA");
  }
}

float getHumedad(){
  float h = dht.readHumidity();
  Serial.println("\n   Calculando Humedad..");
  if(isnan(h)){
    Serial.println("\n   Fallo al leer la humedad");
  }else{
    humedadString = "   Humedad = ";
    humedadString += h;
    humedadString +=" %";
    Serial.println(humedadString);
  }
  return h;
}

void publishHumedad(){
  if(mqttclient.publish(device_topic_publish_h ,(char*) humedadString.c_str())){
    Serial.println("   Publicacion humedad OK");
  }else{
    Serial.println("   Publicacion FALLIDA");
  }
}

void setupFan(){
  Serial.println("\n\nConfigurando Ventilador...");
  pinMode(FAN_PIN, OUTPUT);
  Serial.println("   Probando funcionamiento de Ventilador");
  digitalWrite(FAN_PIN, HIGH);
  Serial.println("   Ventilador Encendido --> Ok");
  Serial.print("   Apagando Ventilador en: ");
  for(int i=3; i>0; i--){
      Serial.print(i);
      Serial.print(" ");
      delay(1000);
    }
  digitalWrite(FAN_PIN, LOW);
  Serial.println("\n   Ventilador Apagado --> Ok");
  Serial.println("Ventilador Configurado");
}

void setupPIR(){
  Serial.println("\n\nConfigurando PIR...");
  // PIR Motion m mode INPUT_PULLUP
  pinMode(MOTION_PIN, INPUT_PULLUP);
  // Set motion pin as interrupt, assign interrupt function and set RISING mode
  attachInterrupt(digitalPinToInterrupt(MOTION_PIN), detectsMovement, RISING);
  Serial.println("PIR Configurado");
}

// Checks if motion was detected, sets LED HIGH and starts a timer
void IRAM_ATTR detectsMovement() {
  Serial.println("\nMOTION DETECTED!!!");
  startTimer = true;
  lastTrigger = millis();
  mov=true;
  if(alarma){
    mensaje=true;
  }
}

//función para obtener el tópico perteneciente a este dispositivo
bool get_topic(int length){

  Serial.println("\n\nIniciando conexión segura para obtener tópico raíz...");

  if (!client2.connect(server, 443)) {
    Serial.println("   Falló conexión!");
    return false;
  }else {
    Serial.println("   Conectados a servidor para obtener tópico - ok");
    // Make a HTTP request:
    String data = "gdp="+get_data_password+"&sn="+serial_number+"\r\n";
    client2.print(String("POST ") + "/app/getdata/gettopics" + " HTTP/1.1\r\n" +\
                 "Host: " + server + "\r\n" +\
                 "Content-Type: application/x-www-form-urlencoded"+ "\r\n" +\
                 "Content-Length: " + String (data.length()) + "\r\n\r\n" +\
                 data +\
                 "Connection: close\r\n\r\n");

    Serial.println("   Solicitud enviada - ok");

    while (client2.connected()) {
      String line = client2.readStringUntil('\n');
      if (line == "\r") {
        Serial.println("   Headers recibidos - ok");
        break;
      }
    }

    String line;
    while(client2.available()){
      line += client2.readStringUntil('\n');
    }
    Serial.println("\n"+line);
    String temporal_topic = s.separa(line,'#',1);
    String temporal_user = s.separa(line,'#',2);
    String temporal_password = s.separa(line,'#',3);



    Serial.println("   El tópico es: " + temporal_topic);
    Serial.println("   El user MQTT es: " + temporal_user);
    Serial.println("   La pass MQTT es: " + temporal_password);
    Serial.println("   La cuenta del tópico obtenido es: " + String(temporal_topic.length()));

    if (temporal_topic.length()==length){
      Serial.println("   El largo del tópico es el esperado: " + String(temporal_topic.length()));

      String temporal_topic_subscribe = temporal_topic + "/actions/#";
      temporal_topic_subscribe.toCharArray(device_topic_subscribe,40);
      Serial.println("   Suscripción a: "+String(device_topic_subscribe)+" --> Ok");
      String temporal_topic_publish = temporal_topic + "/data";
      temporal_topic_publish.toCharArray(device_topic_publish,40);
      temporal_topic_publish = temporal_topic + "/temperatura";
      temporal_topic_publish.toCharArray(device_topic_publish_t,40);
      temporal_topic_publish = temporal_topic + "/humedad";
      temporal_topic_publish.toCharArray(device_topic_publish_h,40);
      temporal_user.toCharArray(mqtt_user,20);
      temporal_password.toCharArray(mqtt_pass,20);

      client2.stop();
      return true;
    }else{
      client2.stop();
      return false;
    }
  }
}

void send_to_database(){

  Serial.println("\n\nIniciando conexión segura para enviar a base de datos...");

  if (!client2.connect(server, 443)) {
    Serial.println("   Falló conexión!");
  }else {
    Serial.println("   Conectados a servidor para insertar en db - ok");
    // Make a HTTP request:
    String data = "idp="+insert_password+"&sn="+serial_number+"&temp="+String(temp)+"&hum="+String(hum)+"\r\n";
    client2.print(String("POST ") + "/app/insertdata/insert" + " HTTP/1.1\r\n" +\
                 "Host: " + server + "\r\n" +\
                 "Content-Type: application/x-www-form-urlencoded"+ "\r\n" +\
                 "Content-Length: " + String (data.length()) + "\r\n\r\n" +\
                 data +\
                 "Connection: close\r\n\r\n");

    Serial.println("   Solicitud enviada - ok");

    while (client2.connected()) {
      String line = client2.readStringUntil('\n');
      if (line == "\r") {
        Serial.println("   Headers recibidos - ok");
        break;
      }
    }
    String line;
    while(client2.available()){
      line += client2.readStringUntil('\n');
    }
    Serial.println("\n"+line);
    client2.stop();

  }

}

void send_to_gmail(bool alarma, bool mensaje){
  Serial.println("\n\nIniciando sesión en Gmail...");
  datosSMTP.setLogin("smtp.gmail.com", 465, "jbenagtfg@gmail.com", "javier00tfg");
  Serial.println("Escribiendo mensaje...");
  // Establecer el nombre del remitente y el correo electrónico
  datosSMTP.setSender("GestorIoT", "jbenagtfg@gmail.com");
  if(mensaje==false && alarma==true){
    // Establezca la prioridad o importancia del correo electrónico High, Normal, Low o 1 a 5 (1 es el más alto)
    datosSMTP.setPriority("Normal");
    // Establecer el asunto
    datosSMTP.setSubject("Información sobre la alarma");
    // Establece el mensaje de correo electrónico en formato de texto (sin formato)
    datosSMTP.setMessage("Alarma activada", false);
    // Agregar destinatarios, se puede agregar más de un destinatario
    datosSMTP.addRecipient("jbenagtfg@gmail.com");
  }else if(mensaje==false && alarma==false){
    // Establezca la prioridad o importancia del correo electrónico High, Normal, Low o 1 a 5 (1 es el más alto)
    datosSMTP.setPriority("Normal");
    // Establecer el asunto
    datosSMTP.setSubject("Información sobre la alarma");
    // Establece el mensaje de correo electrónico en formato de texto (sin formato)
    datosSMTP.setMessage("Alarma desactivada", false);
    // Agregar destinatarios, se puede agregar más de un destinatario
    datosSMTP.addRecipient("jbenagtfg@gmail.com");
  }else if(mensaje==true && alarma==true){
    // Establezca la prioridad o importancia del correo electrónico High, Normal, Low o 1 a 5 (1 es el más alto)
    datosSMTP.setPriority("High");
    // Establecer el asunto
    datosSMTP.setSubject("¡¡¡ALARMA!!!");
    // Establece el mensaje de correo electrónico en formato de texto (sin formato)
    datosSMTP.setMessage("La alarma ha detectado un intruso!!", false);
    // Agregar destinatarios, se puede agregar más de un destinatario
    datosSMTP.addRecipient("jbenagtfg@gmail.com");
  }

  //Comience a enviar correo electrónico.
  if (!MailClient.sendMail(datosSMTP)){
    Serial.println("Error enviando el correo, " + MailClient.smtpErrorReason());
    delay(1000);
    send_to_gmail(alarma,mensaje);
  }
  Serial.println("Mensaje enviado con éxito!!");

  //Borrar todos los datos del objeto datosSMTP para liberar memoria
  datosSMTP.empty();
}

void setupMQTT(){
  Serial.println("\n\nConfigurando broker...");
  mqttclient.setServer(mqtt_server, mqtt_port);
  mqttclient.setCallback(callbackMQTT);
  Serial.println("   Dispositivo escuchando mensajes del servidor MQTT");
  Serial.println("Servidor MQTT configurado");
}

void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  String incoming= "";
  Serial.println("\n\n***************************************");
  Serial.println("Mensaje recibido desde servidor MQTT.");
  Serial.print("Tópico: ");
  Serial.print(topic);
	for (int i = 0; i < length; i++) {
		incoming += (char)payload[i];
	}
	incoming.trim();
	Serial.println("\nMensaje -> " + incoming);
  Serial.println("***************************************");

  String str_topic = String(topic);
  String command = s.separa(str_topic,'/',3);

  if(command=="ala"){
    Serial.println("Alarma pasa manualmente a estado " + incoming);
    autoAlarm = incoming.toInt();
    Serial.print("Alarma: ");
    if(autoAlarm == int(1)){
      Serial.println("ACTIVADO.");
      alarma=true;
      send_to_gmail(alarma,mensaje);
    }else if(autoAlarm ==int(0)){
      Serial.println("DESACTIVADO.");
      alarma=false;
      send_to_gmail(alarma,mensaje);
    }
  }

  if(command=="aut"){
    Serial.println("AutoPanel pasa manualmente a estado " + incoming);
    autoPanel = incoming.toInt(); 
    Serial.print("Modo automatico: ");
    if(autoPanel == int(1)){
      Serial.println("ACTIVADO.");
    }else if(autoPanel ==int(0)){
      Serial.println("DESACTIVADO.");
    }
  }

  if(command=="sw1"){
    if(autoPanel==1){
      Serial.println("Modo automatico: DESACTIVADO.");
      autoPanel=0;
    }
    Serial.println("Sw1 pasa manualmente a estado " + incoming);
    sw1 = incoming.toInt();
    Serial.print("Aire caliente: ");
    if(sw1 == int(1)) {
      Serial.println("ACTIVADO");
      if(sw2==int(1)){
        Serial.println("Desactivando aire frío AUTO ");
        sw2=0;
        digitalWrite(FAN_PIN, LOW);
      }
      if(sw3==int(0)){
        Serial.println("Activando sensor AUTO");
        sw3=1;
      }
    }else if (sw1 == int(0)) {
      Serial.println("DESACTIVADO");
    }
  }

  if(command=="sw2"){
    if(autoPanel==1){
      Serial.println("Modo automatico: DESACTIVADO.");
      autoPanel=0;
    }
    Serial.println("Sw2 pasa manualmente a estado " + incoming);
    sw2 = incoming.toInt();
    Serial.print("Aire frio: ");
    if(sw2 == int(1)){
      Serial.println("ACTIVADO");
      digitalWrite(FAN_PIN, HIGH);
      if(sw1==int(1)){
        Serial.println("Desactivando aire caliente AUTO.");
        sw1=0;
      }
      if(sw3==int(0)){
        Serial.println("Activando sensor AUTO");
        sw3=1;
      }
    }else if (sw2 == int(0)) {
      Serial.println("DESACTIVADO");
      digitalWrite(FAN_PIN, LOW);
    }
  }
  if(command=="sw3"){
    Serial.println("Sw3 pasa manualmente a estado " + incoming);
    sw3 = incoming.toInt();
    Serial.print("Sensor de temperatura y humedad: ");
    if(sw3 == int(1)){
      Serial.println("ACTIVADO.");
    }else if(sw3==int(0)){
      Serial.println("DESACTIVADO.");
    }
  }
//no pasar de 10 segundos
  if(command=="slider"){
    Serial.println("Slider pasa a estado " + incoming);
    slider = incoming.toInt();
    if(slider < int(3)){
      Serial.println("El tiempo mínimo es 3 Segundos.");
      slider = 3;
    }
    Serial.print("Tiempo de refresco de la página: ");
    Serial.print(slider);
    Serial.println(" Segundos.");
  }
}

void connectMQTT(){
  while(!mqttclient.connected()){
    //Creo un idClient aleatorio
    String clientId = "stL_esp32_";
          clientId += String(random(0xffff), HEX);
          //intentamos conectar
    Serial.println("");
    Serial.println("Connecting client to : "+String(mqtt_server));
    if(mqttclient.connect(clientId.c_str(), mqtt_user, mqtt_pass)){
      Serial.println("Connected --> Ok");
      if(mqttclient.subscribe(device_topic_subscribe)){
        Serial.print("Suscripcion a topico: ");
        Serial.print(device_topic_subscribe);
        Serial.println(" --> Ok");
      }else{
        Serial.println("Suscripcion fallida");
      }
    }else{
        Serial.print("ERROR: ");
        Serial.println(mqttclient.state());
      	Serial.println(" Intentamos de nuevo en 4 segundos");
        delay(4000);
    }
  }
}