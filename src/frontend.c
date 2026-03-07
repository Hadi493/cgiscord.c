#include <stddef.h>

/*
 * VAD parameters (mirrored in JS):
 *   RMS_THRESHOLD  = 0.015  -- energy level to trigger voice (0.0-1.0)
 *   HANGOVER_MS    = 400    -- keep transmitting this long after silence
 *   FRAME_SIZE     = 1024   -- samples per ScriptProcessor callback
 *   SAMPLE_RATE    = 16000  -- Hz, 16kHz mono PCM
 *
 * These are conservative defaults. Lower RMS_THRESHOLD if VAD cuts out
 * quiet speakers. Raise if background noise triggers false positives.
 */

const char FRONTEND_HTML[] =
"<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>CGiscord</title>"
"<style>"
":root{"
"--bg:#0e0f11;--bg2:#1a1b1e;--bg3:#2b2d31;"
"--sidebar:#1e1f22;--accent:#5865f2;--accent2:#4752c4;"
"--text:#dcddde;--text2:#96989d;--text3:#72767d;"
"--green:#23a55a;--red:#f23f43;--yellow:#f0b132;"
"--border:#1a1b1e;"
"}"
"*{margin:0;padding:0;box-sizing:border-box;}"
"body{background:var(--bg);color:var(--text);font-family:'Segoe UI',system-ui,sans-serif;height:100vh;display:flex;overflow:hidden;}"

/* auth */
"#auth{position:fixed;inset:0;background:var(--bg);display:flex;align-items:center;justify-content:center;z-index:100;}"
"#auth-box{background:var(--bg2);border-radius:8px;padding:32px;width:420px;box-shadow:0 8px 48px rgba(0,0,0,.6);}"
"#auth-box h1{font-size:26px;font-weight:700;text-align:center;margin-bottom:4px;letter-spacing:-.5px;}"
"#auth-box .sub{color:var(--text2);text-align:center;margin-bottom:28px;font-size:14px;}"
".field{margin-bottom:16px;}"
".field label{display:block;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.5px;color:var(--text2);margin-bottom:6px;}"
".field input{width:100%;background:var(--bg);border:1px solid #3f4147;color:var(--text);padding:10px 12px;border-radius:3px;font-size:16px;outline:none;transition:border-color .15s;}"
".field input:focus{border-color:var(--accent);}"
".abtn{width:100%;padding:11px;background:var(--accent);color:#fff;border:none;border-radius:3px;font-size:16px;font-weight:600;cursor:pointer;transition:background .15s,transform .1s;}"
".abtn:hover{background:var(--accent2);}"
".abtn:active{transform:scale(.98);}"
"#auth-err{color:var(--red);font-size:13px;text-align:center;margin-top:8px;min-height:18px;}"
"#auth-sw{text-align:center;margin-top:16px;font-size:14px;color:var(--text2);}"
"#auth-sw a{color:var(--accent);cursor:pointer;}"

/* main layout */
"#app{display:none;width:100%;height:100%;}"
"#sidebar{width:240px;background:var(--sidebar);display:flex;flex-direction:column;flex-shrink:0;}"
"#server-hdr{padding:0 16px;height:48px;display:flex;align-items:center;font-weight:700;font-size:15px;border-bottom:2px solid var(--bg);box-shadow:0 1px 0 rgba(0,0,0,.2);gap:8px;}"
"#ch-list{flex:1;overflow-y:auto;padding:8px 0;}"
"#ch-list::-webkit-scrollbar{width:4px;}"
"#ch-list::-webkit-scrollbar-thumb{background:var(--bg3);border-radius:2px;}"
".ch-sec{padding:16px 8px 4px;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.5px;color:var(--text3);display:flex;align-items:center;gap:4px;}"
".ch-sec button{background:none;border:none;color:var(--text3);cursor:pointer;margin-left:auto;font-size:16px;line-height:1;}"
".ch-sec button:hover{color:var(--text2);}"
".chi{display:flex;align-items:center;gap:6px;padding:2px 8px;margin:0 8px;border-radius:4px;cursor:pointer;color:var(--text3);font-size:15px;transition:background .1s,color .1s;height:34px;}"
".chi:hover{background:rgba(255,255,255,.06);color:var(--text2);}"
".chi.active{background:rgba(255,255,255,.1);color:var(--text);}"
".chi .ico{flex-shrink:0;font-size:18px;color:var(--text3);}"
".chi.active .ico{color:var(--text2);}"

