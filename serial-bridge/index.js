require('dotenv').config();

const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

const SERVER_BASE = (process.env.SERVER_BASE || 'http://127.0.0.1:8000').replace(/\/$/, '');
const BRIDGE_ID = process.env.BRIDGE_ID || 'laptop-bridge-1';
const COMMAND_POLL_MS = Number(process.env.COMMAND_POLL_MS || 1500);

const UNO_PORT = process.env.UNO_SERIAL_PORT || process.env.SERIAL_PORT || 'COM3';
const UNO_BAUDRATE = Number(process.env.UNO_SERIAL_BAUDRATE || process.env.SERIAL_BAUDRATE || 115200);
const UNO_DEVICE_NAME = process.env.UNO_DEVICE_NAME || process.env.DEVICE_NAME || 'uno-main';

const NANO_PORT = process.env.NANO_SERIAL_PORT || '';
const NANO_BAUDRATE = Number(process.env.NANO_SERIAL_BAUDRATE || 115200);
const NANO_DEVICE_NAME = process.env.NANO_DEVICE_NAME || 'nano-main';

if (typeof fetch !== 'function') {
  console.error('[Bridge] Node 18+ is required because global fetch is not available.');
  process.exit(1);
}

let unoPort = null;
let nanoPort = null;
let unoPollTimer = null;

async function postJson(path, payload) {
  const response = await fetch(`${SERVER_BASE}${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });

  if (!response.ok) {
    const message = await response.text();
    throw new Error(message || `${response.status}`);
  }

  return response.status === 204 ? null : response.json();
}

async function getJson(path) {
  const response = await fetch(`${SERVER_BASE}${path}`);
  if (!response.ok) {
    const message = await response.text();
    throw new Error(message || `${response.status}`);
  }
  return response.json();
}

async function forwardPayload(payload, portLabel) {
  try {
    await postJson('/api/sensors/events', payload);
    console.log(`[Bridge:${portLabel}] forwarded sensor payload:`, payload.kind || 'unknown');
  } catch (error) {
    console.error(`[Bridge:${portLabel}] failed to forward sensor payload:`, error.message);
  }
}

function attachParser(port, portLabel) {
  const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

  parser.on('data', (rawLine) => {
    const line = rawLine.trim();
    if (!line) return;

    let payload;
    try {
      payload = JSON.parse(line);
    } catch (error) {
      console.warn(`[Bridge:${portLabel}] invalid JSON:`, line);
      return;
    }

    void forwardPayload(payload, portLabel);
  });
}

async function pollUnoCommands() {
  if (!unoPort || !unoPort.isOpen) return;

  try {
    const commands = await getJson(`/api/device/commands/pending?device=${encodeURIComponent(UNO_DEVICE_NAME)}`);
    if (!Array.isArray(commands) || commands.length === 0) return;

    for (const command of commands) {
      const serialPayload = {
        cmd: command.cmd,
        worker: command.worker || undefined,
        color: command.color || undefined,
        state: command.state || undefined,
        ...command.payload,
      };

      await new Promise((resolve, reject) => {
        unoPort.write(`${JSON.stringify(serialPayload)}\n`, (error) => {
          if (error) {
            reject(error);
            return;
          }
          unoPort.drain((drainError) => (drainError ? reject(drainError) : resolve()));
        });
      });

      await postJson(`/api/device/commands/${command.id}/ack`, { bridge_id: BRIDGE_ID });
      console.log('[Bridge:uno] command sent to Arduino:', command.cmd, command.worker || '');
    }
  } catch (error) {
    console.error('[Bridge:uno] failed to poll/send commands:', error.message);
  }
}

function openPort(path, baudRate, label) {
  if (!path) return null;

  const port = new SerialPort({ path, baudRate });
  port.on('open', () => {
    console.log(`[Bridge:${label}] serial port opened: ${path} @ ${baudRate}`);
  });
  port.on('error', (error) => {
    console.error(`[Bridge:${label}] serial port error:`, error.message);
  });
  attachParser(port, label);
  return port;
}

unoPort = openPort(UNO_PORT, UNO_BAUDRATE, 'uno');
nanoPort = openPort(NANO_PORT, NANO_BAUDRATE, 'nano');

if (unoPort) {
  unoPollTimer = setInterval(pollUnoCommands, COMMAND_POLL_MS);
}

process.on('SIGINT', () => {
  if (unoPollTimer) clearInterval(unoPollTimer);
  console.log('\n[Bridge] shutting down...');

  const ports = [unoPort, nanoPort].filter(Boolean);
  if (ports.length === 0) {
    process.exit(0);
    return;
  }

  let closedCount = 0;
  ports.forEach((port) => {
    port.close(() => {
      closedCount += 1;
      if (closedCount === ports.length) process.exit(0);
    });
  });
});
