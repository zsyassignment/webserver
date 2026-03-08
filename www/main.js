const textarea = document.getElementById('message');
const sendBtn = document.getElementById('send');
const metaEl = document.getElementById('meta');
const chatLog = document.getElementById('chat-log');
const themeToggle = document.getElementById('theme-toggle');

let history = [];

const savedTheme = localStorage.getItem('theme') || 'dark';
applyTheme(savedTheme);

function applyTheme(theme) {
  document.documentElement.setAttribute('data-theme', theme);
  if (themeToggle) {
    themeToggle.textContent = theme === 'dark' ? 'Switch to light' : 'Switch to dark';
  }
  localStorage.setItem('theme', theme);
}

function renderHistory() {
  chatLog.innerHTML = '';
  if (!history.length) {
    const empty = document.createElement('div');
    empty.className = 'empty';
    empty.textContent = 'No messages yet. Start the conversation.';
    chatLog.appendChild(empty);
    return;
  }

  history.forEach((msg) => {
    const row = document.createElement('div');
    row.className = `message-row ${msg.role}`;

    const avatar = document.createElement('div');
    avatar.className = 'avatar';
    avatar.textContent = msg.role === 'assistant' ? 'AI' : 'You';

    const bubble = document.createElement('div');
    bubble.className = `bubble ${msg.role}`;

    const role = document.createElement('div');
    role.className = 'role';
    role.textContent = msg.role === 'assistant' ? 'Assistant' : 'You';

    const content = document.createElement('div');
    content.className = 'content';
    content.textContent = msg.content;

    bubble.append(role, content);
    row.append(avatar, bubble);
    chatLog.appendChild(row);
  });

  chatLog.scrollTop = chatLog.scrollHeight;
}

async function sendMessage() {
  const text = textarea.value.trim();
  if (!text) {
    metaEl.textContent = 'Type something first.';
    return;
  }

  const requestHistory = [...history];
  history.push({ role: 'user', content: text });
  renderHistory();

  textarea.value = '';
  textarea.focus();
  sendBtn.disabled = true;
  metaEl.textContent = 'Waiting for server response...';

  try {
    const res = await fetch('/api/chat', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message: text, history: requestHistory })
    });
    if (!res.ok) {
      const errText = await res.text();
      throw new Error(`${res.status} ${res.statusText} ${errText}`);
    }
    const data = await res.json();
    if (Array.isArray(data.history)) {
      history = data.history;
    } else if (data.answer) {
      history.push({ role: 'assistant', content: data.answer });
    }
    renderHistory();
    metaEl.textContent = data.cached ? 'Served from cache.' : 'Fresh response from model.';
  } catch (err) {
    metaEl.textContent = 'Request failed.';

    const row = document.createElement('div');
    row.className = 'message-row assistant';
    const avatar = document.createElement('div');
    avatar.className = 'avatar';
    avatar.textContent = 'Err';
    const bubble = document.createElement('div');
    bubble.className = 'bubble error';
    bubble.textContent = err.message;
    row.append(avatar, bubble);
    chatLog.appendChild(row);
    chatLog.scrollTop = chatLog.scrollHeight;
  } finally {
    sendBtn.disabled = false;
  }
}

sendBtn.addEventListener('click', sendMessage);
textarea.addEventListener('keydown', (e) => {
  if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') {
    sendMessage();
  }
});

if (themeToggle) {
  themeToggle.addEventListener('click', () => {
    const current = document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
    applyTheme(current === 'dark' ? 'light' : 'dark');
  });
}

renderHistory();