/* voice channel members */
".vmembers{margin:0 8px 4px;}"
".vmember{display:flex;align-items:center;gap:8px;padding:3px 8px;border-radius:4px;height:32px;transition:background .1s;}"
".vmember:hover{background:rgba(255,255,255,.04);}"
".vavatar{width:24px;height:24px;border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:10px;font-weight:700;flex-shrink:0;position:relative;}"
".vavatar::after{content:'';position:absolute;bottom:-1px;right:-1px;width:9px;height:9px;border-radius:50%;background:var(--green);border:2px solid var(--sidebar);opacity:0;transition:opacity .2s;}"
".vavatar.speaking::after{opacity:1;animation:pulse 1s infinite;}"
".vavatar.muted::after{background:var(--red);opacity:1;animation:none;}"
"@keyframes pulse{0%,100%{box-shadow:0 0 0 0 rgba(35,165,90,.4);}50%{box-shadow:0 0 0 4px rgba(35,165,90,0);}}"
".vname{font-size:14px;color:var(--text2);flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}"
".vname.speaking{color:var(--text);}"
".vicons{display:flex;gap:2px;}"
".vicon{font-size:13px;color:var(--text3);}"

/* voice control bar */
"#voice-bar{display:none;background:#232428;border-top:1px solid var(--border);}"
"#vbar-top{padding:8px 10px;display:flex;align-items:center;gap:6px;}"
"#vbar-status{flex:1;min-width:0;}"
"#vbar-ch{font-size:13px;font-weight:600;color:var(--green);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}"
"#vbar-label{font-size:11px;color:var(--text3);margin-top:1px;}"
".vctrl{background:none;border:none;cursor:pointer;width:32px;height:32px;border-radius:4px;display:flex;align-items:center;justify-content:center;font-size:16px;color:var(--text2);transition:background .1s,color .1s;}"
".vctrl:hover{background:rgba(255,255,255,.08);color:var(--text);}"
".vctrl.danger:hover{background:rgba(242,63,67,.15);color:var(--red);}"
".vctrl.muted-on{color:var(--red);}"
"#vad-meter{height:3px;margin:0 10px 8px;background:var(--bg3);border-radius:2px;overflow:hidden;}"
"#vad-bar{height:100%;width:0%;background:var(--green);border-radius:2px;transition:width .05s;}"
"#vad-bar.active{background:var(--green);}"
"#vad-bar.speaking{background:var(--green);box-shadow:0 0 4px var(--green);}"

/* user bar */
"#user-bar{padding:8px;background:#232428;display:flex;align-items:center;gap:6px;}"
"#my-avatar{width:32px;height:32px;border-radius:50%;display:flex;align-items:center;justify-content:center;font-weight:700;font-size:13px;flex-shrink:0;}"
"#my-info{flex:1;overflow:hidden;}"
"#my-name{font-size:13px;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}"
"#my-status{font-size:11px;color:var(--text3);}"
".ubtn{background:none;border:none;color:var(--text3);cursor:pointer;width:32px;height:32px;border-radius:4px;display:flex;align-items:center;justify-content:center;font-size:15px;transition:background .1s,color .1s;}"
".ubtn:hover{background:rgba(255,255,255,.08);color:var(--text2);}"

