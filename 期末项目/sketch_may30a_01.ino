#include <WiFi.h>
#include <WebServer.h>
// ESP32-S2专用舵机库
#include <ESP32Servo.h>

// -------------------------- 请修改以下参数 --------------------------
const char* ssid = "OnePlus";       // 仅支持2.4G WiFi，不支持5G
const char* password = "15037589976";   // WiFi密码
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
const int LIGHT_THRESHOLD = 2000;   // 光照阈值（越大越亮，0-4095）
const int AUTO_OFF_DELAY = 1*60*1000; // 无人自动关灯延时（1分钟）
const int ON_ANGLE = 90;            // 开灯舵机角度
const int OFF_ANGLE = 0;            // 关灯舵机角度

// 全局变量
bool lightState = false;            // 灯泡当前状态
bool autoMode = true;               // 【新增】模式标志：true=自动传感器控制，false=网页手动控制
unsigned long lastHumanTime = 0;    // 最后检测到人的时间
float currentDistance = 999;        // 实时距离
int currentLight = 0;               // 实时光照值

// 光照传感器多次采样取平均（减少ADC噪声波动）
int readLightStable() {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(LIGHT_PIN);
    delayMicroseconds(500);
  }
  return sum / 10;
}

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
    servo.write(ON_ANGLE);
    delay(500);
    servo.write(OFF_ANGLE);
    lightState = false;
    Serial.println("关灯成功");
  }
}

// -------------------------- 网页控制相关函数 --------------------------
// 生成控制网页（新增模式切换按钮）
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>智能灯控制</title><style>";
  html += "body{font-family:微软雅黑;max-width:400px;margin:30px auto;text-align:center;}";
  html += ".btn{padding:15px 40px;font-size:18px;border:none;border-radius:8px;color:white;cursor:pointer;margin:8px 5px;transition:all 0.3s;}";
  html += ".on{background-color:#2ecc71;}.off{background-color:#e74c3c;}";
  html += ".mode-active{background-color:#3498db;}.mode-normal{background-color:#95a5a6;}";
  html += ".btn:disabled{background-color:#bdc3c7;cursor:not-allowed;opacity:0.7;}";
  html += ".status{margin:25px 0;font-size:17px;line-height:1.8;text-align:left;padding:0 20px;}";
  html += ".card{border:1px solid #eee;border-radius:10px;padding:15px;margin:15px 0;}";
  html += ".hint{font-size:13px;color:#e67e22;margin-top:8px;}";
  html += ".light-indicator{display:inline-block;width:20px;height:20px;border-radius:50%;margin-right:8px;vertical-align:middle;}";
  html += ".light-on{background:#2ecc71;box-shadow:0 0 10px #2ecc71;}";
  html += ".light-off{background:#bdc3c7;}";
  html += "</style></head><body>";
  html += "<h1>智能灯控制面板</h1>";

  // 模式切换区域
  html += "<div class='card'><h3>控制模式选择</h3>";
  if (autoMode) {
    html += "<button class='btn mode-active'>当前：传感器自动控制</button>";
    html += "<button class='btn mode-normal' onclick='location.href=\"/setManual\"'>切换为网页手动控制</button>";
  } else {
    html += "<button class='btn mode-normal' onclick='location.href=\"/setAuto\"'>切换为传感器自动控制</button>";
    html += "<button class='btn mode-active'>当前：网页手动控制</button>";
  }
  html += "</div>";

  // 开关按钮区域（根据模式和灯状态动态变化）
  html += "<div class='card'><h3>灯光控制</h3>";
  const char* indicatorClass = lightState ? "light-on" : "light-off";
  const char* indicatorText = lightState ? "灯已开启" : "灯已关闭";
  html += "<div style='margin-bottom:10px;'><span class='light-indicator ";
  html += indicatorClass;
  html += "'></span><strong>";
  html += indicatorText;
  html += "</strong></div>";
  if (autoMode) {
    // 自动模式：禁用手动按钮，显示提示
    html += "<button class='btn' disabled>开启灯光</button>";
    html += "<button class='btn' disabled>关闭灯光</button>";
    html += "<p class='hint'>自动模式下灯光由传感器控制，请切换为手动模式后操作</p>";
  } else {
    // 手动模式：根据灯状态显示对应按钮
    if (lightState) {
      html += "<button class='btn off' onclick='location.href=\"/off\"'>关闭灯光</button>";
    } else {
      html += "<button class='btn on' onclick='location.href=\"/on\"'>开启灯光</button>";
    }
  }
  html += "</div>";

  // 实时状态显示
  html += "<div class='card'><h3>实时状态</h3>";
  html += "<div class='status'>";
  html += "🔌 灯状态：";
  html += lightState ? "✅ 已开启" : "❌ 已关闭";
  html += "<br>🎛️  控制模式：";
  html += autoMode ? "🤖 传感器自动控制" : "📱 网页手动控制";
  html += "<br>📏 当前距离：";
  html += (int)currentDistance;
  html += ".";
  html += (int)(currentDistance * 10) % 10;
  html += " cm<br>☀️ 当前光照值：";
  html += currentLight;
  html += "（数值越大越亮）<br>";
  if (autoMode) {
    html += "⚙️ 自动规则：光照值低于2000+2米内有人自动开灯，无人5分钟自动关灯";
  } else {
    html += "⚙️ 手动规则：传感器逻辑已暂停，仅网页按钮可控制";
  }
  html += "</div></div>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

// 网页开灯请求
void handleOn() {
  turnOnLight();
  server.sendHeader("Location", "/");
  server.send(302);
}

// 网页关灯请求
void handleOff() {
  turnOffLight();
  server.sendHeader("Location", "/");
  server.send(302);
}

// 【新增】切换为自动模式
void handleSetAuto() {
  autoMode = true;
  lastHumanTime = millis(); // 切换自动模式时，重置无人计时，避免立刻关灯
  Serial.println("已切换为：传感器自动控制模式");
  server.sendHeader("Location", "/");
  server.send(302);
}

// 【新增】切换为手动模式
void handleSetManual() {
  autoMode = false;
  Serial.println("已切换为：网页手动控制模式");
  server.sendHeader("Location", "/");
  server.send(302);
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
  
  server.begin();
  Serial.println("默认模式：传感器自动控制");
  Serial.println("系统初始化完成");
}
void loop() {
  server.handleClient();

  // 【核心逻辑修改】仅在自动模式下，运行传感器自动控制逻辑
  if (autoMode) {
    currentDistance = getDistance();
    currentLight = readLightStable();
    
    // 串口打印日志（仅自动模式打印）
    Serial.print("模式：自动 | 距离：");
    Serial.print(currentDistance);
    Serial.print("cm | 光照值：");
    Serial.println(currentLight);
    
    // 自动开灯逻辑：有人+光线暗+灯关闭
    if (currentDistance < DETECT_DISTANCE) {
      lastHumanTime = millis();
      if (currentLight < LIGHT_THRESHOLD && !lightState) {
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
    currentLight = readLightStable(); // 仅更新网页显示的数值，不参与控制
  }
  
  delay(500);
}