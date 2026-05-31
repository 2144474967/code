#include <WiFi.h>
#include <WebServer.h>
#include <FastLED.h>

// ===================== 请修改你的WiFi =====================
const char* ssid = "iPhone";
const char* password = "pY791101_";
// ==========================================================

WebServer server(80);

// ESP32-S2 硬件引脚（请根据你的实际硬件调整）
const int LIGHT_PIN = 4;   // 光敏电阻模拟输入（GPIO4）
const int LED_PIN   = 1;   // 小夜灯输出（GPIO1，注意部分板子可能冲突）
const int STRIP_PIN = 37;   // WS2812B 灯带数据线（GPIO2）
const int STRIP_LED_COUNT = 20;  // 灯带 LED 数量（按实际调整）
const int STRIP_BRIGHTNESS = 80; // 灯带亮度 0-255
// ⚠️ 硬件注意：ESP32 GPIO 输出 3.3V，WS2812B 需要 5V 逻辑电平。
// 如果灯带不亮或颜色异常，需在 DATA 线加电平转换模块（74HCT125 / SN74AHCT125）。
// 灯带 5V 可接 VBUS（USB 直出），不可接 3.3V 引脚（LDO 限流 ~500mA）。

// 状态变量
bool ledState   = false;   // 灯当前状态
bool autoMode   = true;    // 是否自动模式（true=自动，false=手动）
int  lightValue = 0;       // 当前光照值

// 灯带颜色与模式
int  stripR = 255, stripG = 150, stripB = 50;  // 当前颜色（暖橙默认）
int  stripBright = 80;      // 当前亮度 1-255
bool chaseMode  = false;    // 走马灯模式
bool chaseAuto  = false;    // 走马灯自动换色
int  chasePos   = 0;        // 走马灯当前位置
uint8_t chaseHue = 30;      // 自动换色当前色相
unsigned long lastChase = 0;

// WS2812B 灯带（FastLED）
CRGB leds[STRIP_LED_COUNT];

// 光照阈值与滞回（数值越小越暗，需根据实际电路校准）
const int LIGHT_THRESHOLD_ON  = 1500;  // 暗度低于此值开灯
const int LIGHT_THRESHOLD_OFF = 1800;  // 亮度高于此值关灯（滞回避免闪烁）

// 非阻塞定时
unsigned long previousMillis = 0;
const long interval = 200;   // 光照检测间隔（毫秒）

// 防闪烁：状态切换后强制保持最短时间，打断 LED 自身光线→传感器的反馈回路
unsigned long lastStateChange = 0;
const unsigned long MIN_STATE_DURATION = 1000;

// 刷新灯带（纯色模式）
void updateStrip() {
  if (!ledState) {
    fill_solid(leds, STRIP_LED_COUNT, CRGB::Black);
    FastLED.setBrightness(0);
  } else if (!chaseMode) {
    fill_solid(leds, STRIP_LED_COUNT, CRGB(stripR, stripG, stripB));
    FastLED.setBrightness(stripBright);
  }
  FastLED.show();
}

// 走马灯动画
void updateChase() {
  int tail = 3;
  fill_solid(leds, STRIP_LED_COUNT, CRGB::Black);
  for (int i = 0; i < tail; i++) {
    int idx = (chasePos + i) % STRIP_LED_COUNT;
    leds[idx] = CRGB(stripR, stripG, stripB);
  }
  FastLED.setBrightness(stripBright);
  FastLED.show();
}