/* main chat */
"#main{flex:1;display:flex;flex-direction:column;overflow:hidden;background:var(--bg2);}"
"#chat-hdr{height:48px;padding:0 16px;display:flex;align-items:center;gap:8px;border-bottom:1px solid var(--border);box-shadow:0 1px 0 rgba(0,0,0,.2);flex-shrink:0;}"
"#chat-hdr .ico{font-size:20px;color:var(--text3);}"
"#chat-hdr h2{font-size:16px;font-weight:600;}"
"#msgs{flex:1;overflow-y:auto;padding:16px 0;display:flex;flex-direction:column;}"
"#msgs::-webkit-scrollbar{width:8px;}"
"#msgs::-webkit-scrollbar-track{background:transparent;}"
"#msgs::-webkit-scrollbar-thumb{background:var(--bg3);border-radius:4px;}"
".msg{display:flex;gap:16px;padding:2px 16px;border-radius:0;transition:background .05s;}"
".msg:hover{background:rgba(0,0,0,.06);}"
".msg.first{margin-top:16px;}"
".mavatar{width:40px;height:40px;border-radius:50%;display:flex;align-items:center;justify-content:center;font-weight:700;font-size:16px;flex-shrink:0;margin-top:2px;}"
".mbody{flex:1;min-width:0;}"
".mhdr{display:flex;align-items:baseline;gap:8px;margin-bottom:2px;}"
".muser{font-weight:600;font-size:15px;}"
".mtime{font-size:11px;color:var(--text3);}"
".mtext{font-size:15px;line-height:1.5;color:var(--text);word-break:break-word;}"
".msg.cont .mavatar{visibility:hidden;}"
".msg.cont .mhdr{display:none;}"
".sys-msg{padding:4px 16px;font-size:13px;color:var(--text3);font-style:italic;display:flex;align-items:center;gap:8px;}"
".sys-msg::before{content:'';display:block;height:1px;background:var(--border);width:20px;flex-shrink:0;}"
"#typing{height:24px;padding:0 16px;font-size:13px;color:var(--text3);display:flex;align-items:center;gap:6px;}"
"#input-wrap{padding:0 16px 16px;flex-shrink:0;}"
"#input-box{background:var(--bg3);border-radius:8px;display:flex;align-items:center;padding:0 12px;}"
"#msg-in{flex:1;background:none;border:none;color:var(--text);font-size:15px;padding:12px 0;outline:none;}"
"#msg-in::placeholder{color:var(--text3);}"
"#send-btn{background:none;border:none;color:var(--text3);cursor:pointer;padding:8px 4px;font-size:18px;transition:color .1s;}"
"#send-btn:hover{color:var(--accent);}"
"#no-ch{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:16px;color:var(--text3);}"
"#no-ch .big{font-size:64px;line-height:1;}"
"#no-ch p{font-size:15px;}"
"</style>"
"</head>"
"<body>"

/* auth screen */
"<div id='auth'>"
"<div id='auth-box'>"
"<h1>⚡ CGiscord</h1>"
"<p class='sub' id='auth-sub'>Your private server</p>"
"<div class='field'><label>Username</label><input id='un' type='text' autocomplete='off' placeholder='coolname'></div>"
"<div class='field'><label>Password</label><input id='pw' type='password' placeholder='••••••••'></div>"
"<button class='abtn' id='auth-btn'>Log In</button>"
"<div id='auth-err'></div>"
"<div id='auth-sw'>No account? <a id='sw'>Register</a></div>"
"</div>"
"</div>"

/* main app */
"<div id='app'>"
"<div id='sidebar'>"
"<div id='server-hdr'><span>⚡</span><span>CGiscord</span></div>"
"<div id='ch-list'></div>"
"<div id='voice-bar'>"
"<div id='vbar-top'>"
"<div id='vbar-status'>"
"<div id='vbar-ch'>Voice</div>"
"<div id='vbar-label'>Connected — VAD active</div>"
"</div>"
"<button class='vctrl' id='mute-btn' title='Mute/Unmute'>🎤</button>"
"<button class='vctrl danger' id='leave-btn' title='Disconnect voice'>📵</button>"
"</div>"
"<div id='vad-meter'><div id='vad-bar'></div></div>"
"</div>"
"<div id='user-bar'>"
"<div id='my-avatar'>?</div>"
"<div id='my-info'><div id='my-name'>...</div><div id='my-status'>● Online</div></div>"
"<button class='ubtn' id='logout-btn' title='Logout'>⏻</button>"
"</div>"
"</div>"
"<div id='main'>"
"<div id='no-ch'><div class='big'>👈</div><p>Select a channel</p></div>"
"<div id='chat-view' style='display:none;flex:1;flex-direction:column;overflow:hidden;'>"
"<div id='chat-hdr'><span class='ico'>#</span><h2 id='ch-name'>general</h2></div>"
"<div id='msgs'></div>"
"<div id='typing'></div>"
"<div id='input-wrap'><div id='input-box'>"
"<input id='msg-in' placeholder='Message #general' maxlength='2000'>"
"<button id='send-btn'>➤</button>"
"</div></div>"
"</div>"
"</div>"
"</div>"

