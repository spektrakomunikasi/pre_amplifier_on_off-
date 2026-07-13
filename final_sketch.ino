#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <time.h>
#include <Adhan.h>

#define I2C_SDA D2
#define I2C_SCL D1
#define RELAY_PIN D6

#define RELAY_ON_LEVEL  HIGH
#define RELAY_OFF_LEVEL LOW

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;
ESP8266WebServer server(80);

enum CalcMethodId {
  CM_MWL = 0,
  CM_EGYPT = 1,
  CM_KARACHI = 2,
  CM_UMM_AL_QURA = 3,
  CM_DUBAI = 4,
  CM_MOON_SIGHTING = 5,
  CM_NORTH_AMERICA = 6,
  CM_KUWAIT = 7,
  CM_QATAR = 8,
  CM_SINGAPORE = 9,
  CM_TEHRAN = 10,
  CM_TURKEY = 11
};

struct Config {
  uint32_t magic;
  char ssid[32];
  char pass[32];
  float lat;
  float lon;
  int tz;
  int beforeMin[5];
  int afterMin[5];
  uint8_t calcMethod;
  uint8_t lcdAddr;
};

Config cfg;
const uint32_t CFG_MAGIC = 0x5A7B9C11;
const int EVENT_COUNT = 10;
int eventHour[EVENT_COUNT];
int eventMinute[EVENT_COUNT];
bool eventActionOn[EVENT_COUNT];
bool eventExecuted[EVENT_COUNT];
int prayerHour[5];
int prayerMinute[5];

int lastY=-1,lastM=-1,lastD=-1;
unsigned long lastLcdMs=0,lastRtcSyncMs=0;
const char* prayerName[5] = {"Subuh","Dzuhur","Ashar","Maghrib","Isya"};

void relaySet(bool on){ digitalWrite(RELAY_PIN, on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL); }
void normalizeHM(int totalMin,int &h,int &m){ while(totalMin<0) totalMin+=1440; totalMin%=1440; h=totalMin/60; m=totalMin%60; }
void resetEventExecuted(){ for(int i=0;i<EVENT_COUNT;i++) eventExecuted[i]=false; }
void saveConfig(){ cfg.magic=CFG_MAGIC; EEPROM.put(0,cfg); EEPROM.commit(); }

void setDefaultConfig(){
  memset(&cfg,0,sizeof(cfg));
  cfg.magic=CFG_MAGIC;
  cfg.lat=-6.1783; cfg.lon=106.6319; cfg.tz=7;
  for(int i=0;i<5;i++){ cfg.beforeMin[i]=15; cfg.afterMin[i]=25; }
  cfg.calcMethod=CM_SINGAPORE;
  cfg.lcdAddr=0x27;
}

void loadConfig(){
  EEPROM.get(0,cfg);
  if(cfg.magic!=CFG_MAGIC){ setDefaultConfig(); saveConfig(); }
  if(cfg.lcdAddr!=0x27 && cfg.lcdAddr!=0x3F) cfg.lcdAddr=0x27;
}

CalculationParameters getCalcParams(uint8_t id){
  switch(id){
    case CM_EGYPT: return CalculationMethod::Egyptian();
    case CM_KARACHI: return CalculationMethod::Karachi();
    case CM_UMM_AL_QURA: return CalculationMethod::UmmAlQura();
    case CM_DUBAI: return CalculationMethod::Dubai();
    case CM_MOON_SIGHTING: return CalculationMethod::MoonsightingCommittee();
    case CM_NORTH_AMERICA: return CalculationMethod::NorthAmerica();
    case CM_KUWAIT: return CalculationMethod::Kuwait();
    case CM_QATAR: return CalculationMethod::Qatar();
    case CM_SINGAPORE: return CalculationMethod::Singapore();
    case CM_TEHRAN: return CalculationMethod::Tehran();
    case CM_TURKEY: return CalculationMethod::Turkey();
    default: return CalculationMethod::MuslimWorldLeague();
  }
}