// 网页控制界面（AJAX轮询，无刷新更新）
String getHTML() {
  String html = "<!DOCTYPE html><html lang=\"zh-CN\">";
  html += "<head><meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">";
  html += "<title>智能小夜灯</title>";
  html += "<style>";
  html += ":root{--bg:#080c14;--card:#111827;--text:#b8c5d6;--muted:#5a6680;--gold:#f0c060;--glow:rgba(240,192,96,0.25)}";
  html += "*{margin:0;padding:0;box-sizing:border-box}";
  html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','PingFang SC',sans-serif;";
  html += "background:radial-gradient(ellipse at 50% 25%,#1a2744 0%,#0c101a 60%,#080c14 100%);";
  html += "color:var(--text);min-height:100vh;display:flex;align-items:center;justify-content:center}";
  html += "#stars{position:fixed;top:0;left:0;width:100%;height:100%;pointer-events:none;z-index:0}";
  html += ".wrap{position:relative;z-index:1;width:92%;max-width:380px;padding:36px 28px;text-align:center;animation:fadeIn .6s ease-out}";
  html += "@keyframes fadeIn{from{opacity:0;transform:translateY(16px)}to{opacity:1;transform:translateY(0)}}";
  html += ".bulb{width:100px;height:100px;border-radius:50%;margin:0 auto 28px;";
  html += "background:var(--card);box-shadow:inset 0 0 30px rgba(0,0,0,.5),0 0 0 rgba(240,192,96,0);";
  html += "transition:all .7s cubic-bezier(.34,1.56,.64,1);position:relative}";
  html += ".bulb::after{content:'';position:absolute;inset:-4px;border-radius:50%;";
  html += "border:1.5px solid rgba(255,255,255,.04);pointer-events:none}";
  html += ".bulb.on{background:radial-gradient(circle at 42% 38%,#fff3d0 0%,#f0c060 35%,#c8861a 70%,#6b3a04 100%);";
  html += "box-shadow:0 0 30px var(--glow),0 0 70px rgba(240,192,96,.18),0 0 140px rgba(240,192,96,.08);";
  html += "animation:breath 2.4s ease-in-out infinite}";
  html += "@keyframes breath{0%,100%{box-shadow:0 0 30px var(--glow),0 0 70px rgba(240,192,96,.18),0 0 140px rgba(240,192,96,.08)}";
  html += "50%{box-shadow:0 0 50px rgba(240,192,96,.35),0 0 100px rgba(240,192,96,.25),0 0 180px rgba(240,192,96,.12)}}";
  html += "h1{font-weight:300;font-size:1.6em;letter-spacing:.06em;color:#d8dfec;margin-bottom:22px}";
  html += ".card{display:flex;justify-content:space-between;align-items:center;";
  html += "background:var(--card);border-radius:10px;padding:12px 16px;margin:6px 0;font-size:.9em;";
  html += "border:1px solid rgba(255,255,255,.04)}";
  html += ".card span:first-child{color:var(--muted)}";
  html += ".card span:last-child{font-weight:600;color:#e0e8f2}";
  html += ".tag{display:inline-block;padding:3px 10px;border-radius:20px;font-size:.78em;font-weight:600}";
  html += ".tag.auto{background:#132a1e;color:#4ade80}";
  html += ".tag.manual{background:#2a2012;color:var(--gold)}";
  html += ".btns{display:flex;gap:10px;margin-top:26px;flex-wrap:wrap;justify-content:center}";
  html += ".btn{border:none;border-radius:10px;padding:13px 20px;font-size:.95em;font-weight:500;";
  html += "cursor:pointer;transition:all .2s ease;letter-spacing:.02em;color:#fff;flex:1;min-width:90px}";
  html += ".btn:active{transform:scale(.96)}";
  html += ".btn-on{background:linear-gradient(135deg,#f0c060,#c8861a);box-shadow:0 4px 18px rgba(240,192,96,.2)}";
  html += ".btn-on:hover{box-shadow:0 6px 28px rgba(240,192,96,.4);transform:translateY(-1px)}";
  html += ".btn-off{background:linear-gradient(135deg,#2d3548,#1a1f2e);box-shadow:0 4px 14px rgba(0,0,0,.4)}";
  html += ".btn-off:hover{box-shadow:0 6px 22px rgba(0,0,0,.6);transform:translateY(-1px)}";
  html += ".btn-mode{background:transparent;border:1.5px solid #374151;color:var(--text);flex:0 0 auto;min-width:auto}";
  html += ".btn-mode:hover{border-color:#6b7a94;background:rgba(255,255,255,.02)}";
  html += ".hint{margin-top:22px;font-size:.72em;color:var(--muted);opacity:.7}";
  html += "html{touch-action:manipulation;-webkit-tap-highlight-color:transparent;user-select:none}";
  html += "body{overflow-x:hidden}";
  html += ".btn{min-height:44px}";
  html += ".swatches{display:flex;gap:8px;justify-content:center;flex-wrap:wrap;margin:14px 0 6px}";
  html += ".sw{width:30px;height:30px;border-radius:50%;cursor:pointer;border:2px solid transparent;transition:all .15s}";
  html += ".sw.on{border-color:#fff;box-shadow:0 0 10px currentColor;transform:scale(1.15)}";
  html += ".slider-wrap{display:flex;align-items:center;gap:10px;margin:10px 0}";
  html += ".slider-wrap span{color:var(--muted);font-size:.78em;white-space:nowrap}";
  html += "input[type=range]{-webkit-appearance:none;flex:1;height:5px;border-radius:3px;background:#2d3548;outline:none}";
  html += "input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;border-radius:50%;background:var(--gold);cursor:pointer;box-shadow:0 0 8px var(--glow)}";
  html += ".btn-chase{background:transparent;border:1.5px solid #374151;color:var(--text);width:100%;margin-top:6px}";
  html += ".btn-chase.active{border-color:var(--gold);color:var(--gold);box-shadow:0 0 12px rgba(240,192,96,.15)}";
  html += "@media(max-width:400px){.wrap{padding:28px 18px}";
  html += "h1{font-size:1.4em}.bulb{width:80px;height:80px;margin-bottom:22px}";
  html += ".card{padding:10px 14px;font-size:.82em}";
  html += ".btn{padding:12px 14px;font-size:.88em;min-width:70px}}";
  html += "</style></head>";
  html += "<body><canvas id=\"stars\"></canvas><div class=\"wrap\">";
  html += "<div class=\"bulb\" id=\"bulb\"></div>";
  html += "<h1>智能小夜灯</h1>";
  html += "<div class=\"card\"><span>灯状态</span><span id=\"ledSt\">--</span></div>";
  html += "<div class=\"card\"><span>工作模式</span><span id=\"mode\">--</span></div>";
  html += "<div class=\"card\"><span>光照值</span><span id=\"lux\">--</span></div>";
  html += "<div class=\"card\"><span>灯带状态</span><span id=\"stripSt\">--</span></div>";
  html += "<div class=\"swatches\" id=\"swatches\"></div>";
  html += "<div class=\"slider-wrap\"><span>亮度</span><input type=\"range\" min=\"5\" max=\"255\" value=\"80\" id=\"briSlider\" oninput=\"setBrightness(this.value)\"></div>";
  html += "<button class=\"btn btn-chase\" id=\"btnChase\" onclick=\"toggleChase()\">走马灯</button>";
  html += "<button class=\"btn btn-chase\" id=\"btnAutoColor\" onclick=\"toggleChaseColor()\" style=\"margin-top:4px;display:none\">自动换色</button>";
  html += "<div class=\"btns\">";
  html += "<button class=\"btn btn-on\" onclick=\"send('on')\">开灯</button>";
  html += "<button class=\"btn btn-off\" onclick=\"send('off')\">关灯</button>";
  html += "<button class=\"btn btn-mode\" onclick=\"send('auto')\">切换模式</button>";
  html += "</div>";
  html += "<p class=\"hint\">点击开/关灯将切换为手动模式</p>";
  html += "</div>";
  html += "<script>";
  html += "var colors=[{n:'暖白',r:255,g:150,b:50},{n:'冷白',r:200,g:210,b:255},{n:'琥珀',r:255,g:80,b:0},{n:'红',r:255,g:0,b:0},{n:'绿',r:0,g:255,b:0},{n:'蓝',r:0,g:100,b:255},{n:'粉',r:255,g:50,b:150},{n:'紫',r:150,g:0,b:255}];";
  html += "function renderSwatches(){var h='';for(var i=0;i<colors.length;i++){var c=colors[i];h+='<div class=\"sw\" style=\"background:rgb('+c.r+','+c.g+','+c.b+')\" onclick=\"setColor('+c.r+','+c.g+','+c.b+')\" title=\"'+c.n+'\"></div>'}document.getElementById('swatches').innerHTML=h}";
  html += "function setColor(r,g,b){fetch('/color?r='+r+'&g='+g+'&b='+b);poll()}";
  html += "var briTimer=null,briDrag=false;function setBrightness(v){briDrag=true;clearTimeout(briTimer);briTimer=setTimeout(function(){briDrag=false;fetch('/brightness?val='+v);poll()},250)}";
  html += "function toggleChase(){fetch('/chase');poll()}";
  html += "function toggleChaseColor(){fetch('/chasecolor');poll()}";
  html += "renderSwatches();";
  html += "(function(){var c=document.getElementById('stars'),x=c.getContext('2d'),w,h,s=[],bg=[];";
  html += "function rs(){w=c.width=innerWidth;h=c.height=innerHeight;bg=[];for(var i=0;i<100;i++)bg.push({x:Math.random()*w,y:Math.random()*h,r:Math.random()*1.2+.2,a:Math.random()*.35+.3,ph:Math.random()*6.28,f:Math.random()*.0015+.0005})}";
  html += "rs();onresize=rs;";
  html += "function spawn(){var a=Math.PI/4;return{x:Math.random()*w*.7+w*.15,y:Math.random()*h*.25,len:Math.random()*80+60,spd:Math.random()*2+1,life:0,max:Math.random()*20+12,ang:a}}";
  html += "function draw(){x.clearRect(0,0,w,h);";
  html += "for(var i=0;i<bg.length;i++){var b=bg[i];x.beginPath();x.arc(b.x,b.y,b.r,0,6.28);var tw=Math.sin(Date.now()*b.f+b.ph)*.3+.6;x.beginPath();x.arc(b.x,b.y,b.r*(.7+tw*.6),0,6.28);x.fillStyle='rgba(190,210,240,'+(b.a*tw)+')';x.fill()}";
  html += "for(var i=0;i<s.length;i++){var st=s[i],p=st.life/st.max,al=p<.15?p/.15:1-(p-.15)/.85;";
  html += "var ex=st.x+Math.cos(st.ang)*st.len*p,ey=st.y-Math.sin(st.ang)*st.len*p;";
  html += "var g=x.createLinearGradient(st.x,st.y,ex,ey);g.addColorStop(0,'rgba(255,245,230,'+al+')');g.addColorStop(1,'rgba(200,210,240,0)');";
  html += "x.beginPath();x.moveTo(st.x,st.y);x.lineTo(ex,ey);x.strokeStyle=g;x.lineWidth=1.6;x.stroke();";
  html += "x.beginPath();x.arc(st.x,st.y,2.2,0,6.28);x.fillStyle='rgba(255,250,240,'+(al*.85)+')';x.fill()}";
  html += "}";
  html += "function upd(){if(Math.random()<.07)s.push(spawn());";
  html += "for(var i=s.length-1;i>=0;i--){var st=s[i];st.x-=Math.cos(st.ang)*st.spd;st.y+=Math.sin(st.ang)*st.spd;st.life++;if(st.life>st.max||st.x<-80||st.y>h+80)s.splice(i,1)}";
  html += "draw()}";
  html += "setInterval(upd,66);upd()})();";
  html += "async function poll(){try{let r=await fetch('/state');let d=await r.json();";
  html += "document.getElementById('ledSt').textContent=d.led?'开启':'关闭';";
  html += "document.getElementById('mode').innerHTML=d.auto?'<span class=\"tag auto\">自动</span>':'<span class=\"tag manual\">手动</span>';";
  html += "document.getElementById('lux').textContent=d.light+' (0-4095)';";
  html += "document.getElementById('stripSt').textContent=d.strip?'开启':'关闭';";
  html += "let b=document.getElementById('bulb');d.led?b.classList.add('on'):b.classList.remove('on');";
  html += "if(!briDrag)document.getElementById('briSlider').value=d.bright;";
  html += "var btn=document.getElementById('btnChase');d.chase?(btn.classList.add('active'),btn.textContent='走马灯:开'):(btn.classList.remove('active'),btn.textContent='走马灯');";
  html += "var ab=document.getElementById('btnAutoColor');d.chase?(ab.style.display='',d.chaseauto?(ab.classList.add('active'),ab.textContent='自动换色:开'):(ab.classList.remove('active'),ab.textContent='自动换色')):ab.style.display='none';";
  html += "var sw=document.querySelectorAll('.sw');for(var i=0;i<sw.length;i++){var cl=colors[i];sw[i].classList.toggle('on',cl.r==d.r&&cl.g==d.g&&cl.b==d.b)}}catch(e){}}";
  html += "async function send(c){await fetch('/'+c);poll()}";
  html += "poll();setInterval(poll,2000);";
  html += "</script></body></html>";
  return html;
}