"<script>"
/* ------------------------------------------------------------------ */
/* Config                                                               */
/* ------------------------------------------------------------------ */
"const VAD_RMS_THRESHOLD = 0.015;" /* energy gate — tune per environment */
"const VAD_HANGOVER_MS   = 400;"   /* keep transmitting after silence    */
"const SAMPLE_RATE       = 16000;"
"const FRAME_SIZE        = 1024;"  /* ScriptProcessor buffer size        */

/* ------------------------------------------------------------------ */
/* State                                                                */
/* ------------------------------------------------------------------ */
"let ws=null, token=null, username=null, userId=0;"
"let currentCh=null, lastMsgUser=null, typingTmr=null;"
"let isReg=false;"

/* voice */
"let inVoice=false, voiceChId=null, voiceChName='';"
"let audioCtx=null, micStream=null, micSrc=null, processor=null;"
"let isMuted=false;"
"let voiceUsers={};" /* id -> {id, name, muted, speaking} */
"let speakTimers={};"

/* VAD state */
"let vadActive=false;"          /* are we currently transmitting? */
"let vadHangoverTimer=null;"    /* timeout to stop transmitting   */
"let lastRms=0;"                /* for meter display              */

/* audio playback jitter buffer per sender */
"let playbackTime={};" /* sender_id -> next scheduled play time in audioCtx */

/* ------------------------------------------------------------------ */
/* Auth                                                                 */
/* ------------------------------------------------------------------ */
"document.getElementById('sw').onclick=()=>{"
"isReg=!isReg;"
"document.getElementById('auth-btn').textContent=isReg?'Create Account':'Log In';"
"document.getElementById('auth-sub').textContent=isReg?'Create your account':'Your private server';"
"document.getElementById('sw').textContent=isReg?'Login':'Register';"
"document.querySelector('#auth-sw').firstChild.textContent=isReg?'Have an account? ':\"No account? \";"
"document.getElementById('auth-err').textContent='';"
"};"

"document.getElementById('auth-btn').onclick=async()=>{"
"const u=document.getElementById('un').value.trim();"
"const p=document.getElementById('pw').value;"
"const btn=document.getElementById('auth-btn');"
"if(!u||!p){document.getElementById('auth-err').textContent='Fill both fields';return;}"
"btn.disabled=true;btn.textContent='...';"
"try{"
"const r=await fetch('/api/'+(isReg?'register':'login'),{"
"method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({username:u,password:p})});"
"const d=await r.json();"
"if(d.error){document.getElementById('auth-err').textContent=d.error;return;}"
"token=d.token;username=d.username;userId=d.id||0;"
"localStorage.setItem('ct',token);localStorage.setItem('cu',username);localStorage.setItem('ci',userId);"
"boot();"
"}catch(e){document.getElementById('auth-err').textContent='Connection error';}"
"finally{btn.disabled=false;btn.textContent=isReg?'Create Account':'Log In';}"
"};"

"document.getElementById('pw').onkeydown=e=>{if(e.key==='Enter')document.getElementById('auth-btn').click();};"
"document.getElementById('un').onkeydown=e=>{if(e.key==='Enter')document.getElementById('pw').focus();};"

"function boot(){"
"document.getElementById('auth').style.display='none';"
"document.getElementById('app').style.display='flex';"
"document.getElementById('my-name').textContent=username;"
"const av=document.getElementById('my-avatar');"
"av.textContent=username[0].toUpperCase();av.style.background=strColor(username);"
"connectWS();"
"}"

