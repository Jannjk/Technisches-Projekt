#include <Arduino.h>
#include <MFRC522.h>
#include <SPI.h>
#include "headers/constants.h"
#include <WiFiS3.h>
#include <IRremote.hpp>
#include <IRremoteInt.h>
#include <IRProtocol.h>
#include <EEPROM.h>
#include <ArduinoHttpClient.h>
#include <array>
#include <vector>
#include <ArduinoJson.h>


//Var Def
int bewegungSensor = 0;// Bewegungssensor
MFRC522 rfid(SS_PIN, RST_PIN); //RFID reader
// WIFI
const char* ssidHS = "nao";
const char* passwordHS = "HsPfNaoH25V4";
const char* serverAdress = "192.168.2.175";


//Keycards
std::vector<String> auhorizedCards;

unsigned int savedCardCount = 0;

//Wifi client
const int port = 8090;
const char* ssid = "Wifi-Name";
const char* password = "Wifi-Passwort";
WiFiClient wifi;
HttpClient client = HttpClient(wifi, serverAdress, port);



//IRremote
IRrecv irrecv(4); // Pin 4 für IR receiver
decode_results results;
IRData irData; // IR data structure

String pinInput = "";
const String correctPIN = "1234";



//warten
unsigned long waitStartTime = 0;
bool waiting = false; 

//State Variables
bool bewegungsState = false; 
bool unlocked = false;
bool adminMode = false;
bool preAdminMode = false;

//function Definitions
void readCard(); // RFID Funktion
bool checkBewegegung(); // Bewegungssensor Funktion
void alarm(); // Alarm Funktion
bool isCardAuthorized(String cardUID); 
void unlock();
void unlockSound();
void warten(int seconds);
int getNumberFromCode(uint32_t code); // Helper Funktion
void enterPin(); // PIN Funktion
std::vector<String> getSavedCards();
bool savedCardsAdd(String cardId);
bool arrayContains(std::vector<String> arr, String value);




void setup() {

  Serial.begin(9600);
  //RFID Setup
  SPI.begin(); // Init SPI bus
  rfid.PCD_Init(); // Init MFRC522



  //pin setup
  pinMode(LED_PIN, OUTPUT); 
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT); 
  Serial.println("RFID Reader initialized.");

  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  
  WiFiClient testClient;
  Serial.print("Testing basic connectivity to server...");
  if (testClient.connect(serverAdress, port)) {
      Serial.println("SUCCESS!");
      testClient.stop();
  } else {
      Serial.println("FAILED!");
  }

  Serial.println("Connected to WiFi!");
  
  
  
  irrecv.enableIRIn(); // Start the receiver
  auhorizedCards = getSavedCards();
  Serial.println(auhorizedCards.front());
  
 
  
}

void loop() {
   // Debugging Keypad
  bool prevState = bewegungsState;//Überprüfung Änderung
  checkBewegegung();
  readCard(); 
  if(!prevState && bewegungsState && !unlocked){
    alarm(); // Alarm auslösen
  }

  enterPin();  
}
  //runServer(); 
  bool savedCardsAdd(String cardId) {
    // Check WiFi status first
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi not connected. Attempting to reconnect...");
      WiFi.begin(ssid, password);
      
      // Wait up to 10 seconds for connection
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 10) {
        delay(1000);
        Serial.print(".");
        attempts++;
      }
      
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Failed to reconnect WiFi!");
        return false;
      } else {
        Serial.println("WiFi reconnected!");
      }
    }
  
    // Set timeout and prepare request
    client.setTimeout(15000); // 15 seconds timeout
    
    Serial.println("Adding card to database: " + cardId);
    Serial.print("Connecting to server at ");
    Serial.print(serverAdress);
    Serial.print(":");
    Serial.println(port);
    
    // Construct JSON payload
    String jsonPayload = "{\"Card_number\":\"" + cardId + "\"}";
    Serial.print("JSON payload: ");
    Serial.println(jsonPayload);
    
    // Begin HTTP request
    client.beginRequest();
    client.post("/api/collections/Cards/records");
    client.sendHeader("Content-Type", "application/json");
    client.sendHeader("Accept", "application/json");
    client.sendHeader("Content-Length", jsonPayload.length());
    client.beginBody();
    client.print(jsonPayload);
    client.endRequest();
    
    // Check response status
    int statusCode = client.responseStatusCode();
    Serial.print("Status code: ");
    Serial.println(statusCode);
    
    if (statusCode > 0) {
      // We got a valid HTTP status
      if (statusCode == 200 || statusCode == 201) {
        Serial.println("Card successfully added to database!");
        String response = client.responseBody();
        Serial.print("Response: ");
        Serial.println(response);
        return true;
      } else {
        Serial.print("Server error: ");
        Serial.println(statusCode);
        String errorResponse = client.responseBody();
        Serial.print("Error response: ");
        Serial.println(errorResponse);
        return false;
      }
    } else {
      // Negative status code indicates a client-side error
      Serial.print("Connection error: ");
      Serial.println(statusCode);
      
      switch (statusCode) {
        case -1: Serial.println("Connection failed"); break;
        case -2: Serial.println("API endpoint not found"); break;
        case -3: Serial.println("Connection timed out"); break;
        case -4: Serial.println("Invalid response"); break;
        default: Serial.println("Unknown error"); break;
      }
      
      // Try a simple ping or connection test
      Serial.println("Attempting basic TCP connection to server...");
      WiFiClient testClient;
      if (testClient.connect(serverAdress, port)) {
        Serial.println("Basic TCP connection successful!");
        testClient.stop();
      } else {
        Serial.println("Basic TCP connection failed!");
      }
      
      return false;
    }
  }