// ---------- 网页处理函数 ----------
void handleRoot() {
  server.send(200, "text/html", getHTML());
}

void handleState() {
  String json = "{";
  json += "\"led\":" + String(ledState ? "true" : "false") + ",";
  json += "\"auto\":" + String(autoMode ? "true" : "false") + ",";
  json += "\"light\":" + String(lightValue) + ",";
  json += "\"strip\":" + String(ledState ? "true" : "false") + ",";
  json += "\"chase\":" + String(chaseMode ? "true" : "false") + ",";
  json += "\"chaseauto\":" + String(chaseAuto ? "true" : "false") + ",";
  json += "\"r\":" + String(stripR) + ",";
  json += "\"g\":" + String(stripG) + ",";
  json += "\"b\":" + String(stripB) + ",";
  json += "\"bright\":" + String(stripBright);
  json += "}";
  server.send(200, "application/json", json);
}

void handleOn() {
  autoMode = false;          // 手动操作，退出自动模式
  ledState = true;
  digitalWrite(LED_PIN, ledState);
  updateStrip();
  handleRoot();
}

void handleOff() {
  autoMode = false;
  ledState = false;
  digitalWrite(LED_PIN, ledState);
  updateStrip();
  handleRoot();
}

void handleAuto() {
  autoMode = !autoMode;
  // 如果从手动切回自动，立即根据当前光照更新一次
  if (autoMode) {
    lightValue = min((int)analogRead(LIGHT_PIN), 4095);
    if (lightValue < LIGHT_THRESHOLD_ON) {
      ledState = true;
    } else if (lightValue > LIGHT_THRESHOLD_OFF) {
      ledState = false;
    }
    digitalWrite(LED_PIN, ledState);
    updateStrip();
    lastStateChange = millis();  // 记录切换时间，防止立即被 loop 再次翻转
  }
  handleRoot();
}