/* ------------------------------------------------------------------ */
/* WebSocket — internet-aware URL                                       */
/* ------------------------------------------------------------------ */
"function wsUrl(){"
/* Use wss:// when served over HTTPS (e.g. via nginx TLS proxy), ws:// otherwise */
"const proto=location.protocol==='https:'?'wss':'ws';"
"return proto+'://'+location.host+'/ws';"
"}"

"function connectWS(){"
"ws=new WebSocket(wsUrl());"
"ws.binaryType='arraybuffer';"
"ws.onopen=()=>ws.send(JSON.stringify({type:'auth',token}));"
"ws.onmessage=e=>{"
"if(e.data instanceof ArrayBuffer){onAudioFrame(e.data);return;}"
"try{onMsg(JSON.parse(e.data));}catch(x){console.error('[ws]',x);}"
"};"
"ws.onclose=()=>{"
"if(inVoice)voiceDisconnect();"
"setTimeout(connectWS,2000);"
"};"
"ws.onerror=e=>console.error('[ws error]',e);"
"}"

"function onMsg(m){"
"({channels:()=>renderChannels(m.channels),"
"history:()=>renderHistory(m.messages),"
"message:()=>appendMsg(m,false),"
"system:()=>sysMsg(m.text),"
"typing:()=>showTyping(m.user),"
"voice_state:()=>onVoiceState(m),"
"voice_speaking:()=>onVoiceSpeaking(m),"
"voice_left:()=>onVoiceLeft(m)"
"}[m.type]||function(){})();"
"}"

/* ------------------------------------------------------------------ */
/* Channel rendering — text + voice                                     */
/* ------------------------------------------------------------------ */
"const VCH=[{id:101,name:'General Voice'},{id:102,name:'Gaming'},{id:103,name:'AFK'}];"

"function renderChannels(channels){"
"const list=document.getElementById('ch-list');"
"list.innerHTML='';"
"mkSec(list,'Text Channels');"
"channels.forEach(ch=>{"
"const el=mkChi('#',ch.id,false);el.querySelector('span:last-child').textContent=ch.name;"
"el.onclick=()=>joinCh(ch.id,ch.name);list.appendChild(el);"
"});"
"mkSec(list,'Voice Channels');"
"VCH.forEach(vc=>{"
"const el=mkChi('🔊',vc.id,true);el.querySelector('span:last-child').textContent=vc.name;"
"el.onclick=()=>joinVoice(vc.id,vc.name);list.appendChild(el);"
"const mb=document.createElement('div');mb.className='vmembers';mb.id='vm-'+vc.id;"
"list.appendChild(mb);"
"});"
"if(channels.length)joinCh(channels[0].id,channels[0].name);"
"}"

"function mkSec(parent,text){"
"const d=document.createElement('div');d.className='ch-sec';d.textContent=text;"
"parent.appendChild(d);"
"}"

"function mkChi(icon,id,isVoice){"
"const el=document.createElement('div');el.className='chi';"
"el.innerHTML='<span class=\"ico\">'+icon+'</span><span></span>';"
"if(isVoice)el.dataset.vid=id;else el.dataset.id=id;"
"return el;"
"}"

"function joinCh(id,name){"
"currentCh=id;lastMsgUser=null;"
"document.querySelectorAll('.chi[data-id]').forEach(e=>e.classList.toggle('active',parseInt(e.dataset.id)===id));"
"document.getElementById('ch-name').textContent=name;"
"document.getElementById('msg-in').placeholder='Message #'+name;"
"document.getElementById('msgs').innerHTML='';"
"document.getElementById('no-ch').style.display='none';"
"const cv=document.getElementById('chat-view');"
"cv.style.display='flex';cv.style.flexDirection='column';cv.style.overflow='hidden';"
"ws.send(JSON.stringify({type:'join',channel_id:String(id)}));"
"}"

/* ------------------------------------------------------------------ */
/* Chat                                                                 */
/* ------------------------------------------------------------------ */
"function renderHistory(msgs){const el=document.getElementById('msgs');el.innerHTML='';lastMsgUser=null;msgs.forEach(m=>appendMsg(m,true));el.scrollTop=el.scrollHeight;}"

