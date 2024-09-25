#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>

const char* apSSID = "NasSed";    // SSID del punto de acceso
const char* apPassword = "";        // Contraseña del punto de acceso

String ssid_config = "";          // SSID de la red WiFi preferida
String password_config = "";      // Contraseña de la red WiFi preferida

File uploadFile;
WebServer server(80);  // Inicializa el servidor web en el puerto 80
const char* configFileName = "/config.json";

void setup() {
  Serial.begin(115200);

  // Inicializar la tarjeta SD
  if (!SD.begin()) {
    Serial.println("Error al montar la tarjeta SD");
    while (true);
  }

  // Leer las credenciales almacenadas en el archivo JSON
  readConfigFromFile();

  // Intentar conectar a la red WiFi si hay credenciales guardadas
  if (ssid_config.length() > 0 && password_config.length() > 0) {
    WiFi.begin(ssid_config.c_str(), password_config.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      delay(1000);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConectado a la red WiFi");
      Serial.print("Dirección IP: ");
      Serial.println(WiFi.localIP());
      WiFi.softAPdisconnect(true);  // Deshabilitar AP
    } else {
      Serial.println("\nNo se pudo conectar a la red WiFi");
      // Mantener el AP en caso de fallo de conexión
      WiFi.softAP(apSSID, apPassword);
      Serial.print("Dirección IP del AP: ");
      Serial.println(WiFi.softAPIP());
    }
  } else {
    // No hay credenciales guardadas, iniciar en modo AP
    WiFi.softAP(apSSID, apPassword);
    Serial.println("Modo punto de acceso iniciado");
    Serial.print("Dirección IP del AP: ");
    Serial.println(WiFi.softAPIP());
  }

  // Configurar rutas del servidor web sin autenticación
  server.on("/configurar", HTTP_POST, handleWiFiConfig);
  server.on("/estado", HTTP_GET, handleWiFiStatus);
  server.on("/upload", HTTP_POST, handleUpload);
  server.on("/list_files", HTTP_GET, handleListFiles);
  server.on("/delete_file", HTTP_POST, handleDeleteFile);
  server.on("/download_file", HTTP_GET, handleDownloadFile);

  server.begin();
  Serial.println("Servidor web iniciado");
}

void loop() {
  server.handleClient();
}

void handleWiFiConfig() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  if (ssid.length() > 0 && password.length() > 0) {
    ssid_config = ssid;
    password_config = password;

    WiFi.begin(ssid_config.c_str(), password_config.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      delay(1000);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      String response = "Configuración guardada y conectado a la red WiFi\n";
      response += "Dirección IP: " + WiFi.localIP().toString();
      server.send(200, "text/plain", response);
      Serial.println("\nConectado a la red WiFi");
      Serial.print("Dirección IP: ");
      Serial.println(WiFi.localIP());

      WiFi.softAPdisconnect(true);  // Deshabilitar AP

      // Guardar configuración en el archivo JSON
      saveConfigToFile();
    } else {
      server.send(200, "text/plain", "Configuración guardada pero no se pudo conectar a la red WiFi");
      Serial.println("\nError al conectar a la red WiFi");
    }
  } else {
    server.send(400, "text/plain", "Error: SSID y contraseña no pueden estar vacíos");
  }
}

void handleWiFiStatus() {
  String response;
  if (WiFi.status() == WL_CONNECTED) {
    response = "Conectado a " + String(WiFi.SSID()) + "\nDirección IP: " + WiFi.localIP().toString();
  } else {
    response = "No conectado a ninguna red WiFi";
  }
  server.send(200, "text/plain", response);
}

void handleUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    if (SD.exists(filename)) {
      SD.remove(filename);
    }
    uploadFile = SD.open(filename, FILE_WRITE);
    Serial.printf("Subiendo archivo: %s\n", filename.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      server.send(200, "text/plain", "Archivo subido con éxito!");
      Serial.println("Carga de archivo completada");
    } else {
      server.send(500, "text/plain", "Error al subir el archivo");
      Serial.println("Error al cargar el archivo");
    }
  }
}

void handleListFiles() {
  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    server.send(500, "text/plain", "Error al abrir el directorio");
    return;
  }

  String fileList = "[";
  File file = root.openNextFile();
  while (file) {
    if (fileList.length() > 1) {
      fileList += ",";
    }
    fileList += "{\"name\":\"";
    fileList += file.name();
    fileList += "\",\"size\":";
    fileList += file.size();
    fileList += "}";
    file = root.openNextFile();
  }
  fileList += "]";
  server.send(200, "application/json", fileList);
}

void handleDeleteFile() {
  if (!server.hasArg("filename")) {
    server.send(400, "text/plain", "Nombre de archivo no especificado");
    return;
  }

  String filename = "/" + server.arg("filename");
  if (!SD.exists(filename)) {
    server.send(404, "text/plain", "Archivo no encontrado");
    return;
  }

  Serial.printf("Eliminando archivo: %s\n", filename.c_str());
  if (SD.remove(filename)) {
    server.send(200, "text/plain", "Archivo eliminado");
    Serial.println("Eliminación de archivo exitosa");
  } else {
    server.send(500, "text/plain", "Error al eliminar el archivo");
    Serial.println("Error al eliminar el archivo");
  }
}

void handleDownloadFile() {
  if (!server.hasArg("filename")) {
    server.send(400, "text/plain", "Nombre de archivo no especificado");
    return;
  }

  String filename = "/" + server.arg("filename");
  if (!SD.exists(filename)) {
    server.send(404, "text/plain", "Archivo no encontrado");
    return;
  }

  Serial.printf("Descargando archivo: %s\n", filename.c_str());
  File downloadFile = SD.open(filename, FILE_READ);
  server.streamFile(downloadFile, "application/octet-stream");
  downloadFile.close();
  Serial.println("Descarga de archivo completada");
}

void saveConfigToFile() {
  StaticJsonDocument<512> doc;
  
  doc["ssid"] = ssid_config;
  doc["password"] = password_config;

  File configFile = SD.open(configFileName, FILE_WRITE);
  if (!configFile) {
    Serial.println("Error al abrir el archivo de configuración para escribir");
    return;
  }

  if (serializeJson(doc, configFile) == 0) {
    Serial.println(F("Error al escribir en el archivo"));
  }
  configFile.close();
}

void readConfigFromFile() {
  File configFile = SD.open(configFileName, FILE_READ);
  if (!configFile) {
    Serial.println("No se encontró el archivo de configuración, usando valores predeterminados");
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, configFile);
  if (error) {
    Serial.print(F("Error al leer el archivo: "));
    Serial.println(error.c_str());
  }

  ssid_config = doc["ssid"].as<String>();
  password_config = doc["password"].as<String>();

  configFile.close();
}