bool arrayContains(std::vector<String> arr, String value) {
    for (const auto& item : arr) {
        if (item == value) {
            return true;
        }
    }
    return false;
}

std::vector<String> getSavedCards(){
    String response = "";
    std::vector<String> cards;

    client.setTimeout(3000);

    client.beginRequest();
    client.get("/api/collections/Cards/records");
    
    // Set headers
    client.sendHeader("Content-Type", "application/json");
    client.sendHeader("Accept", "application/json");
    
    // Send the request and finish it
    client.endRequest();
    
    // Now we can check the response status
    int statusCode = client.responseStatusCode();
    Serial.print("Status code: ");
    Serial.println(statusCode);
    
    // Only try to read the response body if we got a successful status code
    if(statusCode == 200) {
      response = client.responseBody();
      if(response.length() == 0) {
        Serial.println("No response body received.");
        return cards;
      }
      const size_t capacity = JSON_ARRAY_SIZE(10) + 10*JSON_OBJECT_SIZE(7) + 500;
    
      // Create a DynamicJsonDocument
      DynamicJsonDocument doc(capacity);
      
      // Deserialize the JSON string
      DeserializationError error = deserializeJson(doc, response);
      
      // Check for parsing errors
      if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        
      }
      
      // Access the "items" array
      JsonArray items = doc["items"];
      
      // Print the number of cards found
      
      // Iterate through each item and extract the Card_number
      for (int i = 0; i < items.size(); i++) {
        JsonObject item = items[i];
        const char* cardNumber = item["Card_number"];
       
        
        // Here you can do additional processing with each card number
        // For example, store them in an array, check against a list, etc.
        //add card number to array 
        cards.push_back(String(cardNumber));
        
      }
      return cards;
     
    } else {
      Serial.print("Error: ");
      Serial.println(statusCode);
      return cards;
    }
   
}

void enterPin(){
  if (IrReceiver.decode()) {
    uint32_t code = IrReceiver.decodedIRData.decodedRawData;
    int number = getNumberFromCode(code);
  
    if (number != -1) {
        if (pinInput.length() < 4) {
            pinInput += String(number);
            Serial.print("PIN so far: ");
            Serial.println(pinInput);
        } else {
            Serial.println("PIN already has 4 digits. Press OK or clear.");
        }
    }

    // 
    if (code == 0xBA45FF00) {
        if (pinInput == correctPIN && !adminMode && !preAdminMode) {
            Serial.println("✅ Correct PIN entered!");
            unlock();
          
        
        } else if(pinInput == "0000" && !adminMode && !preAdminMode){
          Serial.println("preAdmin mode activated.");
          preAdminMode = true;
        }
        else if(pinInput == correctPIN && preAdminMode){
          adminMode = true;
          Serial.println("Admin mode activated.");
        }
        else {
            Serial.println("❌ Incorrect PIN.");
        }
        pinInput = "";  
    }

    IrReceiver.resume();
    }
  // Check if the button is pressed
}

