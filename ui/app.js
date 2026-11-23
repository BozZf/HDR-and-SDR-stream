'use strict';
const byId = (id) => document.getElementById(id);
const bridge = globalThis.chrome?.webview;
const send = (message) => bridge?.postMessage(message);
const closeDialog = byId('close-dialog');
const audioDialog = byId('audio-dialog');
const preview = byId('preview-slot');
const theme = byId('theme');
let running = false;
let lastLayout = '';

function layoutPreview() {
  const box = preview.getBoundingClientRect();
  const visible = running && !closeDialog.open && !audioDialog.open && box.top >= 0 &&
    box.left >= 0 && box.bottom <= innerHeight && box.right <= innerWidth;
  const scale = devicePixelRatio;
  const message = visible ? `layout:${[box.left + 1, box.top + 1, box.width - 2, box.height - 2]
    .map((number) => Math.round(number * scale)).join(':')}` : 'layout:0:0:0:0';
  if (message !== lastLayout) {
    lastLayout = message;
    send(message);
  }
}

function replaceOptions(select, values, selected, emptyLabel) {
  const options = values.map((label, index) => new Option(label, String(index)));
  if (!options.length) options.push(new Option(emptyLabel, '-1'));
  select.replaceChildren(...options);
  select.value = String(selected);
  if (select.selectedIndex < 0) select.selectedIndex = 0;
}

function updateMode() {
  byId('gdi-warning').hidden = byId('mode').value !== '2';
  const hdr = byId('mode').value === '1';
  byId('exposure').disabled = hdr;
  byId('white').disabled = hdr;
}

function updateSliders() {
  byId('exposure-value').value = `${Number(byId('exposure').value) / 10} EV`;
  byId('white-value').value = `${byId('white').value} nits`;
  byId('gain-value').value = `${byId('audio-gain').value}%`;
}

function applyTheme(value) {
  theme.value = value === '1' ? '1' : '0';
  document.documentElement.dataset.theme = theme.value === '1' ? 'light' : 'dark';
}

bridge?.addEventListener('message', ({ data }) => {
  if (data.type === 'state') {
    running = data.running;
    applyTheme(String(data.theme));
    for (const id of ['source-kind', 'resolution', 'rate', 'mode', 'exposure', 'white']) {
      byId(id).value = String(data[id]);
    }
    replaceOptions(byId('source'), data.sources, data.source, 'Nenhuma origem disponível');
    byId('start').disabled = !data.sources.length;
    byId('start').textContent = running ? 'Trocar origem' : 'Iniciar captura';
    byId('stop').disabled = !running;
    byId('show').disabled = !running;
    byId('lower').disabled = !data.output;
    byId('hide').disabled = !data.output;
    byId('live').classList.toggle('active', running);
    byId('live').textContent = running ? 'Capturando' : 'Parada';
    updateMode();
    updateSliders();
    layoutPreview();
  } else if (data.type === 'status' || data.type === 'audio-status') {
    byId(data.type).textContent = data.text;
  } else if (data.type === 'audio-settings') {
    replaceOptions(byId('audio-destination'), data.destinations, data.destination, 'Não selecionado');
    byId('audio-mode').value = String(data.mode);
    byId('audio-gain').value = String(data.gain);
    updateSliders();
  } else if (data.type === 'confirm-close') {
    if (!closeDialog.open) {
      if (audioDialog.open) audioDialog.close();
      closeDialog.returnValue = '';
      byId('remember-close').checked = false;
      closeDialog.showModal();
      layoutPreview();
    }
  }
});

theme.addEventListener('change', () => {
  applyTheme(theme.value);
  send(`theme:${theme.value}`);
});

for (const id of ['source-kind', 'source', 'resolution', 'rate', 'mode']) {
  byId(id).addEventListener('change', () => {
    if (id === 'mode') updateMode();
    send(`${id}:${byId(id).value}`);
  });
}
for (const id of ['exposure', 'white', 'audio-gain']) {
  byId(id).addEventListener('input', () => {
    updateSliders();
    if (id !== 'audio-gain') send(`${id}:${byId(id).value}`);
  });
}
for (const id of ['refresh', 'start', 'stop', 'show', 'lower', 'hide', 'audio-refresh', 'reset-close', 'exit']) {
  byId(id).addEventListener('click', () => send(id));
}
byId('audio').addEventListener('click', () => {
  byId('audio-error').hidden = true;
  audioDialog.showModal();
  layoutPreview();
  send('audio-open');
});
byId('audio-apply').addEventListener('click', () => {
  const mode = byId('audio-mode').value;
  const destination = byId('audio-destination').value;
  if (mode !== '0' && Number(destination) <= 0) {
    byId('audio-error').textContent = 'Selecione um dispositivo de saída.';
    byId('audio-error').hidden = false;
    return;
  }
  send(`audio-apply:${mode}:${destination}:${byId('audio-gain').value}`);
  audioDialog.close();
});
closeDialog.addEventListener('close', () => {
  if (['minimize', 'exit'].includes(closeDialog.returnValue)) {
    send(`close-${closeDialog.returnValue}:${Number(byId('remember-close').checked)}`);
  } else {
    send('close-cancel');
  }
  lastLayout = '';
  layoutPreview();
});
audioDialog.addEventListener('close', layoutPreview);
new ResizeObserver(layoutPreview).observe(preview);
window.addEventListener('resize', layoutPreview);
window.addEventListener('scroll', layoutPreview, { passive: true });
updateSliders();
send('ready');
