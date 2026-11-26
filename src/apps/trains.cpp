#include "trains.h"

#include <Preferences.h>

#include "train_stops_data.h"
#include "web_ui.h"

namespace trains {
namespace {

using webui::htmlEscape;
using webui::jsonEscape;

WebServer* server = nullptr;
Preferences prefs;
Config cfg;
std::function<void()> changedCb;

const char* const kRoutes[] = {"Red", "Blue", "Brn", "G", "Org", "Pink", "P", "Y"};

String stationNameFor(uint32_t mapId);

void load() {
  cfg.route = prefs.getString("rt", "");
  cfg.lineName = prefs.getString("line", "");
  cfg.mapId = prefs.getUInt("mapid", 0);
  cfg.stationName = prefs.getString("station", "");
  if (cfg.stationName.isEmpty() && cfg.mapId) cfg.stationName = stationNameFor(cfg.mapId);
  cfg.stopId = prefs.getUInt("stpid", 0);
  cfg.direction = prefs.getString("dir", "");
  cfg.apiKey = prefs.getString("key", "");
  cfg.trainCount = constrain(prefs.getUChar("count", kDefaultTrainCount), 1, kMaxTrainCount);
#ifdef TRAIN_SAMPLE_DATA
  // Sample builds work out of the box: Red Line, Jarvis, Howard-bound, all six trains.
  if (!cfg.hasStop()) {
    cfg.route = "Red";
    cfg.lineName = "Red Line";
    cfg.mapId = 41190;
    cfg.stationName = "Jarvis";
    cfg.stopId = 30227;
    cfg.direction = "Howard-bound";
    cfg.trainCount = kMaxTrainCount;
  }
  if (cfg.apiKey.isEmpty()) cfg.apiKey = "sample";
#endif
}

// True if `[id,"` appears in the embedded stop list, i.e. it's a real station/stop ID.
bool knownId(uint32_t id) {
  char needle[16];
  snprintf(needle, sizeof(needle), "[%lu,\"", (unsigned long)id);
  return strstr(kTrainStopsJson, needle) != nullptr;
}

// Station name for a map ID, from the embedded stop list ("" if unknown).
String stationNameFor(uint32_t mapId) {
  char needle[16];
  snprintf(needle, sizeof(needle), "[%lu,\"", (unsigned long)mapId);
  const char* start = strstr(kTrainStopsJson, needle);
  if (!start) return "";
  start += strlen(needle);
  const char* end = strchr(start, '"');
  return end ? String(start).substring(0, end - start) : String();
}

bool knownRoute(const String& rt) {
  for (const char* r : kRoutes)
    if (rt == r) return true;
  return false;
}

// Mirrors transitchicago.com/traintracker "pick a stop from a list":
// line color -> station -> direction.
const char kPageScript[] PROGMEM = R"js(
<div id=cur class=card></div>
<form method=POST action=/trains id=f>
<h2>1. Select a line</h2>
<div id=lines class=lines></div>
<div id=stSec hidden><h2>2. Select a station</h2>
<select id=stationSel><option value=''>-- Select a station --</option></select></div>
<div id=dirSec hidden><h2>3. Select a direction</h2><div id=dirs class=dirs></div></div>
<input type=hidden name=rt><input type=hidden name=line><input type=hidden name=mapid>
<input type=hidden name=station><input type=hidden name=stpid><input type=hidden name=dir>
<h2>Arrivals to show</h2>
<select name=count id=count></select>
<p class=muted>The display rotates through this many upcoming trains.</p>
<h2>Train Tracker API key</h2>
<input name=key autocomplete=off id=key>
<p class=muted>Get a free key from the <a href="https://www.transitchicago.com/developers/" target=_blank>CTA developer center</a>.</p>
<button type=submit id=save disabled>Save</button>
</form>
<style>
.lines{display:grid;grid-template-columns:1fr 1fr;gap:.5em}
.lines button,.dirs button{margin:0;border:2px solid transparent;border-radius:6px;cursor:pointer}
.lines button{color:#fff;font-weight:600}
.lines button.on,.dirs button.on{outline:3px solid #222;outline-offset:2px}
.dirs{display:flex;flex-direction:column;gap:.5em}.dirs button{background:#eee;text-align:left}
.dot{display:inline-block;width:.8em;height:.8em;border-radius:50%;margin-right:.4em;vertical-align:-.05em}
</style>
<script>
const $=id=>document.getElementById(id),f=$('f');
let D=[],L=null,S=null,P=null;
function showCur(){
  $('cur').innerHTML=C.stpid?'<b>Currently tracking</b><br><span class=dot style="background:'+(C.color||'#888')+'"></span>'+
    esc(C.line)+' &middot; '+esc(C.station)+' &middot; '+esc(C.dir):'<b>No stop selected yet.</b>';
  $('key').placeholder=C.hasKey?'Saved (leave blank to keep)':'Paste your API key';
}
function esc(s){return String(s).replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';')}
function drawLines(){
  $('lines').innerHTML='';
  D.forEach((l,i)=>{const b=document.createElement('button');b.type='button';b.textContent=l[1];
    b.style.background=l[2];if(l[0]=='Y')b.style.color='#222';b.onclick=()=>pickLine(i);$('lines').appendChild(b)});
}
function pickLine(i){
  L=D[i];S=P=null;[...$('lines').children].forEach((b,j)=>b.classList.toggle('on',j==i));
  const sel=$('stationSel');sel.length=1;
  L[3].forEach((s,j)=>sel.add(new Option(s[1],j)));
  $('stSec').hidden=false;$('dirSec').hidden=true;update();
}
function pickStation(j){
  S=j===''?null:L[3][j];P=null;$('dirs').innerHTML='';$('dirSec').hidden=!S;
  if(S)S[2].forEach((p,k)=>{const b=document.createElement('button');b.type='button';b.textContent=p[1];
    b.onclick=()=>pickDir(k);$('dirs').appendChild(b)});
  if(S&&S[2].length==1)pickDir(0);update();
}
function pickDir(k){P=S[2][k];[...$('dirs').children].forEach((b,i)=>b.classList.toggle('on',i==k));update()}
function update(){
  const ok=!!(L&&S&&P);$('save').disabled=!ok;
  if(!ok)return;
  f.rt.value=L[0];f.line.value=L[1];f.mapid.value=S[0];f.station.value=S[1];f.stpid.value=P[0];f.dir.value=P[1];
}
$('stationSel').onchange=e=>pickStation(e.target.value);
f.onsubmit=e=>{if(!(L&&S&&P))e.preventDefault()};
for(let n=1;n<=C.maxCount;n++)$('count').add(new Option(n+(n==1?' train':' trains'),n));
$('count').value=C.count;
showCur();
fetch('/trains/stops.json').then(r=>r.json()).then(d=>{
  D=d;drawLines();
  const i=D.findIndex(l=>l[0]==C.rt);if(i<0)return;
  C.color=D[i][2];showCur();pickLine(i);
  const j=L[3].findIndex(s=>s[0]==C.mapid);if(j<0)return;
  $('stationSel').value=j;pickStation(j);
  const k=S[2].findIndex(p=>p[0]==C.stpid);if(k>=0)pickDir(k);
}).catch(()=>{$('lines').textContent='Could not load the stop list.'});
</script>
)js";

void handlePage() {
  String body;
  if (server->hasArg("saved")) body += "<p class=card style='background:#e8f5e9'>Saved.</p>";
  body += "<script>const C={rt:" + jsonEscape(cfg.route) + ",line:" + jsonEscape(cfg.lineName) +
          ",mapid:" + String(cfg.mapId) + ",station:" + jsonEscape(cfg.stationName) +
          ",stpid:" + String(cfg.stopId) + ",dir:" + jsonEscape(cfg.direction) +
          ",hasKey:" + (cfg.apiKey.isEmpty() ? "false" : "true") +
          ",count:" + String(cfg.trainCount) + ",maxCount:" + String(kMaxTrainCount) + "};</script>";
  body += FPSTR(kPageScript);
  server->send(200, "text/html", webui::page("Train arrivals", body));
}

void handleStops() {
  server->sendHeader("Cache-Control", "max-age=86400");
  server->send_P(200, "application/json", kTrainStopsJson);
}

void handleSave() {
  String rt = server->arg("rt");
  uint32_t mapId = server->arg("mapid").toInt();
  uint32_t stopId = server->arg("stpid").toInt();
  if (!knownRoute(rt) || !knownId(mapId) || !knownId(stopId)) {
    server->send(400, "text/html",
                 webui::page("Train arrivals", "<p>That isn't a valid line, station and direction. "
                                               "<a href=/trains>Try again</a>.</p>"));
    return;
  }
  prefs.putString("rt", rt);
  prefs.putString("line", server->arg("line").substring(0, 32));
  prefs.putUInt("mapid", mapId);
  prefs.putString("station", stationNameFor(mapId));
  prefs.putUInt("stpid", stopId);
  prefs.putString("dir", server->arg("dir").substring(0, 48));
  prefs.putUChar("count", constrain(server->arg("count").toInt(), 1, kMaxTrainCount));
  String key = server->arg("key");
  key.trim();
  if (!key.isEmpty()) prefs.putString("key", key.substring(0, 64));
  load();
  Serial.printf("[trains] tracking %s / %s (%lu) / %s (%lu)\n", cfg.lineName.c_str(),
                cfg.stationName.c_str(), (unsigned long)cfg.mapId, cfg.direction.c_str(),
                (unsigned long)cfg.stopId);
  if (changedCb) changedCb();
  server->sendHeader("Location", "/trains?saved=1", true);
  server->send(303, "text/plain", "");
}

}  // namespace

void begin(WebServer& srv) {
  server = &srv;
  prefs.begin("trains", false);
  load();
  server->on("/trains", HTTP_GET, handlePage);
  server->on("/trains", HTTP_POST, handleSave);
  server->on("/trains/stops.json", HTTP_GET, handleStops);
}

const Config& config() { return cfg; }

void onConfigChanged(std::function<void()> cb) { changedCb = std::move(cb); }

}  // namespace trains