void handleChase() {
  chaseMode = !chaseMode;
  if (chaseMode) {
    chasePos = 0;
    lastChase = millis();
    updateChase();
  } else {
    updateStrip();
  }
  handleRoot();
}

void handleChaseColor() {
  chaseAuto = !chaseAuto;
  if (chaseAuto) chaseHue = 30;
  handleRoot();
}

void handleColor() {
  if (server.hasArg("r")) stripR = server.arg("r").toInt();
  if (server.hasArg("g")) stripG = server.arg("g").toInt();
  if (server.hasArg("b")) stripB = server.arg("b").toInt();
  if (ledState && !chaseMode) updateStrip();
  handleRoot();
}

void handleBrightness() {
  if (server.hasArg("val")) {
    stripBright = server.arg("val").toInt();
    if (stripBright > 255) stripBright = 255;
    if (stripBright < 5) stripBright = 5;
  }
  if (ledState) updateStrip();
  handleRoot();
}

// ===================== 初始化 =====================
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);  // 固定12位ADC，确保范围0-4095
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // 连接WiFi（带超时，15秒未连接则跳过）
  Serial.print("正在连接WiFi: ");
  Serial.print(ssid);
  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();
  const unsigned long wifiTimeout = 15000;  // 15秒超时

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi 连接成功");
    Serial.print("📶 IP地址：");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi 连接失败，将继续运行本地服务（需手动重启）");
  }

  // 初始化灯带（FastLED 使用 RMT，在 WiFi 之后初始化避免通道冲突）
  FastLED.addLeds<WS2812B, STRIP_PIN, GRB>(leds, STRIP_LED_COUNT);
  FastLED.setBrightness(STRIP_BRIGHTNESS);
  FastLED.clear();
  FastLED.show();
  // 启动测试：闪一下暖白光确认灯带工作
  fill_solid(leds, STRIP_LED_COUNT, CRGB(255, 150, 50));
  FastLED.show();
  delay(300);
  fill_solid(leds, STRIP_LED_COUNT, CRGB::Black);
  FastLED.show();
  Serial.println("💡 灯带初始化完成");

  // 启动Web服务器
  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/auto", handleAuto);
  server.on("/chase", handleChase);
  server.on("/chasecolor", handleChaseColor);
  server.on("/color", handleColor);
  server.on("/brightness", handleBrightness);
  server.begin();
  Serial.println("🌐 Web服务器已启动");
}

