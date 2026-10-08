const $=id=>document.getElementById(id);
const esc=value=>String(value??"").replace(/[&<>"']/g,ch=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[ch]));
let screens=[],filtered=[],selected=null,format="wide";
const dimensions={wide:[1920,1080],ultra:[2560,1080],compact:[1280,720]};
const isIngame=s=>!["Entrada","Diálogos de entrada","Transições","Configurações"].includes(s.group)&&!s.id.startsWith("F")&&!s.id.startsWith("D")&&!s.id.startsWith("L")&&!s.id.startsWith("O");
const presentationRows=rows=>(rows||[]).filter(r=>! /^(Produto|Grade da loja|Mochila|Equipamento|Armazenamento geral|Grupo exibido|Partida ilustrativa|Áreas de crédito)$/i.test(r.label));
const rowsHtml=rows=>presentationRows(rows).length?'<div class="rows">'+presentationRows(rows).map(r=>'<div class="data-row"><span>'+esc(r.label)+'</span><span class="value">'+esc(r.value)+'</span></div>').join("")+'</div>':"";
const choicesHtml=choices=>choices?.length?'<div class="choices">'+choices.map(c=>{const label=typeof c==="string"?c:c.label||c.title||JSON.stringify(c);return /^(Partidas públicas|Carregando|Nenhuma partida|Nenhum jogo|Sem partidas)/i.test(label)?'<div class="informational-choice">'+esc(label)+'</div>':'<button class="choice" type="button">'+esc(label)+'</button>'}).join("")+'</div>':"";
const fieldsHtml=fields=>fields?.length?fields.map(f=>'<label class="field"><span>'+esc(f.label)+'</span><input type="'+esc(f.type==="number"?"number":f.type==="password"?"password":"text")+'" value="'+esc(f.value)+'" maxlength="'+(Number(f.maxLength)||128)+'" readonly></label>').join(""):"";
const actionsHtml=buttons=>buttons?.length?'<div class="gothic-actions">'+buttons.map(b=>{const label=typeof b==="string"?b:b.label||b.title;return '<button type="button" '+(/desabilitado|indisponível/i.test(label)?'disabled':"")+'>'+esc(label)+'</button>'}).join("")+'</div>':"";
const description=s=>s.summary?'<p class="description">'+esc(s.summary)+'</p>':"";
function panel(s,body,classes=""){const footer=s.id==="O34"?"Esc · Cancelar captura":s.layout==="chat"?"Enter · Enviar  /  Esc · Cancelar":s.group==="Comércio"||s.group==="NPCs"?"Esc · Voltar":isIngame(s)?"Esc · Fechar":"Esc · Voltar";return '<section class="gothic-panel '+classes+'"><h3>'+esc(s.title)+'</h3><div class="panel-body">'+body+'</div>'+(s.tabs?tabsHtml(s.tabs):"")+actionsHtml(s.buttons)+(s.footerHtml||"")+((s.noFooter||s.id?.startsWith("F03")||s.id?.startsWith("F02"))?'':'<p class="panel-foot">'+footer+'</p>')+'</section>'}
function grid(rows,cols=10,single=false){let cells="";for(let i=0;i<rows*cols;i++){let label="",extra="";if(i===2)label=single?"Oferta":"Poção";if(!single&&i===15)label="Perg.";if(!single&&i===31)label="Elmo";if(!single&&i===7&&rows===4){label="Espada";extra='style="grid-column:8;grid-row:1 / 4;aspect-ratio:auto"'}if(!single&&rows===4&&[17,27].includes(i))continue;if(!single&&i===46&&rows>4){label="Armadura";extra='style="grid-column:7 / 9;grid-row:5 / 8;aspect-ratio:auto"'}if(!single&&rows>4&&[47,56,57,66,67].includes(i))continue;cells+='<div '+extra+' class="cell '+(label?"item":"")+'">'+(label?'<span class="item-label">'+label+'</span>':"")+'</div>'}return '<div class="inventory-grid" style="grid-template-columns:repeat('+cols+',1fr)" aria-label="'+cols+' colunas e '+rows+' linhas">'+cells+'</div>'}
function equipment(){return '<div class="equipment" aria-label="Sete locais de equipamento">'+[["head","Cabeça"],["amulet","Amuleto"],["hand1","Mão esquerda"],["torso","Torso"],["hand2","Mão direita"],["ring1","Anel"],["ring2","Anel"]].map(([cls,label])=>'<div class="equip-slot '+cls+'">'+label+'</div>').join("")+'</div>'}
function map(){return '<div class="map-canvas"><svg viewBox="0 0 700 340" role="img" aria-label="Traçado fictício de mapa"><g fill="none" stroke="#aaa17a" stroke-width="3"><path d="M80 245L185 185 290 245 395 185 500 245 605 185M185 185V80L290 20 395 80V185M290 245V320M500 245V320M395 80L500 20 605 80V185"/><path d="M110 245L185 205 290 265 395 205 500 265 575 225M205 175V90L290 40 375 90V175" opacity=".4"/></g><path d="M390 180l-8 18 18-6z" fill="#f4dec0"/><rect x="280" y="230" width="18" height="8" fill="#727eaf"/></svg></div>'}
function spellTiles(s){const names=(s.choices?.length?s.choices:["Cura","Bola de fogo","Relâmpago","Portal da cidade"]).slice(0,12);return '<div class="spell-grid">'+names.map((v,i)=>'<div class="spell-tile '+(i?"invalid":"")+'"><b>'+["✦","☄","ϟ","◇"][i%4]+'</b>'+esc(typeof v==="string"?v:v.label)+'<small>'+(i<4?"F"+(i+5):"Sem vínculo")+'</small></div>').join("")+'</div>'}
function generic(s){let choices=s.choices||[];if(s.buttons?.length&&choices.every(c=>s.buttons.includes(c)))choices=[];return fieldsHtml(s.fields)+rowsHtml(s.rows)+choicesHtml(choices)}
function tabsHtml(choices){return '<div class="book-tabs">'+(choices||[]).map(c=>'<button type="button">'+esc(c)+'</button>').join("")+'</div>'}
function settingsRows(s){return '<div class="rows">'+presentationRows(s.rows).map(r=>{const slider=s.id.startsWith("G")&&/música|^som$|gamma|velocidade/i.test(r.label);return '<div class="data-row setting-row"><span>'+esc(r.label)+'</span><span class="value">'+esc(r.value)+(slider?'<span class="faux-slider"><i></i></span>':"")+'</span></div>'}).join("")+'</div>'}
function storefront(s){return '<div class="store-items">'+presentationRows(s.rows).map((r,i)=>'<div class="store-item">'+(s.layout==="store-thumbnails"&&i?'<div class="item-thumb">'+["","⚔","◇","▱"][i%4]+'</div>':"")+'<div><strong>'+esc(r.label)+'</strong><p>'+esc(r.value)+'</p></div></div>').join("")+'</div>'}
function characterHtml(s){return '<div class="rows character-rows">'+(s.rows||[]).map(r=>{const attr=/^(Força|Magia|Destreza|Vitalidade) —/i.test(r.label);return '<div class="data-row '+(attr?'attribute-row':"")+'"><span>'+esc(r.label)+'</span><span class="value">'+esc(r.value)+(attr?'<button class="attribute-plus" aria-label="Aumentar atributo">+</button>':"")+'</span></div>'}).join("")+'</div>'}
function renderPreview(s){
 const scene=$("scene"),[w,h]=dimensions[format];scene.style.width=w+"px";scene.style.height=h+"px";scene.className="scene "+(isIngame(s)?"ingame ":"")+format;$("stage").style.aspectRatio=w+"/"+h;
 const splash=s.id.startsWith("F01"),transition=s.id.startsWith("L01");
 const logo=!isIngame(s)&&!transition?'<img class="menu-logo '+(splash?"splash-logo":"")+'" src="images/logo-d3d-reference.webp" alt="Logo autoral D3D preservado">':"";
 let body="";
 if(splash){body='<div class="splash-credit">'+esc(s.rows?.[0]?.value||"Diablo · Blizzard Entertainment")+'</div>'}
 else if(transition){body='<div class="transition-progress"><div class="transition-title">'+esc(s.title)+'</div><div class="progress-track"><span></span></div></div>'}
 else if(["G19","G20","G21","G22"].includes(s.id)){body='<div class="'+(s.id==="G19"?"pause-message":"state-message")+'">'+esc(s.rows?.[0]?.value||s.title)+'</div>'}
 else if(s.layout==="combined"||s.layout==="inspect"){
  const char=screens.find(v=>v.id==="P01"),inv=screens.find(v=>v.id==="P02");
  const inspect=s.layout==="inspect";
  const c={...char,id:s.id,title:inspect?"ALIADO · PERSONAGEM":"PERSONAGEM · C",buttons:[]},iv={...inv,id:s.id,title:inspect?"ALIADO · INVENTÁRIO":"INVENTÁRIO · I",buttons:[],noFooter:true,footerHtml:rowsHtml([{label:"Ouro",value:"2.450"}])};
  const charRows=inspect?(char?.rows||[]).filter(r=>!/pontos a distribuir/i.test(r.label)):(char?.rows||[]);
  body='<div class="combined"><div class="combo-tabs"><button data-tab="char">Personagem</button><button data-tab="inv">Inventário</button></div>'+panel(c,inspect?rowsHtml(charRows):characterHtml({...c,rows:charRows}),"sidepanel left")+panel(iv,equipment()+grid(4),"sidepanel right")+'</div>'+(inspect?'<span class="readonly-tag">INSPEÇÃO · SOMENTE LEITURA</span>':"");
 } else if(s.layout==="inventory"){body=panel({...s,noFooter:true,footerHtml:rowsHtml(s.rows)},equipment()+grid(4),"solo-side")}
 else if(s.layout==="character"){body=panel({...s,buttons:[]},characterHtml(s),"sidepanel left")}
 else if(s.layout==="stash"||s.layout==="visual-store"){body=panel(s,tabsHtml(s.choices)+grid(s.layout==="stash"?10:9,10,/Wirt/i.test(s.title))+rowsHtml(s.rows),"storage-panel")+panel({id:s.id,title:"INVENTÁRIO",buttons:[],group:s.group},equipment()+grid(4),"sidepanel right")}
 else if(s.layout==="spellbook"){body=panel({...s,tabs:s.choices},(s.rows||[]).map(r=>'<div class="book-entry '+(!r.label?"empty":"")+'">'+(r.label?'<span class="spell-emblem">✦</span>':"")+esc(r.label)+'<small>'+esc(r.value)+'</small></div>').join(""),"solo-side")}
 else if(s.layout==="speedbook"){body=panel(s,tabsHtml(s.choices)+'<div class="spell-grid">'+(s.rows||[]).map((r,i)=>'<div class="spell-tile '+(i===1||i===3?"invalid":"")+'"><b>'+["⚒","☄","✦","◇","ϟ"][i%5]+'</b>'+esc(r.label)+'<small>'+esc(r.value)+'</small></div>').join("")+'</div>',"speedbook-panel")}
 else if(s.layout==="gamepad"){body='<div class="cross-hints">'+(s.choices||[]).slice(0,4).map((c,i)=>'<div class="hint hint-'+i+'">'+esc(c)+'</div>').join("")+'<div class="hint-center">✥</div></div>'}
 else if(s.layout==="touch"){body='<div class="touch-stick"><span>✥</span></div><div class="touch-actions">'+["Principal","Secundária","Magia","Cancelar"].map((t,i)=>'<div class="touch-action a'+i+'">'+t+'</div>').join("")+'</div><div class="touch-menu">☰ Menu</div><div class="touch-potions"><span>Vida</span><span>Mana</span></div>'}
 else if(s.layout==="map"||s.layout==="map-opaque"){body=panel(s,map()+rowsHtml(s.rows)+tabsHtml(s.choices),"map-panel")}
 else if(s.layout==="map-transparent"){body='<div class="transparent-map">'+map()+'</div><div class="map-location">'+esc(s.rows?.[0]?.value||"Catedral · Nível 1")+'</div>'}
 else if(s.layout==="minimap"){body=panel({...s,title:"Mapa · Tab",buttons:[],noFooter:true},map(),"mini-panel")}
 else if(s.layout==="parchment"){body=panel(s,'<div class="parchment">'+map()+'</div>',"parchment-panel")}
 else if(s.layout==="narrative"){body=panel(s,(s.rows||[]).map(r=>'<p class="narrative-copy">'+esc(r.value).replace(/^Trecho ilustrativo:\s*/,"")+'</p>').join(""),"narrative-panel")}
 else if(s.layout==="credits"){body=panel(s,'<div class="credits-copy">'+presentationRows(s.rows).map(r=>'<p><small>'+esc(r.label)+'</small><br>'+esc(r.value)+'</p>').join("")+'</div>',"credits-panel")}
 else if(s.layout==="death"){body='<div class="death-shade"></div><div class="death-message"><h3>VOCÊ MORREU</h3>'+rowsHtml(s.rows)+actionsHtml(s.buttons)+'</div>'}
 else if(s.layout==="notification"){body='<div class="state-message">'+esc(s.rows?.[0]?.value||s.title)+'</div>'}
 else if(s.layout==="party"){body='<div class="party-panel">'+["Mira","Thalen","Ardan"].map((name,i)=>'<div class="party-member"><div class="party-portrait">♟</div><div><strong>'+name+'</strong><div class="life-strip"><i style="width:'+(90-i*18)+'%"></i></div><div class="mana-strip"><i style="width:'+(50+i*10)+'%"></i></div></div></div>').join("")+'</div>'}
 else if(s.layout==="chat"){body='<div class="chat-overlay"><div class="chat-history">Mira: Vamos nos encontrar na entrada.<br>Thalen: Estou a caminho.</div>'+fieldsHtml(s.fields)+choicesHtml(s.choices)+'</div>'}
 else if(s.layout==="chatlog"||s.layout==="help"||s.layout==="console"){body=panel(s,generic(s),"wide-panel "+(s.layout==="console"?"console-panel":""))}
 else if(s.layout==="quests"){body=panel(s,choicesHtml(s.choices)+rowsHtml((s.rows||[]).filter(r=>!(s.choices||[]).some(c=>String(c).includes(r.label)))),"sidepanel left")}
 else if(s.layout==="store"||s.layout==="store-thumbnails"){body=panel(s,storefront(s),"store-panel")}
 else if(s.layout==="settings"){body=panel(s,fieldsHtml(s.fields)+settingsRows(s)+choicesHtml(s.choices),"")}
 else if(s.layout==="loading"){body=panel(s,'<div class="progress-track"><span></span></div>'+rowsHtml(s.rows),"dialog-panel")}
 else if(["dialog","gold","item"].includes(s.layout)){body=panel(s,generic(s),"dialog-panel")}
 else if(s.id.startsWith("F04")||s.id.startsWith("F05")){body=panel(s,'<div class="hero-selection"><div class="hero-portrait"><span>♟</span></div><div>'+generic(s)+'</div></div>',"hero-panel")}
 else{body=panel(s,generic(s),"")}
 scene.innerHTML='<div class="concept-badge">CONCEITO · NÃO É CAPTURA DO JOGO · '+esc(s.id)+'</div>'+logo+body;
 requestAnimationFrame(fitScene);
}
function fitScene(){const [w,h]=dimensions[format],focus=document.body.classList.contains("focus-view"),scale=focus?Math.min($("stage").clientWidth/w,$("stage").clientHeight/h):$("stage").clientWidth/w;$("scene").style.transform=(focus?"translate(-50%,-50%) ":"")+"scale("+scale+")"}
function filterScreens(){const q=$("search").value.toLocaleLowerCase("pt-BR"),g=$("group").value,condition=$("conditional").checked;filtered=screens.filter(s=>(!g||s.group===g)&&(condition||s.availability==="atual")&&JSON.stringify(s).toLocaleLowerCase("pt-BR").includes(q));renderList()}
function renderList(){let last="";$("screen-list").innerHTML=filtered.map(s=>{const header=last!==s.group?'<div class="nav-group">'+esc(s.group)+'</div>':"";last=s.group;return header+'<button class="nav-item '+(selected?.id===s.id?"active":"")+'" data-id="'+esc(s.id)+'"><span class="nav-id">'+esc(s.id)+'</span>'+esc(s.title)+(s.availability!=="atual"?'<small>'+esc(s.availability==="debug"?"Desenvolvimento":"Condicional")+'</small>':"")+'</button>'}).join("");$("count").textContent=filtered.length+" exemplos · "+screens.length+" no catálogo";}
function selectScreen(id,updateHash=true){selected=screens.find(s=>s.id===id)||screens[0];if(!selected)return;showMode("screens");$("screen-id").textContent=selected.id+" · "+selected.group;$("screen-title").textContent=selected.title;renderPreview(selected);$("open-image").href="examples/"+selected.id+".jpg";$("availability").innerHTML='<span class="availability-tag">'+esc(selected.availability==="atual"?"Existe no código":selected.availability==="debug"?"Somente debug":"Condicional")+'</span>'+esc(selected.condition||"Auditoria estática; exemplo da apresentação proposta.");$("summary").textContent=selected.summary||"";$("notes").innerHTML=(selected.notes||[]).map(n=>'<li>'+esc(n)+'</li>').join("");$("sources").innerHTML=(selected.source||[]).map(src=>'<li>'+esc(typeof src==="string"?src:src.file+":"+src.line)+'</li>').join("");$("variants").innerHTML=(selected.variants||[]).map(v=>'<li>'+esc(typeof v==="string"?v:(v.title||"Variante")+" — "+(v.condition||""))+'</li>').join("");if(updateHash)history.replaceState(null,"","#"+encodeURIComponent(selected.id));renderList();window.scrollTo({top:0});}
function showMode(mode){$("concepts").hidden=mode!=="concepts";$("screens").hidden=mode!=="screens";$("show-concepts").classList.toggle("active",mode==="concepts");$("show-screens").classList.toggle("active",mode==="screens");$("format-buttons").style.visibility=mode==="screens"?"visible":"hidden";requestAnimationFrame(fitScene)}
$("show-concepts").onclick=()=>showMode("concepts");$("show-screens").onclick=()=>selectScreen(selected?.id||"F03");$("start-catalog").onclick=()=>selectScreen("F03");
$("search").oninput=filterScreens;$("group").onchange=filterScreens;$("conditional").onchange=filterScreens;
$("screen-list").onclick=e=>{const b=e.target.closest("[data-id]");if(b)selectScreen(b.dataset.id)};
$("format-buttons").onclick=e=>{const b=e.target.closest("[data-format]");if(!b)return;format=b.dataset.format;document.querySelectorAll("[data-format]").forEach(x=>x.classList.toggle("active",x===b));if(selected)renderPreview(selected)};
$("previous").onclick=()=>{const i=filtered.findIndex(s=>s.id===selected?.id);selectScreen(filtered[(i-1+filtered.length)%filtered.length]?.id)};
$("next").onclick=()=>{const i=filtered.findIndex(s=>s.id===selected?.id);selectScreen(filtered[(i+1)%filtered.length]?.id)};
$("open-focus").onclick=()=>window.open("index.html?focus=1&id="+encodeURIComponent(selected.id)+"&format="+format,"_blank");
$("scene").onclick=e=>{const b=e.target.closest(".choice");if(b){b.parentElement.querySelectorAll(".choice").forEach(x=>x.classList.remove("selected"));b.classList.add("selected")}const tab=e.target.closest("[data-tab]");if(tab)$("scene").querySelector(".combined")?.classList.toggle("show-char",tab.dataset.tab==="char")};
new ResizeObserver(fitScene).observe($("stage"));
async function init(){
 try{
  const data=window.HUD_CATALOG||await Promise.all(["frontend","options","ingame","ingame-variants"].map(async f=>{const r=await fetch("data/"+f+".json");if(!r.ok)throw Error(f);return r.json()}));
  screens=data.flatMap(d=>d.screens||[]);const expanded=[];
  screens.forEach(s=>{expanded.push(s);(s.variants||[]).forEach((v,i)=>{
   if(typeof v!=="object"||/Nenhum herói salvo/i.test(v.title||""))return;
   expanded.push({...s,...v,id:s.id+"-V"+(i+1),title:v.title||s.title,condition:v.condition||s.condition,variants:[],summary:v.summary||s.summary})
  })});
  screens=expanded;const seen=new Set();
  screens=screens.filter(s=>{if(seen.has(s.id))throw Error("ID repetido: "+s.id);seen.add(s.id);return true});
  const groups=[...new Set(screens.map(s=>s.group))];screens.sort((a,b)=>groups.indexOf(a.group)-groups.indexOf(b.group));
  for(const g of groups)$("group").insertAdjacentHTML("beforeend",'<option value="'+esc(g)+'">'+esc(g)+'</option>');
  filterScreens();const params=new URLSearchParams(location.search);
  if(params.get("focus")==="1")document.body.classList.add("focus-view");
  if(dimensions[params.get("format")])format=params.get("format");
  const id=params.get("id")||decodeURIComponent(location.hash.slice(1));if(id)selectScreen(id,false);else showMode("concepts");
  window.HUD_STUDY={screens,selectScreen,renderPreview,dimensions};
 }catch(e){$("count").textContent="Não foi possível carregar os dados. Abra pelo servidor local.";console.error(e)}
}
init();
