/*
    Simple STM32 Flash Loader with ESP32
*/
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>

// Pin definitions
#define RX 32
#define TX 33
#define BOOT0 27
#define NRST 26

// Wifi stuff
const char *ssid = "xxxx";
const char *pass = "xxxx";

// UART
HardwareSerial st_link(2);

WebServer server(80);

bool doflash = 0;
uint32_t CHIP_NAME;
String dosyaIsmi;

/*
    With this function,
    We wait for a response from the target device.
    Which it will determine whether operation was succeded.
*/
bool waitForAck(int sure = 5000)
{
  unsigned long basla = millis();
  while (millis() - basla < sure)
  {
    // if ((st_link.available() && st_link.read()) == 0x79)
    if (st_link.available() && st_link.read() == 0x79)
    {
      return 1;
    }
    delay(10);
  }
  return 0;
}

/*
    Here we are preparing our target device for boot.
*/
void getInBootMode()
{
  digitalWrite(BOOT0, LOW);
  digitalWrite(NRST, LOW);
  delay(100);

  digitalWrite(NRST, HIGH);
  delay(200);

  //  some buffer cleaning
  while (st_link.available())
  {
    st_link.read();
  }

  // At this stage we are going to be in BOOT Mode.
  digitalWrite(BOOT0, HIGH);
  delay(50);
  digitalWrite(NRST, LOW);
  delay(100);
  digitalWrite(NRST, HIGH);
  delay(500);
}


bool bootloader_sync()
{
  for (int i = 0; i < 5; i++)
  {
    // buffer cleaning
    while (st_link.available())
    {
      st_link.read();
    }
    // El sikismak(ack) icin
    st_link.write(0x7F);

    delay(100);
    if (waitForAck(1000))
    {
      return 1;
    }
    delay(200);
  }
  return 0;
}

/*
    This function will erase the whole flash memory.
    Hex values are specified in reference manuals.
*/
bool eraseFlashMemory()
{
  st_link.write(0x44);
  st_link.write(0xBB);

  // Wait 2 seconds for ACK or NACK
  if (!waitForAck(2000))
  {
    return 0;
  }

  // Erase whole mem
  st_link.write(0xFF);
  st_link.write(0xFF);

  st_link.write(0x00);

  // Here timeout value is 40sec due to slow flash erase operation
  return waitForAck(40000);
}

/*
  Before writing the data we must specify the address.
  So in here we select the address for writing the data.
*/
bool selectAddress(uint32_t addr)
{

  uint8_t adres[5] = {
      (addr >> 24) & 0xFF, // msb
      (addr >> 16) & 0xFF,
      (addr >> 8) & 0xFF,
      addr & 0xFF, // lsb
      0            // checksum
  };

  // To calculate the checksum, we must -xor- the bytes with eachother.
  // The target device also does that so we are sending that info for check
  adres[4] = adres[0] ^ adres[1] ^ adres[2] ^ adres[3];

  st_link.write(adres, 5);

  return waitForAck(2000);
}

/*
    We send the data with this function.
*/
bool writeData(uint32_t addr, uint8_t *d, uint8_t size)
{
  st_link.write(0x31);
  st_link.write(0xCE);

  if (!waitForAck(1000))
  {
    return 0;
  }

  // As I said before, we set the address before writing the data.
  if (!selectAddress(addr))
  {
    return 0;
  }

  // Send the size, STM waits (size - 1)
  st_link.write(size - 1);

  uint8_t checksum = size - 1;

  // send the data and also xor the value with checksum.
  for (int i = 0; i < size; i++)
  {
    st_link.write(d[i]);
    checksum ^= d[i];
  }
  // lastly we send checksum
  st_link.write(checksum);

  return waitForAck(5000);
}

bool flash()
{
  Serial.println("\nDosya flash;'a yaziliyor...");

  getInBootMode();
  if (!bootloader_sync())
  {
    Serial.println("bootloader_sync hatasi(Kablolari kontrol et )");
    return 0;
  }

  uint8_t size;
  uint8_t ad[2];

  // buffer cleaning
  while (st_link.available())
  {
    st_link.read();
  }

  if (!eraseFlashMemory())
  {
    Serial.println("Flash silinemedi");
    return 0;
  }

  File app = SPIFFS.open("/app.bin", "r");
  if (!app)
  {
    Serial.println("Dosya spiffs'te bulunamadi!!");
    return 0;
  }

  // STM32 memory mapte flashin baslangic adresi
  uint32_t adres = 0x08000000;
  uint8_t buffer[128];
  size_t total = app.size();
  size_t yazilmisVeri = 0;

  while (app.available())
  {
    size_t n = app.read(buffer, 128);
    if (n < 128)
    {
      memset(buffer + n, 0xFF, 128 - n);
      n = 128;
    }

    if (!writeData(adres, buffer, n))
    {
      Serial.println("Veri yazilamadi!!");
      app.close();
      return 0;
    }

    yazilmisVeri += n;
    adres += n;
    // Serial.println("asdsada")
    yield(); // olmayinca calismiyor watchdog ile alakali bir durum oluyormus
  }

  app.close();
  /*
    buraya kadar hatasiz gelindiyse
    flash islemi tamam ama hala bootloader modundayiz
    bu kisimda reset atip normal moda geciyoruz
  */
  digitalWrite(BOOT0, LOW);
  digitalWrite(NRST, LOW);
  delay(100);
  digitalWrite(NRST, HIGH);

  Serial.println("\nDosya basarili bir sekilde hedefin flash adresine yazildi");
  String s = "id:0x" + String(CHIP_NAME, HEX) + " dosya:" + dosyaIsmi;
  digital->save(s);
  return 0;
}
void handleRoot()
{
  String html = "<html><head><title>Gomulu proje odevi</title></head><body>";
  html += "<form method='POST' action='/yukle' enctype='multipart/form-data'>";
  html += "<input type='file' name='app'>";
  html += "<button type='submit'>Dosyayi yaz</button>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

void handleUpload()
{
  HTTPUpload &u = server.upload();
  static File f;

  if (u.status == UPLOAD_FILE_START)
  {
    SPIFFS.remove("/app.bin");
    f = SPIFFS.open("/app.bin", "w");
    // Serial.print("dddddddd");
  }
  else if (u.status == UPLOAD_FILE_WRITE)
  {
    if (f)
      f.write(u.buf, u.currentSize);
  }
  else if (u.status == UPLOAD_FILE_END)
  {
    if (f)
      f.close();
    doflash = 1;
    dosyaIsmi = u.filename;
  }
}

void handleDone()
{
  server.send(200, "text/plain", "Dosya alindi monitorden takip edin....");
}

void setup()
{
  Serial.begin(115200);

  pinMode(BOOT0, OUTPUT);
  pinMode(NRST, OUTPUT);
  digitalWrite(BOOT0, LOW);
  digitalWrite(NRST, HIGH);

  // Baglanti turu 8 data 1 even olmak zorunda
  // 8n1 yazinca patladi
  st_link.begin(115200, SERIAL_8E1, RX, TX);
  SPIFFS.begin(1);

  WiFi.begin(ssid, pass);

  Serial.println("Ag'a baglaniliyor");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }

  Serial.print("\nAga baglandi, atanan ip: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/yukle", HTTP_POST, handleDone, handleUpload);
  server.begin();
}

void loop()
{

  server.handleClient();

  // Dosyayi hemen aldiktan sonra flash islemini yapinca calismiyor?
  // loopta yapmak lazim
  if (doflash)
  {
    doflash = 0;
    delay(500);
    flash();
  }
}