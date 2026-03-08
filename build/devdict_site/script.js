let DOC_INDEX = null;

function badgeClass(ch){
  if(ch==="R") return "r";
  if(ch==="G") return "g";
  if(ch==="B") return "b";
  return "a";
}

function renderEntry(e){
  const ch = e.channel || "A";
  const bc = badgeClass(ch);
  const kind = (e.kind || "").toUpperCase();
  const sig = e.sig ? `<div class="small"><code>${escapeHtml(e.sig)}</code></div>` : "";
  return `
    <div class="entry">
      <div class="badges">
        <span class="badge ${bc}">${ch}</span>
        <span class="badge">${kind}</span>
        <span class="badge">${e.category || ""}</span>
      </div>
      <h3>${escapeHtml(e.name)}</h3>
      <p>${escapeHtml(e.desc || "")}</p>
      ${sig}
      <div class="small">위치: ${escapeHtml(e.loc || "")}</div>
    </div>
  `;
}

function goHome(){
  document.getElementById("content").innerHTML = `
    <h1>Welcome</h1>
    <p>좌측 탭 또는 검색으로 OS 기능/문서를 탐색하세요.</p>
    <div class="entry">
      <h3>Docs</h3>
      <p>프로젝트 문서(docs/)는 <a class="pilllink" href="#" onclick="openDocs();return false;">Docs 탭</a>에서 바로 읽을 수 있어요.</p>
    </div>
  `;
}

function openTab(name){
  const items = (typeof DOCS !== "undefined" && DOCS[name]) ? DOCS[name] : [];
  let html = `<h2>${name.toUpperCase()}</h2>`;
  html += `<div class="small">항목 수: ${items.length} (구조체/열거형/상수/함수)</div>`;
  items.forEach(e=>{ html += renderEntry(e); });
  document.getElementById("content").innerHTML = html;
}

async function loadDocsIndex(){
  if(DOC_INDEX) return DOC_INDEX;
  try{
    const r = await fetch("docs_index.json");
    DOC_INDEX = await r.json();
    return DOC_INDEX;
  }catch(e){
    DOC_INDEX = {docs:[]};
    return DOC_INDEX;
  }
}

async function openDocs(){
  const idx = await loadDocsIndex();
  const docs = idx.docs || [];
  let html = `<h2>DOCS</h2>`;
  html += `<div class="small">docs/ 폴더의 문서들을 한 곳에서 열람합니다.</div>`;
  if(docs.length===0){
    html += `<div class="entry"><p>문서를 찾지 못했습니다. (docs_index.json 비어있음)</p></div>`;
    document.getElementById("content").innerHTML = html;
    return;
  }
  docs.forEach(d=>{
    html += `
      <div class="entry">
        <h3>${escapeHtml(d.title)}</h3>
        <p class="small">source: ${escapeHtml(d.source || "")}</p>
        <a class="pilllink" href="${escapeHtml(d.path)}" target="_blank">열기</a>
      </div>
    `;
  });
  document.getElementById("content").innerHTML = html;
}

function support(){
  window.location.href = "mailto:SJPUPRO@GMAIL.COM?subject=SJ%20CANVAS%20OS%20Support";
}

function escapeHtml(s){
  return String(s)
    .replaceAll("&","&amp;")
    .replaceAll("<","&lt;")
    .replaceAll(">","&gt;")
    .replaceAll("\"","&quot;")
    .replaceAll("'","&#039;");
}

document.getElementById("searchBox").addEventListener("input", async e=>{
  const q = (e.target.value || "").trim().toLowerCase();
  if(q.length===0){ goHome(); return; }

  let results = [];
  if(typeof DOCS !== "undefined"){
    Object.values(DOCS).forEach(tab=>{
      tab.forEach(item=>{
        const hay = (item.name+" "+(item.desc||"")+" "+(item.sig||"")+" "+(item.loc||"")+" "+(item.category||"")).toLowerCase();
        if(hay.includes(q)) results.push({type:"symbol", item});
      });
    });
  }

  const idx = await loadDocsIndex();
  (idx.docs||[]).forEach(d=>{
    const hay = (d.title+" "+(d.source||"")).toLowerCase();
    if(hay.includes(q)) results.push({type:"doc", doc:d});
  });

  let html = `<h2>검색 결과</h2>`;
  html += `<div class="small">키워드: <code>${escapeHtml(q)}</code> / 결과: ${results.length}</div>`;

  const docs = results.filter(r=>r.type==="doc");
  if(docs.length){
    html += `<h3>문서 (docs/)</h3>`;
    docs.slice(0,40).forEach(r=>{
      html += `
        <div class="entry">
          <h3>${escapeHtml(r.doc.title)}</h3>
          <p class="small">source: ${escapeHtml(r.doc.source || "")}</p>
          <a class="pilllink" href="${escapeHtml(r.doc.path)}" target="_blank">열기</a>
        </div>
      `;
    });
  }

  const sym = results.filter(r=>r.type==="symbol");
  if(sym.length){
    html += `<h3>기능/심볼</h3>`;
    sym.slice(0,80).forEach(r=>{ html += renderEntry(r.item); });
  }

  document.getElementById("content").innerHTML = html;
});

goHome();
