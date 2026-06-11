const $ = (s) => document.querySelector(s);
let self = null, peers = [], current = null, messages = [];

async function api(path, opts) {
  const r = await fetch(path, opts);
  if (!r.ok) throw new Error('请求失败: ' + r.status);
  return r.json();
}

function fmtSize(n) {
  if (n < 1024) return n + ' B';
  if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
  if (n < 1073741824) return (n / 1048576).toFixed(1) + ' MB';
  return (n / 1073741824).toFixed(2) + ' GB';
}

function fmtTime(ts) { return new Date(ts).toLocaleString('zh-CN'); }

async function refreshPeers() {
  peers = await api('/api/peers');
  renderPeers();
}

function renderPeers() {
  const el = $('#peer-list');
  el.innerHTML = '';
  for (const p of peers) {
    const d = document.createElement('div');
    d.className = 'peer' + (current === p.id ? ' active' : '') + (p.online ? '' : ' offline');
    const dot = document.createElement('span');
    dot.className = 'dot';
    const name = document.createElement('span');
    name.className = 'pname';
    name.textContent = p.name;
    d.append(dot, name);
    d.onclick = () => selectPeer(p.id);
    el.appendChild(d);
  }
  if (!peers.length) {
    el.innerHTML = '<div class="empty">正在搜索局域网设备…<br>请确认对方已启动本程序</div>';
  }
}

async function selectPeer(id) {
  current = id;
  const p = peers.find((x) => x.id === id);
  $('#chat-header').textContent = p ? p.name : '';
  $('#composer').hidden = false;
  messages = await api('/api/messages?peer=' + encodeURIComponent(id));
  renderMessages();
  renderPeers();
}

function msgEl(m) {
  const d = document.createElement('div');
  d.className = 'msg ' + m.direction + (m.status === 'fail' ? ' fail' : '');
  d.dataset.id = m.id;
  const b = document.createElement('div');
  b.className = 'bubble';
  if (m.kind === 'text') {
    b.textContent = m.body;
  } else {
    const fname = document.createElement('div');
    fname.className = 'fname';
    fname.textContent = '📄 ' + m.file_name;
    const fmeta = document.createElement('div');
    fmeta.className = 'fmeta';
    fmeta.textContent = fmtSize(m.file_size) +
      (m.direction === 'in' && m.file_path ? ' · 已保存到 ' + m.file_path : '');
    b.append(fname, fmeta);
    if (m.status === 'pending') {
      const bar = document.createElement('div');
      bar.className = 'bar';
      bar.appendChild(document.createElement('i'));
      b.appendChild(bar);
    }
  }
  d.appendChild(b);
  const meta = document.createElement('div');
  meta.className = 'meta';
  meta.textContent = fmtTime(m.ts) +
    (m.status === 'fail' ? ' · 发送失败' : m.status === 'pending' ? ' · 发送中…' : '');
  if (m.status === 'fail' && m.direction === 'out') {
    const r = document.createElement('a');
    r.href = '#';
    r.textContent = ' 重试';
    r.onclick = (e) => {
      e.preventDefault();
      api('/api/retry', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ message_id: m.id }),
      }).catch(() => alert('重试请求失败，请稍后再试'));
    };
    meta.appendChild(r);
  }
  d.appendChild(meta);
  return d;
}

function renderMessages() {
  const box = $('#messages');
  box.innerHTML = '';
  for (const m of messages) box.appendChild(msgEl(m));
  box.scrollTop = box.scrollHeight;
}

function upsertMessage(m) {
  if (m.peer_id !== current) return;
  const i = messages.findIndex((x) => x.id === m.id);
  if (i >= 0) messages[i] = m;
  else messages.push(m);
  renderMessages();
}

async function sendText() {
  const t = $('#text-input').value.trim();
  if (!t || !current) return;
  $('#text-input').value = '';
  try {
    const m = await api('/api/send-text', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ peer_id: current, text: t }),
    });
    upsertMessage(m);
  } catch (e) {
    $('#text-input').value = t;  // 失败把内容还给输入框，不能静默丢
    alert('发送失败：对方可能已离线');
  }
}

async function sendFiles(files) {
  if (!current) return;
  const failed = [];
  for (const f of files) {
    const fd = new FormData();
    fd.append('file', f);
    try {
      const m = await api('/api/send-file?peer=' + encodeURIComponent(current), {
        method: 'POST',
        body: fd,
      });
      upsertMessage(m);
    } catch (e) {
      failed.push(f.name);  // 单个失败不中断后续文件
    }
  }
  if (failed.length) alert('以下文件发送失败：' + failed.join('、'));
}

const es = new EventSource('/api/events');
es.onopen = () => {
  // SSE 断线重连后补拉当前会话，找回断线窗口期丢失的消息
  if (current) selectPeer(current);
  refreshPeers();
};
es.onmessage = (e) => {
  const ev = JSON.parse(e.data);
  if (ev.type === 'peers') refreshPeers();
  if (ev.type === 'message' || ev.type === 'message_update') upsertMessage(ev.message);
  if (ev.type === 'progress') {
    const el = document.querySelector(`.msg[data-id="${ev.message_id}"] .bar i`);
    if (el) el.style.width = ((ev.sent * 100) / ev.total).toFixed(1) + '%';
  }
};

$('#send-btn').onclick = sendText;
$('#text-input').addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault();
    sendText();
  }
});
$('#file-btn').onclick = () => $('#file-input').click();
$('#file-input').onchange = () => {
  sendFiles($('#file-input').files);
  $('#file-input').value = '';
};
$('#messages').addEventListener('dragover', (e) => e.preventDefault());
$('#messages').addEventListener('drop', (e) => {
  e.preventDefault();
  sendFiles(e.dataTransfer.files);
});

(async () => {
  self = await api('/api/self');
  $('#self-name').textContent = '本机：' + self.name;
  document.title = '多端互通 - ' + self.name;
  await refreshPeers();
  setInterval(refreshPeers, 5000);  // 兜底轮询，处理设备下线
})();