String methodName(uint8_t id){
  switch(id){
    case CM_EGYPT:return "Egypt"; case CM_KARACHI:return "Karachi"; case CM_UMM_AL_QURA:return "UmmAlQura";
    case CM_DUBAI:return "Dubai"; case CM_MOON_SIGHTING:return "MoonSighting"; case CM_NORTH_AMERICA:return "NorthAmerica";
    case CM_KUWAIT:return "Kuwait"; case CM_QATAR:return "Qatar"; case CM_SINGAPORE:return "Singapore";
    case CM_TEHRAN:return "Tehran"; case CM_TURKEY:return "Turkey"; default:return "MWL";
  }
}

void computeEventsForDate(int year,int month,int day){
  Coordinates coordinates(cfg.lat,cfg.lon);
  DateComponents date(year,month,day);
  CalculationParameters params=getCalcParams(cfg.calcMethod);
  params.madhab=Madhab::Shafi;
  PrayerTimes p(coordinates,date,params);

  int base[5]={
    p.fajr.hour*60+p.fajr.minute,
    p.dhuhr.hour*60+p.dhuhr.minute,
    p.asr.hour*60+p.asr.minute,
    p.maghrib.hour*60+p.maghrib.minute,
    p.isha.hour*60+p.isha.minute
  };

  for(int i=0;i<5;i++){ prayerHour[i]=base[i]/60; prayerMinute[i]=base[i]%60; }
  for(int i=0;i<5;i++){ int tOn=base[i]-cfg.beforeMin[i]; normalizeHM(tOn,eventHour[i],eventMinute[i]); eventActionOn[i]=true; }
  for(int i=0;i<5;i++){ int tOff=base[i]+cfg.afterMin[i]; normalizeHM(tOff,eventHour[i+5],eventMinute[i+5]); eventActionOn[i+5]=false; }
  resetEventExecuted();
}

void syncRTCFromNTPIfConnected(){
  if(WiFi.status()!=WL_CONNECTED) return;
  configTime(cfg.tz*3600,0,"pool.ntp.org","time.nist.gov");
  struct tm t;
  if(getLocalTime(&t,2000)){
    DateTime dt(t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec);
    rtc.adjust(dt);
  }
}

String optSel(int value,int current,const String& label){ return "<option value='"+String(value)+"' "+String(value==current?"selected":"")+">"+label+"</option>"; }

String pageRoot(){
  String s="<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Setup</title></head><body>";
  s+="<h3>Sholat Relay v1.1 (Tangerang 15159 default)</h3><form method='POST' action='/save'>";
  s+="SSID:<br><input name='ssid' value='"+String(cfg.ssid)+"'><br>Password:<br><input name='pass' value='"+String(cfg.pass)+"'><br><br>";
  s+="Latitude:<br><input name='lat' value='"+String(cfg.lat,6)+"'><br>Longitude:<br><input name='lon' value='"+String(cfg.lon,6)+"'><br>Timezone UTC:<br><input name='tz' value='"+String(cfg.tz)+"'><br><br>";
  s+="Metode Hisab:<br><select name='cm'>";
  s+=optSel(CM_SINGAPORE,cfg.calcMethod,"Singapore"); s+=optSel(CM_MWL,cfg.calcMethod,"MuslimWorldLeague"); s+=optSel(CM_KARACHI,cfg.calcMethod,"Karachi");
  s+=optSel(CM_EGYPT,cfg.calcMethod,"Egypt"); s+=optSel(CM_UMM_AL_QURA,cfg.calcMethod,"UmmAlQura"); s+=optSel(CM_DUBAI,cfg.calcMethod,"Dubai");
  s+=optSel(CM_MOON_SIGHTING,cfg.calcMethod,"MoonSighting"); s+=optSel(CM_NORTH_AMERICA,cfg.calcMethod,"NorthAmerica"); s+=optSel(CM_KUWAIT,cfg.calcMethod,"Kuwait");
  s+=optSel(CM_QATAR,cfg.calcMethod,"Qatar"); s+=optSel(CM_TEHRAN,cfg.calcMethod,"Tehran"); s+=optSel(CM_TURKEY,cfg.calcMethod,"Turkey"); s+="</select><br><br>";
  s+="LCD Address:<br><select name='lcd'><option value='39' "+String(cfg.lcdAddr==0x27?"selected":"")+">0x27</option><option value='63' "+String(cfg.lcdAddr==0x3F?"selected":"")+">0x3F</option></select><br><br>";
  for(int i=0;i<5;i++){ s+="<b>"+String(prayerName[i])+"</b><br>Before(min): <input name='b"+String(i)+"' value='"+String(cfg.beforeMin[i])+"'><br>After(min): <input name='a"+String(i)+"' value='"+String(cfg.afterMin[i])+"'><br><br>"; }
  s+="<button type='submit'>Simpan & Restart</button></form><br><a href='/schedule'>Lihat jadwal hari ini</a><br><a href='/status'>Status JSON</a><br><a href='/setrtc'>Set RTC dari jam HP</a></body></html>";
  return s;
}