void readCard(){
  if ( !rfid.PICC_IsNewCardPresent()){
    return;
  }
  // Select one of the cards
  if ( !rfid.PICC_ReadCardSerial()){
    return;
  }
  //Show UID on serial monitor
  Serial.print("UID tag :");
  String content= "";
  byte letter;

  for (byte i = 0; i < rfid.uid.size; i++){
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(rfid.uid.uidByte[i], HEX);
    content.concat(String(rfid.uid.uidByte[i] < 0x10 ? " 0" : " "));
    content.concat(String(rfid.uid.uidByte[i], HEX));
  }
  Serial.println();
  Serial.print("Message : ");
  content.toUpperCase();
  content.replace(" ", "");

  if(adminMode){
    if(!arrayContains(auhorizedCards, content)){
      auhorizedCards.push_back(content);
      savedCardsAdd(content);
    }
    //print all saved cards
    Serial.println("Saved cards:");
    Serial.println(auhorizedCards.size());
    adminMode = false;
    preAdminMode = false;
    
  }

  if(isCardAuthorized(content) && !adminMode){
    unlock();
  }
 
}

bool checkBewegegung(){
  bewegungSensor = digitalRead(PIR_PIN);

  if(bewegungSensor == HIGH){
    //Serial.println("Bewegung erkannt!"); // Bewegung erkannt
    //digitalWrite(BUZZER_PIN, HIGH); 
    //tone(BUZZER_PIN, 1000, 100);  
    if(!bewegungsState){
      bewegungsState = true; // Bewegung erkannt
      Serial.print("BewegungsState: ");
      Serial.println(bewegungsState);
    }
    return true; 
  }
  else{ 
    if(bewegungsState){
      bewegungsState = false;
      Serial.print("BewegungsState: ");
      Serial.println(bewegungsState);
    }
    return false; 
  }
  delay(100);
}

void alarm() {
  for (int i = 0; i < 5; i++) {//Wiederhole 5 mal
      tone(BUZZER_PIN, 1000, 500); // 1000Hz für 500ms
      delay(600); 
      tone(BUZZER_PIN, 1500, 500); // 1500Hz für 500ms
      delay(600);
  }
  noTone(BUZZER_PIN); // Stop 
}

void unlock(){

  unlocked = true;
  unlockSound();
  //mehr logik zum auffschliessen
  pinInput = "";
  //nach 20 sekunden wieder verriegeln
}

bool isCardAuthorized(String cardUID) {
  for (int i = 0; i < NUM_CARDS; i++) {
    if (cardUID == auhorizedCards[i]) {
      Serial.println("Karte erkannt!");
      return true;
    }
  }
  return false;
}

void unlockSound(){
  tone(BUZZER_PIN, 400, 100); // Low tone
  delay(120);
  tone(BUZZER_PIN, 600, 100); // Medium tone
  delay(120);
  tone(BUZZER_PIN, 800, 100); // Higher tone
  delay(120);
  tone(BUZZER_PIN, 1000, 150); // Final high tone
  delay(150);
  noTone(BUZZER_PIN); // Stop the ton
}

void warten(int seconds){

  if(unlocked){
    if(!waiting){
      waitStartTime = millis(); // Start the timer
      waiting = true; // Set the waiting state to true
    }
    if(waiting && (millis() - waitStartTime >= seconds * 1000)){
      unlocked = false; // Reset the unlocked state after the wait time
      waiting = false; // Reset the waiting state
      Serial.println("Schloss zurueckgesetzt.");
    }
  }
  
}



int getNumberFromCode(uint32_t code) {
  switch (code) {
      case 0xBA45FF00: return 30;
      case 0xE916FF00: return 0;
      case 0xF30CFF00: return 1;
      case 0xE718FF00: return 2;
      case 0xA15EFF00: return 3;
      case 0xF708FF00: return 4;
      case 0xE31CFF00: return 5;
      case 0xA55AFF00: return 6;
      case 0xBD42FF00: return 7;
      case 0xAD52FF00: return 8;
      case 0xB54AFF00: return 9;
      default: return -1;  // Unknown button
  }
}