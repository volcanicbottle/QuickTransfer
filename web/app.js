const $ = (s) => document.querySelector(s);
let self = null, peers = [], current = null, messages = [], phoneMode = false;

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

function phoneId() {
  let id = localStorage.getItem('qt_phone_id');
  if (!id) {
    id = [...crypto.getRandomValues(new Uint8Array(8))]
      .map((b) => b.toString(16).padStart(2, '0')).join('');
    localStorage.setItem('qt_phone_id', id);
  }
  return id;
}

function showLogin() {
  $('#login').hidden = false;
  const urlPin = new URLSearchParams(location.search).get('pin');
  if (urlPin && /^\d{6}$/.test(urlPin)) {
    $('#login-pin').value = urlPin;
    setTimeout(() => $('#login-name').focus(), 0);
  }
  $('#login-btn').onclick = async () => {
    const name = $('#login-name').value.trim();
    const pin = $('#login-pin').value.trim();
    if (!name || !/^\d{6}$/.test(pin)) {
      $('#login-err').textContent = '请填写设备名和 6 位 PIN';
      return;
    }
    const device = /Mobi|Android|iPhone/i.test(navigator.userAgent) ? 'phone' : 'pc';
    try {
      await api('/api/phone/register', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ phone_id: phoneId(), name, pin, device }),
      });
      location.replace('/');  // 抹掉带 PIN 的地址
    } catch (e) {
      $('#login-err').textContent = String(e).includes('429')
        ? '尝试次数过多，请稍后再试' : 'PIN 错误，请核对电脑屏幕上的数字';
    }
  };
}

async function refreshPeers() {
  peers = await api('/api/peers');
  renderPeers();
}

function renderPeers() {
  const el = $('#peer-list');
  const wasOpen = !!el.querySelector('details')?.open;  // 重建后保留折叠区的展开状态
  el.innerHTML = '';
  const mkRow = (p) => {
    const d = document.createElement('div');
    d.className = 'peer' + (current === p.id ? ' active' : '') + (p.online ? '' : ' offline');
    const dot = document.createElement('span');
    dot.className = 'dot';
    const name = document.createElement('span');
    name.className = 'pname';
    const isPc = p.type === 'pc' || p.device === 'pc';
    name.textContent = (isPc ? '💻 ' : '📱 ') + p.name + (p.paired ? '' : ' 🔒');
    d.append(dot, name);
    d.onclick = () => (p.paired ? selectPeer(p.id) : pairWith(p));
    return d;
  };
  const paired = peers.filter((p) => p.paired);
  const unpaired = peers.filter((p) => !p.paired);
  for (const p of paired) el.appendChild(mkRow(p));
  if (unpaired.length) {
    if (!paired.length) {
      for (const p of unpaired) el.appendChild(mkRow(p));  // 还没配对过：平铺方便首次配对
    } else {
      const det = document.createElement('details');
      det.open = wasOpen;
      const sum = document.createElement('summary');
      sum.textContent = `未配对设备 (${unpaired.length})`;
      det.appendChild(sum);
      for (const p of unpaired) det.appendChild(mkRow(p));
      el.appendChild(det);
    }
  }
  if (!peers.length) {
    el.innerHTML = '<div class="empty">正在搜索局域网设备…<br>请确认对方已启动本程序</div>';
  }
}

function makeQR(text) {
  const qr = qrcode(0, 'M');  // type 0 = 自动选版本
  qr.addData(text);
  qr.make();
  return qr.createDataURL(6);  // 每格 6px
}

function showQR() {
  const ips = self.lan_ips || [];
  const sel = $('#qr-ip');
  const render = (ip) => {
    const url = `http://${ip}:${self.port}/?pin=${self.pin}`;
    $('#qr-url').textContent = url;
    $('#qr-img').innerHTML = '';
    const img = new Image();
    img.src = makeQR(url);
    $('#qr-img').appendChild(img);
  };
  if (!ips.length) {
    sel.hidden = true;
    $('#qr-img').innerHTML = '';
    $('#qr-url').textContent = '未检测到局域网 IP，请确认已连接 Wi-Fi 或网线';
  } else {
    sel.hidden = ips.length < 2;
    sel.innerHTML = '';
    ips.forEach((ip) => {
      const o = document.createElement('option');
      o.value = ip; o.textContent = ip;
      sel.appendChild(o);
    });
    sel.onchange = () => render(sel.value);
    render(ips[0]);
  }
  $('#qr-modal').hidden = false;
}