"function appendMsg(m,hist){"
"const el=document.getElementById('msgs');"
"const cont=m.user===lastMsgUser;lastMsgUser=m.user;"
"const d=document.createElement('div');"
"d.className='msg'+(cont?'':' first')+(cont?' cont':'');"
"const ts=new Date(m.ts*1000).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'});"
"d.innerHTML='<div class=\"mavatar\" style=\"background:'+strColor(m.user)+'\">'+esc(m.user[0].toUpperCase())+'</div>'"
"+'<div class=\"mbody\"><div class=\"mhdr\"><span class=\"muser\">'+esc(m.user)+'</span><span class=\"mtime\">'+ts+'</span></div>'"
"+'<div class=\"mtext\">'+esc(m.text)+'</div></div>';"
"el.appendChild(d);"
"if(!hist)el.scrollTop=el.scrollHeight;"
"}"

"function sysMsg(t){"
"const el=document.getElementById('msgs');lastMsgUser=null;"
"const d=document.createElement('div');d.className='sys-msg';d.textContent=t;"
"el.appendChild(d);el.scrollTop=el.scrollHeight;"
"}"

"function showTyping(u){"
"if(u===username)return;"
"const el=document.getElementById('typing');"
"el.innerHTML='<span style=\"color:var(--text3);font-size:12px;\">●●●</span> '+esc(u)+' is typing...';"
"clearTimeout(typingTmr);typingTmr=setTimeout(()=>el.innerHTML='',3000);"
"}"

"document.getElementById('send-btn').onclick=sendMsg;"
"document.getElementById('msg-in').onkeydown=e=>{"
"if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();sendMsg();return;}"
"if(ws&&ws.readyState===1)ws.send(JSON.stringify({type:'typing'}));"
"};"
"function sendMsg(){"
"const t=document.getElementById('msg-in').value.trim();"
"if(!t||!ws||ws.readyState!==1)return;"
"ws.send(JSON.stringify({type:'message',text:t}));"
"document.getElementById('msg-in').value='';"
"}"

"document.getElementById('logout-btn').onclick=()=>{localStorage.clear();if(ws)ws.close();location.reload();};"

/* ------------------------------------------------------------------ */
/* VOICE — join / leave                                                 */
/* ------------------------------------------------------------------ */
"async function joinVoice(chId,chName){"
"if(inVoice&&voiceChId===chId)return;"
"if(inVoice)leaveVoice();"
"try{"
"micStream=await navigator.mediaDevices.getUserMedia({"
"audio:{sampleRate:SAMPLE_RATE,channelCount:1,"
"echoCancellation:true,noiseSuppression:true,autoGainControl:true},"
"video:false"
"});"
"}catch(e){sysMsg('⚠ Mic: '+e.message);return;}"
"inVoice=true;voiceChId=chId;voiceChName=chName;"
"ws.send(JSON.stringify({type:'voice_join',channel_id:String(chId)}));"
"startCapture();"
"document.getElementById('voice-bar').style.display='block';"
"document.getElementById('vbar-ch').textContent='🟢 '+chName;"
"document.getElementById('vbar-label').textContent='Connected — VAD active';"
"document.querySelectorAll('.chi[data-vid]').forEach(e=>e.classList.toggle('active',parseInt(e.dataset.vid)===chId));"
"}"

"function leaveVoice(){"
"if(!inVoice)return;"
"ws.send(JSON.stringify({type:'voice_leave'}));"
"voiceDisconnect();"
"}"

"function voiceDisconnect(){"
"const oldCh=voiceChId;"
"inVoice=false;voiceChId=null;voiceChName='';"
"voiceUsers={};playbackTime={};"
"clearTimeout(vadHangoverTimer);vadActive=false;"
"stopCapture();"
"document.getElementById('voice-bar').style.display='none';"
"document.querySelectorAll('.chi[data-vid]').forEach(e=>e.classList.remove('active'));"
"if(oldCh){const m=document.getElementById('vm-'+oldCh);if(m)m.innerHTML='';}"
"}"