void handleRoot(){ server.send(200,"text/html",pageRoot()); }
void handleSave(){
  if(server.hasArg("ssid")) strncpy(cfg.ssid,server.arg("ssid").c_str(),sizeof(cfg.ssid)-1);
  if(server.hasArg("pass")) strncpy(cfg.pass,server.arg("pass").c_str(),sizeof(cfg.pass)-1);
  if(server.hasArg("lat")) cfg.lat=server.arg("lat").toFloat();
  if(server.hasArg("lon")) cfg.lon=server.arg("lon").toFloat();
  if(server.hasArg("tz")) cfg.tz=server.arg("tz").toInt();
  if(server.hasArg("cm")) cfg.calcMethod=(uint8_t)server.arg("cm").toInt();
  if(server.hasArg("lcd")) cfg.lcdAddr=(uint8_t)server.arg("lcd").toInt();
  for(int i=0;i<5;i++){ if(server.hasArg("b"+String(i))) cfg.beforeMin[i]=server.arg("b"+String(i)).toInt(); if(server.hasArg("a"+String(i))) cfg.afterMin[i]=server.arg("a"+String(i)).toInt(); }
  saveConfig(); server.send(200,"text/html","<h3>Tersimpan, restart...</h3>"); delay(700); ESP.restart();
}

void handleSchedule(){
  DateTime n=rtc.now();
  String s="<html><body><h3>Jadwal Hari Ini</h3>Tanggal: "+String(n.day())+"/"+String(n.month())+"/"+String(n.year())+"<br>Metode: "+methodName(cfg.calcMethod)+"<br><br>";
  s+="<table border='1' cellpadding='4'><tr><th>Waktu</th><th>Adzan</th><th>Relay ON</th><th>Relay OFF</th></tr>";
  for(int i=0;i<5;i++){ char adz[6],onb[6],offb[6]; sprintf(adz,"%02d:%02d",prayerHour[i],prayerMinute[i]); sprintf(onb,"%02d:%02d",eventHour[i],eventMinute[i]); sprintf(offb,"%02d:%02d",eventHour[i+5],eventMinute[i+5]); s+="<tr><td>"+String(prayerName[i])+"</td><td>"+String(adz)+"</td><td>"+String(onb)+"</td><td>"+String(offb)+"</td></tr>"; }
  s+="</table><br><a href='/'>Kembali</a></body></html>";
  server.send(200,"text/html",s);
}

void handleStatus(){
  DateTime n=rtc.now();
  String j="{";
  j += "\"dt\":\""+String(n.timestamp())+"\",";
  j += "\"method\":\""+methodName(cfg.calcMethod)+"\",";
  j += "\"relay\":\""+String(digitalRead(RELAY_PIN)==RELAY_ON_LEVEL?"ON":"OFF")+"\",";
  j += "\"ap_ip\":\""+WiFi.softAPIP().toString()+"\",";
  j += "\"sta_ip\":\""+WiFi.localIP().toString()+"\"";
  j += "}";
  server.send(200,"application/json",j);
}