async function quitService() {
  if (!confirm('确定要停止服务吗？停止后本页和所有手机都将断开。')) return;
  try { await fetch('/api/quit', { method: 'POST' }); } catch (e) { /* 服务关闭会断连，忽略 */ }
  document.body.innerHTML =
    '<div style="padding:48px;text-align:center;font-size:18px;color:#333">服务已停止，可关闭本页。</div>';
}

async function pairWith(p) {
  const pin = prompt(`与「${p.name}」配对\n请输入对方屏幕上显示的 6 位 PIN：`);
  if (!pin) return;
  try {
    await api('/api/pair', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ peer_id: p.id, pin: pin.trim() }),
    });
    await refreshPeers();
  } catch (e) {
    alert(String(e).includes('429') ? '对方已锁定，请 5 分钟后再试'
                                    : '配对失败：PIN 错误或对方不在线');
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
  const mine = phoneMode ? m.direction === 'in' : m.direction === 'out';
  const d = document.createElement('div');
  d.className = 'msg ' + (mine ? 'out' : 'in') + (m.status === 'fail' ? ' fail' : '');
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
      (!phoneMode && m.direction === 'in' && m.file_path ? ' · 已保存到 ' + m.file_path : '');
    b.append(fname, fmeta);
    const downloadable = m.status === 'ok' && m.file_path &&
      (phoneMode ? m.direction === 'out' : true);
    if (downloadable) {
      const a = document.createElement('a');
      a.className = 'dl';
      a.href = '/api/file?id=' + m.id;
      a.textContent = '⬇ 下载';
      b.appendChild(a);
    }
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
    const m = phoneMode
      ? await api('/api/phone/send-text', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ text: t }),
        })
      : await api('/api/send-text', {
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
      const url = phoneMode
        ? '/api/phone/send-file'
        : '/api/send-file?peer=' + encodeURIComponent(current);
      const m = await api(url, { method: 'POST', body: fd });
      upsertMessage(m);
    } catch (e) {
      failed.push(f.name);  // 单个失败不中断后续文件
    }
  }
  if (failed.length) alert('以下文件发送失败：' + failed.join('、'));
}

const es = new EventSource('/api/events');
es.onopen = () => {
  if (current) {
    api('/api/messages?peer=' + encodeURIComponent(current))
      .then((ms) => { messages = ms; renderMessages(); })
      .catch(() => {});
  }
  if (!phoneMode) refreshPeers();
};
es.onmessage = (e) => {
  const ev = JSON.parse(e.data);
  if (ev.type === 'peers' && !phoneMode) refreshPeers();
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
  try {
    self = await api('/api/self');
  } catch (e) {
    if (String(e).includes('401')) { showLogin(); return; }
    throw e;
  }
  if (self.is_remote) {
    phoneMode = true;
    document.body.classList.add('phone');
    document.title = '多端互通 - ' + self.name;
    $('#chat-header').textContent = self.name;
    $('#composer').hidden = false;
    current = phoneId();
    messages = await api('/api/messages?peer=' + encodeURIComponent(current));
    renderMessages();
    setInterval(() => api('/api/phone/heartbeat', { method: 'POST' }).catch(() => {}), 10000);
  } else {
    $('#self-name').textContent = '本机：' + self.name;
    $('#self-pin').textContent = '配对 PIN：' + self.pin;
    $('#qr-btn').hidden = false;
    $('#quit-btn').hidden = false;
    $('#qr-btn').onclick = showQR;
    $('#quit-btn').onclick = quitService;
    const closeQR = () => { $('#qr-modal').hidden = true; };
    $('#qr-close').onclick = closeQR;
    $('#qr-modal').onclick = (e) => { if (e.target === $('#qr-modal')) closeQR(); };  // 点击遮罩关闭
    document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeQR(); });
    document.title = '多端互通 - ' + self.name;
    await refreshPeers();
    setInterval(refreshPeers, 5000);
  }
})();