/* ------------------------------------------------------------------ */
/* VOICE — audio capture with VAD                                       */
/* ------------------------------------------------------------------ */
"function startCapture(){"
"if(audioCtx){try{audioCtx.close();}catch(_){}}"
"audioCtx=new(window.AudioContext||window.webkitAudioContext)({sampleRate:SAMPLE_RATE});"
"micSrc=audioCtx.createMediaStreamSource(micStream);"
"processor=audioCtx.createScriptProcessor(FRAME_SIZE,1,1);"

"processor.onaudioprocess=evt=>{"
"if(!ws||ws.readyState!==1||!inVoice||isMuted)return;"
"const samples=evt.inputBuffer.getChannelData(0);"

/* RMS energy calculation */
"let sum=0;"
"for(let i=0;i<samples.length;i++)sum+=samples[i]*samples[i];"
"const rms=Math.sqrt(sum/samples.length);"
"lastRms=rms;"
"updateVadMeter(rms);"

"if(rms>=VAD_RMS_THRESHOLD){"
/* Voice detected — start or continue transmitting */
"clearTimeout(vadHangoverTimer);"
"vadHangoverTimer=null;"
"if(!vadActive)vadActive=true;"

/* Convert Float32 → Int16 PCM and send */
"const pcm=new Int16Array(samples.length);"
"for(let i=0;i<samples.length;i++){"
"const s=Math.max(-1,Math.min(1,samples[i]));"
"pcm[i]=s<0?s*0x8000:s*0x7FFF;"
"}"
"ws.send(pcm.buffer);"
"}else if(vadActive&&!vadHangoverTimer){"
/* Below threshold — start hangover timer before stopping */
"vadHangoverTimer=setTimeout(()=>{vadActive=false;vadHangoverTimer=null;},VAD_HANGOVER_MS);"
/* Still transmit during hangover to avoid clipping */
"const pcm=new Int16Array(samples.length);"
"for(let i=0;i<samples.length;i++){const s=Math.max(-1,Math.min(1,samples[i]));pcm[i]=s<0?s*0x8000:s*0x7FFF;}"
"ws.send(pcm.buffer);"
"}"
"};"

"micSrc.connect(processor);"
"processor.connect(audioCtx.destination);"
"}"

"function stopCapture(){"
"clearTimeout(vadHangoverTimer);vadHangoverTimer=null;vadActive=false;"
"try{"
"if(processor){processor.disconnect();processor=null;}"
"if(micSrc){micSrc.disconnect();micSrc=null;}"
"if(audioCtx){audioCtx.close();audioCtx=null;}"
"}catch(_){}"
"if(micStream){micStream.getTracks().forEach(t=>t.stop());micStream=null;}"
"updateVadMeter(0);"
"}"

"function updateVadMeter(rms){"
"const pct=Math.min(100,rms/0.1*100);"
"const bar=document.getElementById('vad-bar');"
"bar.style.width=pct+'%';"
"bar.className=''+( rms>=VAD_RMS_THRESHOLD?'active speaking':'');"
"}"

/* ------------------------------------------------------------------ */
/* VOICE — audio playback with simple jitter buffer                     */
/* ------------------------------------------------------------------ */
"function onAudioFrame(buf){"
"if(!audioCtx||buf.byteLength<6)return;" /* 4 bytes sender_id + at least 2 bytes audio */

"const view=new DataView(buf);"
/* 4-byte big-endian sender user_id */
"const senderId=(view.getUint8(0)<<24|view.getUint8(1)<<16|view.getUint8(2)<<8|view.getUint8(3))>>>0;"
"const pcmByteLen=buf.byteLength-4;"
"const sampleCount=pcmByteLen>>1;" /* each sample is 2 bytes */
"if(sampleCount<1)return;"

"const ab=audioCtx.createBuffer(1,sampleCount,SAMPLE_RATE);"
"const ch=ab.getChannelData(0);"
"for(let i=0;i<sampleCount;i++)ch[i]=view.getInt16(4+i*2,true)/32768.0;"

"const src=audioCtx.createBufferSource();"
"src.buffer=ab;"

