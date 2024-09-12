/**
 * @file tracking.cpp
 * @author Gideon Maina
 * @brief Implement GPS tracking
 * @version 0.1
 * @date 2024-08-22
 *
 * @copyright Copyright (c) 2024
 *
 * This test was performed using a NodeMCU Lolin v3 with a CH340G driver, a Quectel EC200U GSM module
 * and NEO-6M GPS module. GPS data is pushed to a Google firebase real-time database
 *
 *
 */

#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "secrets.h"

/* Put your SSID & Password */
const char *ssid = AP_SSID;    // Enter SSID here
const char *password = AP_PWD; // Enter Password here

/* Put IP Address details */
IPAddress local_ip(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

ESP8266WebServer server(80);
TinyGPSPlus gps;

#define MCU_RXD D5 // GSM TX
#define MCU_TXD D6 // GSM RX
#define GPS_TXD D1 // GPS TX
#define GPS_RXD D2 // GPS RX
#define MESSAGE_BUFFER_SIZE 4097
char msgStream[MESSAGE_BUFFER_SIZE];
SoftwareSerial GSM_Serial(MCU_RXD, MCU_TXD);
SoftwareSerial GPS_Serial(GPS_TXD, GPS_RXD);
String CLOUD_URL = FIREBASE_URL;

// Function declarations

void read_serial(SoftwareSerial *softSerial, char *buffer);
void sendATcommand(SoftwareSerial *softSerial, String CMD, long unsigned int timeout = 4000, bool fill_buffer = false);
void cleanSerial(SoftwareSerial *softSerial);
void enableGPRS();
void PUT_REQUEST(const String &data);
void gps_encode();
String SendHTML(String _body = "");
void display_logs();
void handle_OnConnect();
void sys_restart();
void gps_status_send();
void handle_NotFound();
double _lat = 0, _long = 0, _prevLat = 0, _prevLong = 0, _newLat = 0, _newLong = 0;
unsigned int last_gps_read = 0;
bool isLocationUpdated = false;

void setup()
{

    Serial.begin(115200);
    GPS_Serial.begin(9600);
    GSM_Serial.begin(115200);
    WiFi.softAP(ssid, password);
    WiFi.softAPConfig(local_ip, gateway, subnet);
    delay(1000);
    server.on("/", gps_status_send);
    server.on("/logs", display_logs);
    server.on("/restart", sys_restart);
    server.onNotFound(handle_NotFound);
    server.begin();
    Serial.println("HTTP server started");

    delay(2000); // Allow some delay for you to open the serial monitor
    sendATcommand(&GSM_Serial, "AT");
    sendATcommand(&GSM_Serial, "AT+QIACT=0");
    sendATcommand(&GSM_Serial, "AT+CGATT=0");
    sendATcommand(&GSM_Serial, "AT+CFUN=1,1");
    delay(30000);
    enableGPRS();
}

void loop()
{

    server.handleClient();
    if (GPS_Serial.available() && (millis() - last_gps_read) > 10000)
    {
        read_serial(&GPS_Serial, msgStream);
        gps_encode();
        last_gps_read = millis();
    }
}

/**
 * @brief - read serial data and put it in a buffer
 * @param softSerial: pointer to SoftwareSerial object
 * @param buffer: pointer to char array
 */
void read_serial(SoftwareSerial *softSerial, char *buffer)
{
    bool bufferfull = false;
    int buff_pos = 0;
    memset(buffer, '\0', MESSAGE_BUFFER_SIZE);
    while (softSerial->available() > 0 && !bufferfull)
    {
        unsigned char c = softSerial->read();
        // Serial.write(c);
        if (buff_pos == MESSAGE_BUFFER_SIZE - 1)
        {
            bufferfull = true;
            Serial.println("\nBuffer full");
            break;
        }
        buffer[buff_pos] = c;
        buff_pos++;
        delay(2); // ? This seems to be the trick to get all serial data if it comes in chunks
    }

    buffer[buff_pos] = '\0'; // ? Unecessary if memset is used
}

void sendATcommand(SoftwareSerial *softSerial, String CMD, long unsigned int _timeout, bool fill_buffer)
{
    Serial.println("*********");
    bool ASSERT_BUFFER = fill_buffer;
    cleanSerial(softSerial);

    softSerial->println(CMD);
    unsigned int send_time = millis();

    do
    {

        if (softSerial->available())
        {
            read_serial(softSerial, msgStream);
        }
    } while ((millis() - send_time) < _timeout || ASSERT_BUFFER);

    const char *ptr = msgStream;
    while (*ptr)
    {

        Serial.write(*ptr);
        ptr++;
    }
    display_logs();
}

void cleanSerial(SoftwareSerial *softSerial)
{

    while (Serial.available())
    {
        Serial.read();
    }
    while (softSerial->available())
    {
        softSerial->read();
    }
}

void enableGPRS()
{

    Serial.println("ENABLING GPRS >>>>>>");
    String url = "AT+QHTTPCFG=\"url\",\"";
    url += CLOUD_URL;
    url += "\"";
    sendATcommand(&GSM_Serial, "AT+CGATT=1");
    sendATcommand(&GSM_Serial, "AT+QICSGP=1,1");
    sendATcommand(&GSM_Serial, "AT+QIACT=1");
    sendATcommand(&GSM_Serial, "AT+QHTTPCFG=\"sslctxid\",1");
    sendATcommand(&GSM_Serial, url);
    sendATcommand(&GSM_Serial, "AT+QHTTPCFG=\"contextid\",1");
    sendATcommand(&GSM_Serial, "AT+QHTTPCFG=\"responseheader\",1");
    sendATcommand(&GSM_Serial, "AT+QHTTPCFG=\"rspout/auto\",1");
    sendATcommand(&GSM_Serial, "AT+QHTTPCFG=\"header\",\"Content-Type: application/json\"");
}

void PUT_REQUEST(const String &data)
{

    unsigned int data_length = data.length();
    String HTTPCFG = "AT+QHTTPPUT=" + String(data_length) + ",30,60";
    Serial.println(HTTPCFG);
    Serial.println(data);
    sendATcommand(&GSM_Serial, HTTPCFG);
    sendATcommand(&GSM_Serial, data);
}

void gps_encode()
{

    const char *ptr = msgStream;
    // double _lat = 0, _long = 0;
    while (*ptr)
    {
        gps.encode(*ptr);
        Serial.write(*ptr);
        ptr++;
        // if (*ptr == '\0') // uncoment to debug
        // {
        //   Serial.println("Null character found");
        // }
    }
    if (gps.location.isValid()) // gprmc data seems to do nothing
    {
        _lat = gps.location.lat();
        _long = gps.location.lng();
        Serial.print("\nLatitude= ");
        Serial.print(_lat, 9);
        Serial.print(" Longitude= ");
        Serial.println(_long, 9);
    }
    // if (gps.location.isUpdated()) // gprmc data seems to do nothing
    // {

    //     _newLat = gps.location.lat();
    //     _newLong = gps.location.lng();
    //     Serial.print("\nLocation Updated to: ");
    //     Serial.print("Latitude= ");
    //     Serial.print(_lat);
    //     Serial.print(" Longitude= ");
    //     Serial.println(_long);

    //     String gps_update = "{\"lat\":" + String(_lat, 9) + ",\"long\":" + String(_long, 9) + "}";
    //     PUT_REQUEST(gps_update);
    // }
    if (_lat != _newLat || _long != _newLong)
    {
        _newLat = _lat;
        _newLong = _long;
        isLocationUpdated = true;
        Serial.print("\nLocation Updated to: ");
        Serial.print("Latitude= ");
        Serial.print(_newLat, 9);
        Serial.print(" Longitude= ");
        Serial.println(_newLong, 9);

        String gps_update = "{\"lat\":" + String(_newLat, 9) + ",\"long\":" + String(_newLong, 9) + "}";
        PUT_REQUEST(gps_update);
    }

    else
    {
        _prevLat = _newLat;
        _prevLong = _newLong;
        isLocationUpdated = false;
    }
}

void gps_status_send()
{

    Serial.println("Sending GPS data");
    String data = "<h1>GPS COORDS</h1>\n";
    if (_lat != 0 && _long != 0)
    {
        data += "<p>Current Latitude: " + String(_lat, 9) + "</P>\n";
        data += "<p>Current Longitude: " + String(_long, 9) + "</P>\n";
    }

    if (isLocationUpdated)
    {
        data += "<div style=\"padding:4px;border: 1px solid green;word-wrap:break-word;\">";
        data += "<p>Updated latitude FROM: " + String(_prevLat, 9) + " TO: " + String(_newLat, 9) + "</P>\n";
        data += "<p>Updated longtitude FROM: " + String(_prevLong, 9) + " TO: " + String(_newLong, 9) + "</P>\n";
        data += "</div>\n";
    }
    else
    {
        data += "<p> GPS location not updated</p>\n";
    }

    server.send(200, "text/html", SendHTML(data));
}

String SendHTML(String _body)
{
    String ptr = "<!DOCTYPE html><html>\n ";
    ptr += "<head><meta name='viewport' content='width=device-width, initial-scale=1.0' /><title>GPS TRACKER</title></head>\n";
    ptr += "<body>\n";
    ptr += "<style>\n";
    ptr += "body{margin-top:50px;display:flex;flex-direction:column;padding:1rem;align-items:center}h1,h3{color:#2f2d2d;margin:1rem auto}";
    ptr += "a,a:active,a:hover,a:visited{text-decoration:none;font-size:32px;color:#15ad8f}p{font-size:1rem;color:#3a3838;margin:12px auto}";
    ptr += "button{padding:.5rem 1rem;outline:0;border-radius:5px;background-color:#0ba485;border:0;cursor:pointer;color:#fff;font-size:24px}";
    ptr += "</style>\n";
    ptr += "<h3><i>Webserver in Access Point (AP) Mode</i></h3>\n";
    ptr += "<div style='display: flex; gap: 1rem'><a href='/'>Home</a> <a href='/logs'>Serial logs</a></div>\n";

    if (_body)
    {

        ptr += _body;
    }
    else
    {
        ptr += "<p> NOTHING TO SHOW</p>";
    }

    ptr += "<a href='/restart'><button>RESTART</button></a>\n";
    ptr += "</body>\n";
    ptr += "</html>\n";
    return ptr;
}

void handle_OnConnect()
{
    Serial.println("Connecting to homepage");
    server.send(200, "text/html", SendHTML());
}

void display_logs()
{
    String body = "<h1>Serial logs</h1>\n";
    body += "<div style=\"margin:8px 4px;border:1px solid red; padding: 4px\">\n";
    body += "<p>" + String(msgStream) + "</p>";
    body += "</div>\n";
    server.send(200, "text/html", SendHTML(body));
}

void sys_restart()
{
    Serial.println("Restarting system");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    server.send(200, "text/html", SendHTML("Restarting ..."));

    delay(5000);
    ESP.restart();
    // should not be reached
    while (true)
    {
        yield();
    }
}
void handle_NotFound()
{
    server.send(404, "text/plain", "Not found");
}
