#pragma once

namespace esphome::openquatt_recovery {

// Self-contained by design: never load the main application, entities or logs.
static constexpr const char RECOVERY_PAGE[] = R"HTML(<!doctype html>
<html lang="nl"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenQuatt herstel</title>
<style>
/* Palette and radii match openquatt/web/css/src/00-tokens.css; no app assets needed. */
body{font:16px/1.6 system-ui,sans-serif;max-width:38rem;margin:clamp(1rem,5vw,3rem) auto;padding:0 1.25rem;color:#111827;background:#f9fafb}
h1{font-size:2rem;line-height:1.2;letter-spacing:-.04em;margin:0}.brand{display:flex;align-items:center;gap:.75rem;margin-bottom:1rem}.brand svg{flex-shrink:0}h2{font-size:1.15rem;line-height:1.4;margin:0 0 .75rem}
p{margin:.75rem 0;color:#4b5563}header{border-top:4px solid #ea580c;padding-top:1.5rem}
label,input,button,small{display:block}label{font-weight:600}input,button{font:inherit;padding:.7rem 1rem;margin:.4rem 0 1rem;box-sizing:border-box;max-width:100%;border:1px solid #d1d5db;border-radius:10px}
input{width:100%;background:#fff;color:inherit}button{cursor:pointer;background:#fff;color:#111827;font-weight:600;min-height:44px}
button:hover:not(:disabled){background:#fff7ed;border-color:#c2410c}button:disabled{cursor:default;opacity:.55}
input:focus-visible,button:focus-visible{outline:2px solid #c2410c;outline-offset:3px}
#login button{background:#c2410c;border-color:#c2410c;color:#fff}#login button:hover:not(:disabled){background:#9a3412}
small{color:#4b5563}#status{padding:1rem;border-left:3px solid #ea580c;background:#fff7ed;color:#7c2d12;border-radius:0 10px 10px 0}
#message:empty{display:none}#message{font-weight:600}fieldset{border:0;padding:0;margin:0;min-width:0}
form,section{padding:1.5rem 0;border-bottom:1px solid #e5e7eb}#end{margin-top:1.5rem}
</style>
<main><header>
<div class="brand">
<!-- Inline mark from openquatt/web/assets/brand/favicon.svg. -->
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="48" height="48" aria-hidden="true" focusable="false">
<rect width="64" height="64" rx="12" fill="#0F1724"/>
<g transform="translate(8.96 8.96) scale(0.461)">
<path d="M60.161 77.339 A37.5 37.5 0 1 1 77.839 59.661 L65.208 47.03 A21.5 21.5 0 1 0 47.53 64.708 Z" fill="#F8FAFC"/>
<path d="M54 53.5 L80 79.5" fill="none" stroke="#F97316" stroke-width="16" stroke-linecap="round"/>
</g></svg>
<h1>OpenQuatt herstel</h1>
</div>
<p>Dit scherm geeft uitsluitend toegang tot herstelacties. De gewone webinterface blijft afgeschermd.</p>
<p>Op de HeatPump Controller Q edition is de herstelknop de <strong>linker van de twee knoppen</strong>.</p>
</header>
<p id="status">Status ophalen…</p><p id="message" role="status" aria-live="polite"></p>
<fieldset id="actions" disabled><form id="login">
<h2>Web-login instellen</h2>
<label for="username">Gebruikersnaam</label><input id="username" name="new_username" maxlength="32" required autocomplete="username">
<label for="password">Nieuw wachtwoord</label><input id="password" name="new_password" type="password" maxlength="64" required autocomplete="new-password">
<button>Login opslaan</button>
<small>Na gecontroleerde opslag herstart de controller. De nieuwe login wordt bij die boot actief.</small>
</form><section>
<h2>Home Assistant opnieuw koppelen</h2>
<p>Verwijder de opgeslagen API-beveiligingssleutel. Alle API-clients verliezen hun koppeling en de controller herstart. Daarna kan Home Assistant 10 minuten lang opnieuw koppelen. Home Assistant kan dezelfde sleutel opnieuw instellen.</p>
<button id="api-reset" type="button">API-beveiliging resetten</button></section>
<section id="wifi" hidden><h2>Wi-Fi opnieuw instellen</h2>
<p>Wis de opgeslagen Wi-Fi-gegevens en herstart. Verbind daarna met het OpenQuatt access point en stel Wi-Fi opnieuw in. Web-login, API-beveiliging en overige instellingen blijven behouden.</p>
<p>Dit kan ook zonder browser: houd de herstelknop 10 seconden vast en laat hem daarna los.</p>
<button id="wifi-reset" type="button">Wi-Fi wissen en herstarten</button></section>
<button id="end" type="button">Herstel afsluiten</button></fieldset>
</main>
<script>
let state,waiting=false,accepted=false,pendingGeneration=0,pendingToken='',readSequence=0;
const status=document.querySelector('#status'),message=document.querySelector('#message'),actions=document.querySelector('#actions');
async function refresh(){
 const sequence=++readSequence;
 try{
  const response=await fetch('/recovery/status',{cache:'no-store',signal:AbortSignal.timeout(5000)});
  if(!response.ok)throw Error('Status niet beschikbaar');
  const next=await response.json();if(sequence!==readSequence)return;
  state=next;actions.disabled=!state.active||state.busy||waiting;
  document.querySelector('#wifi').hidden=!state.capabilities.wifi_reset;
  status.textContent=state.active?'Herstel actief · '+Math.ceil(state.expires_in_ms/60000)+' min resterend':'Houd de fysieke herstelknop 5 seconden ingedrukt en laat hem daarna los.';
  if(state.active&&state.button_held&&state.next_threshold_ms&&state.capabilities.wifi_reset)status.textContent+=' · Laat nu los, of houd nog '+Math.ceil(state.next_threshold_ms/1000)+' s vast om Wi-Fi te wissen.';
  const matches=state.generation===pendingGeneration&&state.csrf_token===pendingToken;
  if(waiting&&!matches){message.textContent='Controller opnieuw bereikbaar. Controleer de nieuwe login of koppeling; de vorige actie is niet bevestigd.';actions.disabled=true;}
  if(state.error&&(!waiting||(accepted&&matches&&!state.busy))){waiting=false;message.textContent='Herstelactie mislukt. Controleer de instellingen en kies expliciet een nieuwe actie.';actions.disabled=!state.active||state.busy;}
 }catch(error){if(sequence!==readSequence)return;actions.disabled=true;status.textContent='Geen verbinding. Controleer je netwerk.';}
}
async function post(path,data=new URLSearchParams()){
 if(!state?.active||state.busy||waiting)return;
 data.set('csrf_token',state.csrf_token);data.set('generation',String(state.generation));
 actions.disabled=true;message.textContent='Bezig…';
 try{++readSequence;waiting=true;accepted=false;pendingGeneration=state.generation;pendingToken=state.csrf_token;const response=await fetch(path,{method:'POST',body:data,signal:AbortSignal.timeout(5000)});++readSequence;if(response.status!==202)throw Error();accepted=true;message.textContent='Verzoek geaccepteerd. Na gecontroleerde opslag volgt een herstart. Controleer daarna de nieuwe login of koppeling.';await refresh();}
 catch(error){++readSequence;message.textContent='Actie niet bevestigd. Herlaad na het opnieuw verbinden en controleer de status voordat je een nieuwe actie kiest.';await refresh();}
}
document.querySelector('#login').onsubmit=event=>{event.preventDefault();const data=new URLSearchParams(new FormData(event.target));document.querySelector('#password').value='';post('/recovery/web-auth',data);};
document.querySelector('#end').onclick=()=>post('/recovery/end');
document.querySelector('#api-reset').onclick=()=>{if(confirm('API-beveiliging wissen en controller herstarten? Dit verbreekt alle API-koppelingen.'))post('/api-security/reset',new URLSearchParams({confirm:'RESET_API_SECURITY'}));};
document.querySelector('#wifi-reset').onclick=()=>{if(confirm('Wi-Fi-gegevens wissen en controller herstarten? Stel daarna Wi-Fi opnieuw in via het OpenQuatt access point.'))post('/wifi/reset',new URLSearchParams({confirm:'RESET_WIFI'}));};
refresh();setInterval(refresh,1000);
</script></html>)HTML";

}  // namespace esphome::openquatt_recovery