/* Simple jitter buffer: schedule frames 60ms ahead, chaining them */
"const now=audioCtx.currentTime;"
"const JITTER=0.06;" /* 60ms jitter buffer */
"if(!playbackTime[senderId]||playbackTime[senderId]<now+JITTER){"
"playbackTime[senderId]=now+JITTER;"
"}"
"src.connect(audioCtx.destination);"
"src.start(playbackTime[senderId]);"
"playbackTime[senderId]+=ab.duration;"
"}"

/* ------------------------------------------------------------------ */
/* VOICE — mute / leave controls                                        */
/* ------------------------------------------------------------------ */
"document.getElementById('mute-btn').onclick=()=>{"
"isMuted=!isMuted;"
"ws.send(JSON.stringify({type:'voice_mute'}));"
"const btn=document.getElementById('mute-btn');"
"btn.textContent=isMuted?'🔇':'🎤';"
"btn.classList.toggle('muted-on',isMuted);"
"if(micStream)micStream.getAudioTracks().forEach(t=>t.enabled=!isMuted);"
"if(isMuted){clearTimeout(vadHangoverTimer);vadActive=false;}"
"};"
"document.getElementById('leave-btn').onclick=leaveVoice;"

/* ------------------------------------------------------------------ */
/* VOICE — state sync from server                                       */
/* ------------------------------------------------------------------ */
"function onVoiceState(m){"
"if(m.channel_id===0){voiceDisconnect();return;}"
"voiceUsers={};"
"m.users.forEach(u=>{voiceUsers[u.id]=u;});"
"renderVoiceMembers(m.channel_id,m.users);"
"}"

"function onVoiceSpeaking(m){"
"if(!voiceUsers[m.user_id])voiceUsers[m.user_id]={id:m.user_id,name:m.username,muted:false,speaking:false};"
"voiceUsers[m.user_id].speaking=m.speaking;"
"const av=document.getElementById('vav-'+m.user_id);"
"const nm=document.getElementById('vnm-'+m.user_id);"
"if(av){av.classList.toggle('speaking',m.speaking);}"
"if(nm){nm.classList.toggle('speaking',m.speaking);}"
"}"

"function onVoiceLeft(m){"
"delete voiceUsers[m.user_id];"
"renderVoiceMembers(m.channel_id,Object.values(voiceUsers));"
"}"

"function renderVoiceMembers(chId,users){"
"const el=document.getElementById('vm-'+chId);if(!el)return;"
"el.innerHTML='';"
"users.forEach(u=>{"
"const d=document.createElement('div');d.className='vmember';"
"const avColor=strColor(u.name);"
"d.innerHTML="
"'<div class=\"vavatar'+(u.speaking?' speaking':'')+(u.muted?' muted':'')+'\" id=\"vav-'+u.id+'\" style=\"background:'+avColor+'\">'+esc(u.name[0].toUpperCase())+'</div>'"
"+'<span class=\"vname'+(u.speaking?' speaking':'')+'\" id=\"vnm-'+u.id+'\">'+esc(u.name)+'</span>'"
"+(u.muted?'<span class=\"vicon\">🔇</span>':'');"
"el.appendChild(d);"
"});"
"}"

/* ------------------------------------------------------------------ */
/* Utils                                                                */
/* ------------------------------------------------------------------ */
"function strColor(s){"
"let h=0;for(let i=0;i<s.length;i++)h=(h*31+s.charCodeAt(i))&0xffffffff;"
"const c=['#5865f2','#23a55a','#f0b132','#f23f43','#eb459e','#00b0f4','#57f287'];"
"return c[Math.abs(h)%c.length];"
"}"

"function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}"

/* ------------------------------------------------------------------ */
/* Auto-login from localStorage                                         */
/* ------------------------------------------------------------------ */
"(()=>{"
"const t=localStorage.getItem('ct');"
"const u=localStorage.getItem('cu');"
"const i=localStorage.getItem('ci');"
"if(t&&u){token=t;username=u;userId=parseInt(i)||0;boot();}"
"})();"
"</script>"
"</body></html>";
