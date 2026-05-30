#include <WiFi.h>
#include <WebServer.h>
// ESP32-S2专用舵机库
#include <ESP32Servo.h>

// -------------------------- 请修改以下参数 --------------------------
const char* ssid = "vivoS18";       // 仅支持2.4G WiFi，不支持5G
const char* password = "12345678";   // WiFi密码
// -------------------------------------------------------------------

// 【LOLIN S2 Mini 引脚定义 完全不变】
#define TRIG_PIN    38  // 超声波TRIG
#define ECHO_PIN    36  // 超声波ECHO
#define LIGHT_PIN   2   // 光敏电阻
#define SERVO_PIN   33  // 舵机信号

// 网页服务器对象
WebServer server(80);

// 舵机对象
Servo servo;

// 核心参数（和原逻辑完全一致）
const int DETECT_DISTANCE = 200;    // 人体检测距离阈值（cm）
const int LIGHT_THRESHOLD = 200;   // 光照阈值（越小越暗，0-4095）
const int AUTO_OFF_DELAY = 0.1*60*1000; // 无人自动关灯延时（5分钟）
const int ON_ANGLE = 90;            // 开灯舵机角度
const int OFF_ANGLE = 45;            // 关灯舵机角度
const int OOP = 0;

// 全局变量
bool lightState = false;            // 灯泡当前状态
bool autoMode = true;               // 【新增】模式标志：true=自动传感器控制，false=网页手动控制
unsigned long lastHumanTime = 0;    // 最后检测到人的时间
float currentDistance = 999;        // 实时距离
int currentLight = 0;               // 实时光照值

// 超声波测距函数（完全不变）
float getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

// 开灯函数（完全不变）
void turnOnLight() {
  if (!lightState) {
    servo.write(ON_ANGLE);
    delay(500);
    servo.write(OFF_ANGLE);
    lightState = true;
    Serial.println("开灯成功");
  }
}

// 关灯函数（完全不变）
void turnOffLight() {
  if (lightState) {
    servo.write(OOP);
    delay(500);
    servo.write(OFF_ANGLE);
    lightState = false;
    Serial.println("关灯成功");
  }
}

// 返回JSON格式的实时数据（供AJAX调用，避免整页刷新）
void handleData() {
  String json = "{";
  json += "\"light\":" + String(lightState ? "true" : "false") + ",";
  json += "\"auto\":" + String(autoMode ? "true" : "false") + ",";
  json += "\"dist\":" + String(currentDistance, 1) + ",";
  json += "\"lux\":" + String(currentLight);
  json += "}";
  server.send(200, "application/json", json);
}

// 返回简化的状态按钮HTML片段（供AJAX调用）
void handleStatus() {
  String h = "";
  // 模式按钮
  h += "<div class='card'><h3>控制模式</h3>";
  if (autoMode) {
    h += "<button class='btn mode-active'>自动模式</button>";
    h += "<button class='btn mode-normal' onclick='sendCmd(\"setManual\")'>切换手动</button>";
  } else {
    h += "<button class='btn mode-normal' onclick='sendCmd(\"setAuto\")'>切换自动</button>";
    h += "<button class='btn mode-active'>手动模式</button>";
  }
  h += "</div>";
  // 灯光按钮
  h += "<div class='card'><h3>灯光控制</h3>";
  h += "<div class='indicator-wrap'><span class='indicator " + String(lightState ? "on" : "off") + "'></span>";
  h += String(lightState ? "灯已开启" : "灯已关闭") + "</div>";
  if (lightState) {
    h += "<button class='btn off' onclick='sendCmd(\"off\")'>关闭灯光</button>";
  } else {
    h += "<button class='btn on' onclick='sendCmd(\"on\")'>开启灯光</button>";
  }
  h += "</div>";
  // 状态面板
  h += "<div class='card'><h3>实时状态</h3><div class='status'>";
  h += "<div class='stat-row'><span class='label'>灯状态</span><span class='val'>" + String(lightState ? "已开启" : "已关闭") + "</span></div>";
  h += "<div class='stat-row'><span class='label'>控制模式</span><span class='val'>" + String(autoMode ? "自动" : "手动") + "</span></div>";
  h += "<div class='stat-row'><span class='label'>距离</span><span class='val' id='dist'>--</span></div>";
  h += "<div class='stat-row'><span class='label'>光照</span><span class='val' id='lux'>--</span></div>";
  h += "<div class='rule'>" + String(autoMode ? "光线暗+2米内有人→开灯，无人5分钟→关灯" : "传感器暂停，仅网页可控制") + "</div>";
  h += "</div></div>";
  server.send(200, "text/html", h);
}

