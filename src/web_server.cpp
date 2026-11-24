#include "web_server.h"
#include "sd_manager.h"
#include "gif_player.h"
#include "wifi_manager.h"
#include "stream_server.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SD_MMC.h>

static AsyncWebServer server(80);

// ---- Web UI HTML (PROGMEM) ----

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>espGIF</title>
<style>
:root{--bg:#0a0a1a;--card:#111827;--border:#1e293b;--text:#e2e8f0;--muted:#64748b;--dim:#475569;--cyan:#67e8f9;--accent:#06b6d4;--green:#34d399;--red:#f87171;--purple:#a78bfa}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,sans-serif;background:var(--bg);color:var(--text);max-width:480px;margin:0 auto}
.hdr{text-align:center;padding:16px 16px 0}
.hdr h1{font-size:1.6em;font-weight:300;letter-spacing:2px;color:var(--cyan)}
.hdr .sub{font-size:.75em;color:var(--dim);margin-top:4px}
.badge{display:inline-block;padding:2px 8px;border-radius:10px;font-size:.7em;font-weight:600}
.badge.on{background:#065f46;color:#6ee7b7}
.badge.off{background:#1e293b;color:var(--muted)}
.badge.str{background:#164e63;color:var(--cyan)}
.tabs{display:flex;border-bottom:1px solid var(--border);margin:12px 16px 0;gap:0}
.tab{flex:1;padding:10px;text-align:center;font-size:.85em;font-weight:500;color:var(--muted);cursor:pointer;border-bottom:2px solid transparent;transition:all .2s}
.tab.active{color:var(--cyan);border-bottom-color:var(--cyan)}
.page{display:none;padding:12px 16px 16px}
.page.active{display:block}
.card{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:14px;margin-bottom:12px}
.card h3{font-size:.8em;font-weight:500;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;margin-bottom:10px}
.toast{position:fixed;top:16px;left:50%;transform:translateX(-50%);padding:10px 24px;border-radius:10px;font-size:.85em;z-index:99;opacity:0;transition:opacity .3s;pointer-events:none}
.toast.ok{background:#065f46;color:#6ee7b7;opacity:1}
.toast.er{background:#7c2d12;color:#fca5a5;opacity:1}
input[type=file]{color:#94a3b8;font-size:.8em;margin-bottom:8px;display:block;width:100%}
.inp{width:100%;padding:10px 12px;border-radius:8px;border:1px solid var(--border);background:#0f172a;color:var(--text);font-size:.85em;margin-bottom:8px}
select.inp{-webkit-appearance:none;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='10' height='6'%3E%3Cpath fill='%2364748b' d='M5 6L0 0h10z'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 12px center}
.btn{border:none;border-radius:8px;padding:8px 14px;cursor:pointer;font-weight:600;font-size:.8em;transition:transform .1s}
.btn:active{transform:scale(.95)}
.btn-p{background:var(--accent);color:#000}
.btn-d{background:var(--red);color:#fff}
.btn-r{background:var(--purple);color:#fff}
.btn-g{background:#059669;color:#fff}
.btn-o{background:var(--border);color:var(--cyan);border:1px solid #334155}
.btn-w{width:100%;padding:12px;font-size:.85em}
.prog{width:100%;height:4px;background:var(--border);border-radius:2px;margin:8px 0;display:none;overflow:hidden}
.prog div{height:100%;background:var(--accent);border-radius:2px;width:0%;transition:width .2s}
.gl{list-style:none}
.gl li{display:flex;align-items:center;gap:10px;padding:10px 0;border-bottom:1px solid var(--border)}
.gl li:last-child{border-bottom:none}
.gt{width:40px;height:72px;border-radius:6px;object-fit:cover;background:var(--border);flex-shrink:0}
.gi{flex:1;min-width:0}
.gi b{display:block;font-size:.85em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.gi small{color:var(--muted);font-size:.7em}
.gi.a b{color:var(--cyan)}
.gb{display:flex;gap:4px;flex-shrink:0}
.empty{text-align:center;color:var(--dim);padding:16px;font-size:.85em}
.wl{list-style:none}
.wl li{display:flex;align-items:center;justify-content:space-between;padding:8px 0;border-bottom:1px solid var(--border);font-size:.85em}
.wl li:last-child{border-bottom:none}
.wl .ws{color:var(--text)}
.wl .wr{font-size:.75em;color:var(--muted)}
</style>
</head>
<body>
<div class="hdr">
<h1>espGIF</h1>
<div class="sub"><span id="sp" class="badge off">Idle</span> <span id="ss" class="badge str" style="display:none">STREAMING</span> Free: <span id="sf">-</span></div>
</div>

<div class="tabs">
<div class="tab active" data-tab="gifs">GIFs</div>
<div class="tab" data-tab="wifi">WiFi</div>
</div>

<div class="toast" id="toast"></div>

<!-- GIFs Page -->
<div class="page active" id="p-gifs">
<div class="card">
<h3>Upload</h3>
<form id="uf">
<input type="file" name="file" id="fi" accept=".gif">
<div class="prog" id="pg"><div id="pb"></div></div>
<button type="submit" class="btn btn-p btn-w">Upload GIF</button>
</form>
</div>
<div class="card">
<h3>Library</h3>
<ul class="gl" id="gl"><li class="empty">Loading...</li></ul>
</div>
</div>

<!-- WiFi Page -->
<div class="page" id="p-wifi">
<div class="card">
<h3>Saved Networks</h3>
<ul class="wl" id="nlist"><li class="empty">Loading...</li></ul>
</div>
<div class="card">
<h3>Add Network</h3>
<button class="btn btn-o btn-w" id="wn" style="margin-bottom:8px">Scan for Networks</button>
<select class="inp" id="wl" style="display:none"><option value="">-- Select --</option></select>
<input type="text" class="inp" id="ws" placeholder="SSID">
<input type="password" class="inp" id="wp" placeholder="Password">
<button class="btn btn-g btn-w" id="wa">Add Network</button>
</div>
<div class="card">
<h3>AP Mode Settings</h3>
<label style="font-size:.7em;color:var(--muted);display:block;margin-bottom:4px">Network name when in AP mode</label>
<input type="text" class="inp" id="apn" placeholder="espGIF-Setup">
<button class="btn btn-p btn-w" id="aps">Save AP Name</button>
</div>
</div>

<script>
var G=document.getElementById.bind(document),Q=document.querySelectorAll.bind(document);
function toast(t,ok){var m=G('toast');m.textContent=t;m.className='toast '+(ok?'ok':'er');setTimeout(function(){m.className='toast'},2500)}
function sz(b){return b<1024?b+' B':b<1048576?(b/1024).toFixed(1)+' KB':(b/1048576).toFixed(1)+' MB'}
function xhr(method,url,body,cb){var x=new XMLHttpRequest();x.open(method,url);if(body)x.setRequestHeader('Content-Type','application/json');x.onload=function(){cb(x.status,x.responseText)};x.onerror=function(){cb(0,'')};x.send(body||null);return x}

// Tabs
Q('.tab').forEach(function(t){t.addEventListener('click',function(){
Q('.tab').forEach(function(x){x.classList.remove('active')});
Q('.page').forEach(function(x){x.classList.remove('active')});
t.classList.add('active');
G('p-'+t.dataset.tab).classList.add('active');
if(t.dataset.tab==='wifi')loadNetworks();
})});

// GIF list
function loadList(){
xhr('GET','/api/gifs',null,function(s,r){
try{
var gifs=JSON.parse(r);
xhr('GET','/api/status',null,function(s2,r2){
try{
var st=JSON.parse(r2);
var sp=G('sp');sp.textContent=st.current||'Idle';sp.className='badge '+(st.playing?'on':'off');
G('sf').textContent=sz(st.freeBytes);
G('ss').style.display=st.streaming?'inline-block':'none';
var h='';
if(!gifs.length){h='<li class="empty">No GIFs yet</li>';}
else{for(var i=0;i<gifs.length;i++){var g=gifs[i],ac=g.name===st.current?' a':'',rt=g.rotation||0;
h+='<li><img class="gt" src="/api/gifs/file?name='+encodeURIComponent(g.name)+'" loading="lazy" alt="">';
h+='<div class="gi'+ac+'"><b title="'+g.name+'">'+g.name+'</b><small>'+sz(g.size)+(rt?' &middot; '+rt+'&deg;':'')+'</small></div>';
h+='<div class="gb"><button class="btn btn-p" data-p="'+g.name+'">&#9654;</button>';
h+='<button class="btn btn-r" data-r="'+g.name+'" data-rv="'+rt+'">'+rt+'&deg;</button>';
h+='<button class="btn btn-d" data-d="'+g.name+'">&times;</button></div></li>';
}}
G('gl').innerHTML=h;
}catch(e){}});
}catch(e){G('gl').innerHTML='<li class="empty">Error</li>';}});
}

// GIF actions
G('gl').addEventListener('click',function(e){
var t=e.target.closest('[data-p],[data-r],[data-d]');if(!t)return;
if(t.dataset.p){xhr('POST','/api/select','{"name":"'+t.dataset.p+'"}',function(s){if(s===200){toast('Playing',true);loadList()}else toast('Failed',false)});}
if(t.dataset.r){var nxt=((parseInt(t.dataset.rv)||0)+90)%360;xhr('POST','/api/rotate','{"name":"'+t.dataset.r+'","rotation":'+nxt+'}',function(s){if(s===200){toast(nxt+'°',true);loadList()}else toast('Failed',false)});}
if(t.dataset.d){if(!confirm('Delete '+t.dataset.d+'?'))return;xhr('DELETE','/api/gifs?name='+encodeURIComponent(t.dataset.d),null,function(s){if(s===200){toast('Deleted',true);loadList()}else toast('Failed',false)});}
});

// Upload
G('uf').addEventListener('submit',function(e){
e.preventDefault();var f=G('fi').files[0];
if(!f){toast('Pick a file',false);return}
if(!f.name.toLowerCase().match(/\.gif$/)){toast('.gif only',false);return}
var fd=new FormData();fd.append('file',f);
var x=new XMLHttpRequest(),pg=G('pg'),pb=G('pb');
pg.style.display='block';
x.upload.onprogress=function(e){if(e.lengthComputable)pb.style.width=(e.loaded/e.total*100)+'%'};
x.onload=function(){pg.style.display='none';pb.style.width='0%';if(x.status===200){toast('Uploaded!',true);G('fi').value='';loadList()}else toast('Failed',false)};
x.onerror=function(){pg.style.display='none';toast('Error',false)};
x.open('POST','/api/upload');x.send(fd);
});

// WiFi saved networks
function loadNetworks(){
xhr('GET','/api/wifi/networks',null,function(s,r){
try{var nets=JSON.parse(r),h='';
if(!nets.length){h='<li class="empty">No saved networks</li>';}
else{for(var i=0;i<nets.length;i++){
h+='<li><span class="ws">'+nets[i].ssid+'</span>';
h+='<button class="btn btn-d" style="padding:4px 10px;font-size:.75em" data-rm="'+nets[i].index+'">&times;</button></li>';
}}
G('nlist').innerHTML=h;
}catch(e){}});
}
G('nlist').addEventListener('click',function(e){
var t=e.target.closest('[data-rm]');if(!t)return;
if(!confirm('Remove this network?'))return;
xhr('DELETE','/api/wifi/networks?index='+t.dataset.rm,null,function(s){if(s===200){toast('Removed',true);loadNetworks()}else toast('Failed',false)});
});

// Add network
G('wa').addEventListener('click',function(){
var ss=G('ws').value.trim(),pw=G('wp').value;
if(!ss){toast('Enter SSID',false);return}
xhr('POST','/api/wifi/networks','{"ssid":"'+ss+'","pass":"'+pw+'"}',function(s){
if(s===200){toast('Added! Reboot to connect.',true);G('ws').value='';G('wp').value='';loadNetworks()}
else toast('Failed',false)});
});

// WiFi scan (async polling)
var scanTimer=null;
G('wn').addEventListener('click',function(){
G('wn').textContent='Scanning...';G('wn').disabled=true;
xhr('POST','/api/wifiscan',null,function(){
pollScan();
});
});
function pollScan(){
clearTimeout(scanTimer);
xhr('GET','/api/wifiscan',null,function(s,r){
try{var d=JSON.parse(r);
if(d.scanning){scanTimer=setTimeout(pollScan,800);return}
G('wn').textContent='Scan for Networks';G('wn').disabled=false;
var nets=d.networks||[];
if(!nets.length){toast('No networks found',false);return}
var sel=G('wl');sel.innerHTML='<option value="">-- Select ('+nets.length+') --</option>';
nets.sort(function(a,b){return b.rssi-a.rssi});
for(var i=0;i<nets.length;i++){var o=document.createElement('option');o.value=nets[i].ssid;
o.textContent=nets[i].ssid+' ('+nets[i].rssi+'dBm)'+(nets[i].open?' [Open]':'');sel.appendChild(o)}
sel.style.display='block';toast(nets.length+' found',true);
}catch(e){G('wn').textContent='Scan for Networks';G('wn').disabled=false;toast('Error',false)}
});
}
G('wl').addEventListener('change',function(){if(this.value)G('ws').value=this.value});

// AP SSID
G('aps').addEventListener('click',function(){
var name=G('apn').value.trim();if(!name){toast('Enter name',false);return}
xhr('POST','/api/settings','{"ap_ssid":"'+name+'"}',function(s){if(s===200)toast('Saved!',true);else toast('Failed',false)});
});
xhr('GET','/api/settings',null,function(s,r){try{G('apn').value=JSON.parse(r).ap_ssid||''}catch(e){}});

loadList();
setInterval(loadList,10000);
</script>
</body>
</html>
)rawliteral";

// ---- Upload handler ----

static File uploadFile;

static void handleUpload(AsyncWebServerRequest* request, const String& filename,
                         size_t index, uint8_t* data, size_t len, bool final) {
    if (index == 0) {
        gifPlayerPause();
        String path = gifPath(filename);
        Serial.printf("Upload start: %s\n", path.c_str());
        uploadFile = SD_MMC.open(path, FILE_WRITE);
        if (!uploadFile) {
            Serial.println("Failed to open file for writing");
            gifPlayerResume();
            return;
        }
    }

    if (uploadFile && len > 0) {
        size_t written = uploadFile.write(data, len);
        if (written != len) {
            Serial.printf("Write error: %u of %u bytes\n", written, len);
        }
    }

    if (final) {
        if (uploadFile) {
            uploadFile.flush();
            Serial.printf("Upload complete: %u bytes\n", index + len);
            uploadFile.close();
        }
        gifPlayerResume();
    }
}

// ---- Server routes ----

void webServerInit() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", INDEX_HTML);
    });

    // List GIFs
    server.on("/api/gifs", HTTP_GET, [](AsyncWebServerRequest* request) {
        auto gifs = listGifs();
        String json = "[";
        for (size_t i = 0; i < gifs.size(); i++) {
            File f = SD_MMC.open(gifPath(gifs[i]), FILE_READ);
            size_t sz = f ? f.size() : 0;
            if (f) f.close();
            if (i > 0) json += ",";
            json += "{\"name\":\"" + gifs[i] + "\",\"size\":" + String(sz) + ",\"rotation\":" + String(getGifRotation(gifs[i])) + "}";
        }
        json += "]";
        request->send(200, "application/json", json);
    });

    // Serve GIF file for preview
    server.on("/api/gifs/file", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("name")) {
            request->send(400);
            return;
        }
        String name = request->getParam("name")->value();
        String path = gifPath(name);
        if (!SD_MMC.exists(path)) {
            request->send(404);
            return;
        }
        AsyncWebServerResponse* response = request->beginResponse(SD_MMC, path, "image/gif");
        response->addHeader("Cache-Control", "max-age=3600");
        request->send(response);
    });

    // Upload GIF
    server.on("/api/upload", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            request->send(200, "application/json", "{\"ok\":true}");
        },
        handleUpload
    );

    // Select GIF
    server.on("/api/select", HTTP_POST, [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            String body;
            body.reserve(total);
            body.concat((char*)data, len);
            if (index + len == total) {
                int nameStart = body.indexOf("\"name\"");
                if (nameStart < 0) { request->send(400, "application/json", "{\"error\":\"missing name\"}"); return; }
                int vs = body.indexOf('"', nameStart + 6);
                int ve = body.indexOf('"', vs + 1);
                if (vs < 0 || ve < 0) { request->send(400, "application/json", "{\"error\":\"bad json\"}"); return; }
                String name = body.substring(vs + 1, ve);
                if (!SD_MMC.exists(gifPath(name))) { request->send(404, "application/json", "{\"error\":\"not found\"}"); return; }
                gifPlayerRequestSwitch(name);
                request->send(200, "application/json", "{\"ok\":true}");
            }
        }
    );

    // Delete GIF
    server.on("/api/gifs", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("name")) { request->send(400, "application/json", "{\"error\":\"missing name\"}"); return; }
        String name = request->getParam("name")->value();
        if (deleteGif(name)) request->send(200, "application/json", "{\"ok\":true}");
        else request->send(404, "application/json", "{\"error\":\"not found\"}");
    });

    // Status
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = "{";
        json += "\"current\":\"" + gifPlayerCurrentName() + "\"";
        json += ",\"playing\":" + String(gifPlayerIsPlaying() ? "true" : "false");
        json += ",\"freeBytes\":" + String(getCardFreeBytes());
        json += ",\"totalBytes\":" + String(getCardTotalBytes());
        json += ",\"rssi\":" + String(wifiIsConnected() ? WiFi.RSSI() : 0);
        json += ",\"apMode\":" + String(wifiIsAPMode() ? "true" : "false");
        json += ",\"streaming\":" + String(streamIsActive() ? "true" : "false");
        json += ",\"uptime\":" + String(millis() / 1000);
        json += "}";
        request->send(200, "application/json", json);
    });

    // WiFi saved networks - list
    server.on("/api/wifi/networks", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", wifiGetNetworksJSON());
    });

    // WiFi saved networks - add
    server.on("/api/wifi/networks", HTTP_POST, [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            String body;
            body.reserve(total);
            body.concat((char*)data, len);
            if (index + len == total) {
                int ssidStart = body.indexOf("\"ssid\"");
                if (ssidStart < 0) { request->send(400, "application/json", "{\"error\":\"missing ssid\"}"); return; }
                int vs1 = body.indexOf('"', ssidStart + 6);
                int ve1 = body.indexOf('"', vs1 + 1);
                if (vs1 < 0 || ve1 < 0) { request->send(400, "application/json", "{\"error\":\"bad json\"}"); return; }
                String ssid = body.substring(vs1 + 1, ve1);
                String pass = "";
                int passStart = body.indexOf("\"pass\"");
                if (passStart >= 0) {
                    int vs2 = body.indexOf('"', passStart + 6);
                    int ve2 = body.indexOf('"', vs2 + 1);
                    if (vs2 >= 0 && ve2 > vs2) pass = body.substring(vs2 + 1, ve2);
                }
                if (wifiAddNetwork(ssid, pass)) request->send(200, "application/json", "{\"ok\":true}");
                else request->send(400, "application/json", "{\"error\":\"max networks reached\"}");
            }
        }
    );

    // WiFi saved networks - remove
    server.on("/api/wifi/networks", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("index")) { request->send(400, "application/json", "{\"error\":\"missing index\"}"); return; }
        int idx = request->getParam("index")->value().toInt();
        if (wifiRemoveNetwork(idx)) request->send(200, "application/json", "{\"ok\":true}");
        else request->send(404, "application/json", "{\"error\":\"invalid index\"}");
    });

    // WiFi scan - POST starts scan, GET returns results
    server.on("/api/wifiscan", HTTP_POST, [](AsyncWebServerRequest* request) {
        wifiStartScan();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/wifiscan", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", wifiGetScanResults());
    });

    // Settings (AP SSID)
    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = "{\"ap_ssid\":\"" + wifiGetAPSSID() + "\"}";
        request->send(200, "application/json", json);
    });

    server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            String body;
            body.reserve(total);
            body.concat((char*)data, len);
            if (index + len == total) {
                int apStart = body.indexOf("\"ap_ssid\"");
                if (apStart >= 0) {
                    int vs = body.indexOf('"', apStart + 9);
                    int ve = body.indexOf('"', vs + 1);
                    if (vs >= 0 && ve > vs) {
                        wifiSetAPSSID(body.substring(vs + 1, ve));
                        request->send(200, "application/json", "{\"ok\":true}");
                        return;
                    }
                }
                request->send(400, "application/json", "{\"error\":\"bad json\"}");
            }
        }
    );

    // Set GIF rotation
    server.on("/api/rotate", HTTP_POST, [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            String body;
            body.reserve(total);
            body.concat((char*)data, len);
            if (index + len == total) {
                int nameStart = body.indexOf("\"name\"");
                int rotStart = body.indexOf("\"rotation\"");
                if (nameStart < 0 || rotStart < 0) { request->send(400, "application/json", "{\"error\":\"bad json\"}"); return; }
                int vs = body.indexOf('"', nameStart + 6);
                int ve = body.indexOf('"', vs + 1);
                if (vs < 0 || ve < 0) { request->send(400, "application/json", "{\"error\":\"bad name\"}"); return; }
                String name = body.substring(vs + 1, ve);
                int rvStart = rotStart + 11;
                while (rvStart < (int)body.length() && (body[rvStart] == ':' || body[rvStart] == ' ')) rvStart++;
                int rvEnd = rvStart;
                while (rvEnd < (int)body.length() && (isDigit(body[rvEnd]) || body[rvEnd] == '-')) rvEnd++;
                int rotation = body.substring(rvStart, rvEnd).toInt();
                setGifRotation(name, rotation);
                if (name == gifPlayerCurrentName()) gifPlayerRequestSwitch(name);
                request->send(200, "application/json", "{\"ok\":true,\"rotation\":" + String(rotation) + "}");
            }
        }
    );

    server.begin();
    Serial.println("Web server started on port 80");
}
