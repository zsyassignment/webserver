const textarea = document.getElementById('message');
const sendBtn = document.getElementById('send');
const answerEl = document.getElementById('answer');
const metaEl = document.getElementById('meta');

async function sendMessage() {
  const text = textarea.value.trim();
  if (!text) {
    metaEl.textContent = 'Type something first.';
    return;
  }
  sendBtn.disabled = true;
  metaEl.textContent = 'Waiting for server response...';
  answerEl.textContent = '';
  try {
    const res = await fetch('/api/chat', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message: text })
    });
    if (!res.ok) {
      const errText = await res.text();
      throw new Error(`${res.status} ${res.statusText} ${errText}`);
    }
    const data = await res.json();
    answerEl.textContent = data.answer || 'No answer received.';
    metaEl.textContent = data.cached ? 'Served from cache.' : 'Fresh response from model.';
  } catch (err) {
    answerEl.textContent = err.message;
    metaEl.textContent = 'Request failed.';
  } finally {
    sendBtn.disabled = false;
  }
}

sendBtn.addEventListener('click', sendMessage);
textarea.addEventListener('keydown', (e) => {
  if (e.metaKey && e.key === 'Enter') {
    sendMessage();
  }
});