void handleSetRtcPage(){ String s="<html><body><button onclick='go()'>Set RTC dari jam HP</button><script>function go(){location.href='/setrtc_do?e='+Math.floor(Date.now()/1000);}</script></body></html>"; server.send(200,"text/html",s); }
void handleSetRtcDo(){ if(!server.hasArg("e")){ server.send(400,"text/plain","epoch required"); return; } uint32_t e=(uint32_t)server.arg("e").toInt(); e += cfg.tz*3600; rtc.adjust(DateTime(e)); server.send(200,"text/plain","RTC updated"); }

void initWeb(){ server.on("/",handleRoot); server.on("/save",HTTP_POST,handleSave); server.on("/schedule",handleSchedule); server.on("/status",handleStatus); server.on("/setrtc",handleSetRtcPage); server.on("/setrtc_do",handleSetRtcDo); server.begin(); }

void lcdShow(DateTime n){
  char l1[17],l2[17];
  snprintf(l1,sizeof(l1),"%02d/%02d %02d:%02d:%02d",n.day(),n.month(),n.hour(),n.minute(),n.second());
  int nextIdx=-1; for(int i=0;i<EVENT_COUNT;i++){ if(!eventExecuted[i]){ nextIdx=i; break; } }
  if(nextIdx>=0) snprintf(l2,sizeof(l2),"%s %02d:%02d",eventActionOn[nextIdx]?"ON ":"OFF",eventHour[nextIdx],eventMinute[nextIdx]); else snprintf(l2,sizeof(l2),"Done Today");
  lcd.setCursor(0,0); lcd.print("                "); lcd.setCursor(0,1); lcd.print("                "); lcd.setCursor(0,0); lcd.print(l1); lcd.setCursor(0,1); lcd.print(l2);
}

void setup(){
  pinMode(RELAY_PIN,OUTPUT); relaySet(false);
  Serial.begin(115200); EEPROM.begin(1024); loadConfig();
  Wire.begin(I2C_SDA,I2C_SCL);
  lcd = LiquidCrystal_I2C(cfg.lcdAddr,16,2); lcd.init(); lcd.backlight(); lcd.setCursor(0,0); lcd.print("Init v1.1...");
  if(!rtc.begin()){ lcd.setCursor(0,1); lcd.print("RTC ERROR"); while(1){ delay(1000); } }
  if(rtc.lostPower()){ rtc.adjust(DateTime(2026,1,1,0,0,0)); }
  WiFi.mode(WIFI_AP_STA); WiFi.softAP("SHOLAT-AMP-SETUP","12345678");
  if(strlen(cfg.ssid)>0){ WiFi.begin(cfg.ssid,cfg.pass); unsigned long t0=millis(); while(WiFi.status()!=WL_CONNECTED && millis()-t0<12000){ delay(200); } syncRTCFromNTPIfConnected(); }
  initWeb();
  DateTime n=rtc.now(); computeEventsForDate(n.year(),n.month(),n.day()); lastY=n.year(); lastM=n.month(); lastD=n.day(); lcd.clear();
}

void loop(){
  server.handleClient();
  DateTime n=rtc.now();
  if(n.year()!=lastY || n.month()!=lastM || n.day()!=lastD){ computeEventsForDate(n.year(),n.month(),n.day()); lastY=n.year(); lastM=n.month(); lastD=n.day(); }
  int h=n.hour(), m=n.minute();
  for(int i=0;i<EVENT_COUNT;i++){ if(!eventExecuted[i] && h==eventHour[i] && m==eventMinute[i]){ relaySet(eventActionOn[i]); eventExecuted[i]=true; } }
  if(millis()-lastRtcSyncMs>21600000UL){ lastRtcSyncMs=millis(); syncRTCFromNTPIfConnected(); }
  if(millis()-lastLcdMs>1000){ lastLcdMs=millis(); lcdShow(n); }
  delay(20);
}