// 生成控制网页
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>智能灯控制</title><style>";
  html += "*{box-sizing:border-box;margin:0;padding:0}";
  html += "body{font-family:-apple-system,sans-serif;background:#f0f2f5;color:#333}";
  html += ".wrap{max-width:420px;margin:0 auto;padding:16px}";
  html += "h1{text-align:center;font-size:22px;padding:20px 0 10px;color:#1a1a1a}";
  html += ".card{background:#fff;border-radius:12px;padding:16px;margin:10px 0;box-shadow:0 1px 3px rgba(0,0,0,.08)}";
  html += ".card h3{font-size:14px;color:#888;margin-bottom:10px;font-weight:500}";
  html += ".btn{padding:12px 0;font-size:15px;border:none;border-radius:8px;color:#fff;cursor:pointer;width:48%;transition:opacity .2s}";
  html += ".btn:active{opacity:.7}";
  html += ".on{background:#22c55e}.off{background:#ef4444}";
  html += ".mode-active{background:#3b82f6}.mode-normal{background:#94a3b8}";
  html += ".indicator-wrap{display:flex;align-items:center;justify-content:center;gap:8px;font-weight:600;margin-bottom:12px;font-size:15px}";
  html += ".indicator{width:14px;height:14px;border-radius:50%;display:inline-block}";
  html += ".indicator.on{background:#22c55e;box-shadow:0 0 8px #22c55e}";
  html += ".indicator.off{background:#cbd5e1}";
  html += ".status{font-size:14px}";
  html += ".stat-row{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #f0f0f0}";
  html += ".stat-row:last-of-type{border:none}";
  html += ".stat-row .label{color:#888}";
  html += ".stat-row .val{font-weight:600}";
  html += ".rule{font-size:12px;color:#f59e0b;margin-top:8px;padding:8px;background:#fffbeb;border-radius:6px}";
  html += ".live-dot{display:inline-block;width:6px;height:6px;border-radius:50%;background:#22c55e;margin-right:4px;animation:pulse 1.5s infinite}";
  html += "@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}";
  html += "</style></head><body><div class='wrap'>";
  html += "<h1>智能灯控制面板</h1>";
  html += "<div id='ctrl'></div>";
  html += "<div id='status'></div>";
  html += "<div style='text-align:center;margin:12px 0;font-size:12px;color:#aaa'><span class='live-dot'></span>每2秒自动刷新</div>";
  html += "</div><script>";
  // 发送控制命令（不刷新整页，只更新状态区域）
  html += "function sendCmd(c){fetch('/'+c).then(()=>{setTimeout(refresh,300)})}";
  // 刷新状态面板
  html += "function refresh(){fetch('/status').then(r=>r.text()).then(t=>{document.getElementById('ctrl').innerHTML=t});";
  html += "fetch('/data').then(r=>r.json()).then(d=>{";
  html += "var di=document.getElementById('dist'),lu=document.getElementById('lux');";
  html += "if(di)di.textContent=d.dist.toFixed(1)+' cm';";
  html += "if(lu)lu.textContent=d.lux})}";
  html += "refresh();setInterval(refresh,2000);";
  html += "</script></body></html>";
  server.send(200, "text/html", html);
}

// 网页开灯请求
void handleOn() {
  turnOnLight();
  server.send(200, "text/plain", "ok");
}

// 网页关灯请求
void handleOff() {
  turnOffLight();
  server.send(200, "text/plain", "ok");
}

// 切换为自动模式
void handleSetAuto() {
  autoMode = true;
  lastHumanTime = millis();
  Serial.println("已切换为：传感器自动控制模式");
  server.send(200, "text/plain", "ok");
}

// 切换为手动模式
void handleSetManual() {
  autoMode = false;
  Serial.println("已切换为：网页手动控制模式");
  server.send(200, "text/plain", "ok");
}

void setup() {
  Serial.begin(115200);
  
  // 初始化引脚
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LIGHT_PIN, INPUT);

  // ========== 新增：ESP32-S2 ADC专用配置 修复读数异常 ==========
  // 设置ADC分辨率为12位（满量程0~4095）
  analogReadResolution(12);
  // 设置ADC衰减为11dB，支持0~3.3V全范围测量（必须加）
  analogSetAttenuation(ADC_11db);
  // 给光敏引脚做一次初始读数，消除首次误差
  analogRead(LIGHT_PIN);
  delay(10);
  // ==========================================================
  
  // 初始化舵机
  servo.attach(SERVO_PIN);
  servo.write(OFF_ANGLE);
  
  // 连接WiFi
  Serial.print("正在连接WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi连接成功！");
  Serial.print("控制面板地址：");
  Serial.println(WiFi.localIP());
  
  // 注册网页路由
  server.on("/", handleRoot);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/setAuto", handleSetAuto);
  server.on("/setManual", handleSetManual);
  server.on("/data", handleData);
  server.on("/status", handleStatus);
  
  server.begin();
  Serial.println("默认模式：传感器自动控制");
  Serial.println("系统初始化完成");
}
void loop() {
  server.handleClient();

  // 【核心逻辑修改】仅在自动模式下，运行传感器自动控制逻辑
  if (autoMode) {
    currentDistance = getDistance();
    currentLight = analogRead(LIGHT_PIN);
    
    // 串口打印日志（仅自动模式打印）
    Serial.print("模式：自动 | 距离：");
    Serial.print(currentDistance);
    Serial.print("cm | 光照值：");
    Serial.println(currentLight);
      Serial.println(WiFi.localIP());
    
    // 自动开灯逻辑：有人+光线暗+灯关闭
    if (currentDistance < DETECT_DISTANCE) {
      lastHumanTime = millis();
      if (currentLight  < LIGHT_THRESHOLD && !lightState) {
        turnOnLight();
      }
    }
    
    // 自动关灯逻辑：无人超过5分钟+灯开启
    if (millis() - lastHumanTime > AUTO_OFF_DELAY && lightState) {
      turnOffLight();
    }
  } else {
    // 手动模式下，不运行任何传感器逻辑，仅响应网页按钮操作
    currentDistance = getDistance();
    currentLight = analogRead(LIGHT_PIN); // 仅更新网页显示的数值，不参与控制
  }
  
  delay(500);
}