// ===================== 主循环 =====================
void loop() {
  server.handleClient();   // 优先处理客户端请求

  // 非阻塞定时读取光敏电阻，避免占用过多CPU导致网页响应慢
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // 读取光敏电阻（12位ADC，0-4095）
    lightValue = min((int)analogRead(LIGHT_PIN), 4095);

    // 自动模式：根据光照自动控制灯
    if (autoMode) {
      unsigned long now = millis();
      // 状态切换后至少保持 MIN_STATE_DURATION，防止 LED 自身光线干扰传感器导致闪烁
      if (now - lastStateChange >= MIN_STATE_DURATION) {
        if (ledState == false && lightValue < LIGHT_THRESHOLD_ON) {
          ledState = true;
          lastStateChange = now;
        } else if (ledState == true && lightValue > LIGHT_THRESHOLD_OFF) {
          ledState = false;
          lastStateChange = now;
        }
        digitalWrite(LED_PIN, ledState);
        updateStrip();
      }
    }
    // 手动模式下不改变 ledState，保持用户设定的值
  }

  // 走马灯动画（独立于光照检测）
  if (chaseMode && ledState) {
    unsigned long now = millis();
    if (now - lastChase >= 80) {  // 80ms 每步
      lastChase = now;
      chasePos = (chasePos + 1) % STRIP_LED_COUNT;
      if (chaseAuto) {
        chaseHue += 3;
        CRGB c = CHSV(chaseHue, 255, 255);
        stripR = c.r; stripG = c.g; stripB = c.b;
      }
      updateChase();
    }
  }
}