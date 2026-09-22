#pragma once

namespace esphome::openquatt_recovery {

// Self-contained by design: never load the main application, entities or logs.
static constexpr const char RECOVERY_PAGE[] = R"HTML(<!doctype html>
<html lang="nl"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenQuatt herstel</title>
<style>
body{font:16px system-ui,sans-serif;max-width:36rem;margin:3rem auto;padding:0 1rem;color:#172d35;background:#f5f7f8}
label,input,button{display:block}input,button{font:inherit;padding:.7rem;margin:.5rem 0 1rem;box-sizing:border-box;max-width:100%}
input{width:100%}button{cursor:pointer}small{color:#475f68}#message{min-height:2em}fieldset{border:0;padding:0}
</style>
<h1>OpenQuatt herstel</h1>
<p>Dit scherm geeft uitsluitend toegang tot herstelacties. De gewone webinterface blijft afgeschermd.</p>
<p id="status">Status ophalen…</p><p id="message" role="status" aria-live="polite"></p>
<fieldset id="actions" disabled><form id="login">
<h2>Web-login instellen</h2>
<label for="username">Gebruikersnaam</label><input id="username" name="new_username" maxlength="32" required autocomplete="username">
<label for="password">Nieuw wachtwoord</label><input id="password" name="new_password" type="password" maxlength="64" required autocomplete="new-password">
<button>Login opslaan</button></form>
<small>De nieuwe login wordt actief wanneer je herstel afsluit of het venster verloopt.</small>
<button id="end" type="button">Herstel afsluiten</button></fieldset>
<script>
let state,waiting=false;
const status=document.querySelector('#status'),message=document.querySelector('#message'),actions=document.querySelector('#actions');
async function refresh(){
 try{
  const response=await fetch('/recovery/status',{cache:'no-store'});
  if(!response.ok)throw Error('Status niet beschikbaar');
  state=await response.json();actions.disabled=!state.active||state.busy||waiting;
  status.textContent=state.active?'Herstel actief · '+Math.ceil(state.expires_in_ms/60000)+' min resterend':'Houd de fysieke herstelknop 5 seconden ingedrukt en laat hem daarna los.';
  if(waiting&&!state.busy){waiting=false;message.textContent=state.active?'Login opgeslagen. Sluit herstel af om hem te gebruiken.':'Herstel afgesloten. Open de gewone webinterface.';actions.disabled=!state.active;}
  if(state.error)message.textContent='Herstelactie mislukt. Er is niet herstart. Probeer opnieuw.';
 }catch(error){actions.disabled=true;status.textContent='Geen verbinding. Controleer je netwerk.';}
}
async function post(path,data=new URLSearchParams()){
 if(!state?.active||state.busy||waiting)return;
 data.set('csrf_token',state.csrf_token);data.set('generation',String(state.generation));
 actions.disabled=true;message.textContent='Bezig…';
 try{const response=await fetch(path,{method:'POST',body:data});if(!response.ok)throw Error();waiting=true;await refresh();}
 catch(error){message.textContent='Actie niet bevestigd. Controleer de status voordat je opnieuw probeert.';await refresh();}
}
document.querySelector('#login').onsubmit=event=>{event.preventDefault();const data=new URLSearchParams(new FormData(event.target));document.querySelector('#password').value='';post('/recovery/web-auth',data);};
document.querySelector('#end').onclick=()=>post('/recovery/end');
refresh();setInterval(refresh,1000);
</script></html>)HTML";

}  // namespace esphome::openquatt_recovery
